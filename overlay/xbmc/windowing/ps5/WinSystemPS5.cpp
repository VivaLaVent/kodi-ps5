/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WinSystemPS5.h"

#include "VideoSyncPS5.h"
#include "platform/ps5/VideoOutInfo.h"

#include "ServiceBroker.h"
#include "guilib/DispResource.h"
#include "settings/DisplaySettings.h"
#include "utils/StringUtils.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"

#include "platform/ps5/input/PS5PadInput.h"
#include "platform/ps5/video/VideoCodecRegistration.h"

#include <algorithm>
#include <cmath>
#include <unistd.h>
#include <mutex>

using namespace KODI::WINDOWING::PS5;

CWinSystemPS5::CWinSystemPS5() = default;

CWinSystemPS5::~CWinSystemPS5() = default;

bool CWinSystemPS5::InitWindowSystem()
{
  if (!CWinSystemBase::InitWindowSystem())
    return false;

  m_padInput = std::make_unique<KODI::PLATFORM::PS5::CPadInput>();
  m_padInput->Start();

  // hardware H.264/HEVC decoding (falls back to FFmpeg per stream)
  KODI::PLATFORM::PS5::RegisterVideoCodecs();
  return true;
}

bool CWinSystemPS5::DestroyWindowSystem()
{
  if (m_padInput)
  {
    m_padInput->Stop();
    m_padInput.reset();
  }
  return CWinSystemBase::DestroyWindowSystem();
}

bool CWinSystemPS5::ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop)
{
  // Fixed output; nothing to resize.
  return true;
}

void CWinSystemPS5::UpdateResolutions()
{
  CWinSystemBase::UpdateResolutions();

  // The desktop mode is the system's own rate; 120 Hz is an extra mode.
  const float desktopHz = m_systemRefresh > 0.0f ? m_systemRefresh : m_outputRefresh;
  RESOLUTION_INFO& desktop = CDisplaySettings::GetInstance().GetResolutionInfo(RES_DESKTOP);
  UpdateDesktopResolution(desktop, "PS5", m_outputWidth, m_outputHeight, desktopHz, 0);
  CDisplaySettings::GetInstance().ClearCustomResolutions();

  if (m_highRefreshAvailable)
  {
    // 119.88 Hz pairs with a 59.94 Hz system rate, 120 with 60
    const float highHz = std::abs(desktopHz - 59.94f) < 0.05f ? 119.88f : 2.0f * desktopHz;
    RESOLUTION_INFO high;
    UpdateDesktopResolution(high, "PS5", m_outputWidth, m_outputHeight, highHz, 0);
    high.strMode = StringUtils::Format("{}x{} @ {:.2f}Hz (PS5 120 Hz mode)", m_outputWidth,
                                       m_outputHeight, highHz);
    GetGfxContext().ResetOverscan(high);
    CDisplaySettings::GetInstance().AddResolutionInfo(high);

    if (m_vrrAvailable)
    {
      // experimental: 120 Hz preset + VRR, presentation paced at 50 Hz
      RESOLUTION_INFO vrr;
      UpdateDesktopResolution(vrr, "PS5", m_outputWidth, m_outputHeight, kVrrModeHz, 0);
      vrr.strMode = StringUtils::Format("{}x{} @ {:.2f}Hz (PS5 VRR)", m_outputWidth,
                                        m_outputHeight, kVrrModeHz);
      GetGfxContext().ResetOverscan(vrr);
      CDisplaySettings::GetInstance().AddResolutionInfo(vrr);
    }
  }
  CDisplaySettings::GetInstance().ApplyCalibrations();

  CLog::Log(LOGINFO, "CWinSystemPS5: output {}x{} @ {:.2f} Hz{}", m_outputWidth, m_outputHeight,
            desktopHz, m_highRefreshAvailable ? ", 120 Hz mode available" : "");
}

void CWinSystemPS5::Register(IDispResource* resource)
{
  std::unique_lock lock(m_resourceSection);
  m_resources.push_back(resource);
}

void CWinSystemPS5::Unregister(IDispResource* resource)
{
  std::unique_lock lock(m_resourceSection);
  auto it = std::find(m_resources.begin(), m_resources.end(), resource);
  if (it != m_resources.end())
    m_resources.erase(it);
}

void CWinSystemPS5::OnLostDevice()
{
  CLog::Log(LOGDEBUG, "CWinSystemPS5::{} - notify display lost", __FUNCTION__);
  std::unique_lock lock(m_resourceSection);
  for (IDispResource* resource : m_resources)
    resource->OnLostDisplay();
}

void CWinSystemPS5::OnResetDevice()
{
  CLog::Log(LOGDEBUG, "CWinSystemPS5::{} - notify display reset", __FUNCTION__);
  std::unique_lock lock(m_resourceSection);
  for (IDispResource* resource : m_resources)
    resource->OnResetDisplay();
}

std::unique_ptr<CVideoSync> CWinSystemPS5::GetVideoSync(CVideoReferenceClock* clock)
{
  return std::make_unique<CVideoSyncPS5>(clock);
}

void CWinSystemPS5::ApplySystemRefreshRate(float hz)
{
  if (hz > 0.0f && !m_highRefreshActive)
    m_systemRefresh = hz;
  if (hz <= 0.0f || std::abs(hz - m_outputRefresh) < 0.005f)
    return;
  CLog::Log(LOGINFO, "CWinSystemPS5: system output runs at {:.3f} Hz (was assuming {:.3f})", hz,
            m_outputRefresh);
  m_outputRefresh = hz;
  m_fRefreshRate = hz;
  RESOLUTION_INFO& desktop = CDisplaySettings::GetInstance().GetResolutionInfo(RES_DESKTOP);
  desktop.fRefreshRate = hz;
  desktop.strMode = StringUtils::Format("{}x{} @ {:.2f}Hz (PS5)", desktop.iScreenWidth,
                                        desktop.iScreenHeight, hz);
}

void CWinSystemPS5::DetectOutputModes()
{
  m_highRefreshAvailable = KODI::PLATFORM::PS5::IsHighRefreshSupported();
  // Experimental 50 Hz VRR mode (needs sceVideoOutVrrUnpegFromFixedRate,
  // which firmware 10.01 does not export): only with the kodi-vrr switch.
  m_vrrAvailable = access("/app0/kodi-vrr", F_OK) == 0;
  if (m_vrrAvailable)
    CLog::Log(LOGINFO, "CWinSystemPS5: kodi-vrr: offering the experimental 50 Hz VRR mode");
  CLog::Log(LOGINFO, "CWinSystemPS5: 120 Hz output mode {}",
            m_highRefreshAvailable ? "available" : "not available");
  if (m_highRefreshAvailable)
    UpdateResolutions();
}

float CWinSystemPS5::SwitchOutputRate(float requestedHz)
{
  using namespace KODI::PLATFORM::PS5;
  const bool wantVrr = m_highRefreshAvailable && m_vrrAvailable &&
                       std::abs(requestedHz - kVrrModeHz) < 0.5f;
  const bool wantHigh = m_highRefreshAvailable && (wantVrr || requestedHz > 100.0f);
  const bool vrrActive = m_vrrTargetHz > 0.0f;
  if (wantHigh == m_highRefreshActive && wantVrr == vrrActive)
    return m_fRefreshRate;

  // Like a mode switch elsewhere: display resources (renderer, vsync clock)
  // see a lost/reset display around it, so the clock restarts at the new rate.
  OnLostDevice();
  m_vblankClockUnreliable = false; // re-evaluated for the new mode

  int rc = 0;
  if (vrrActive && wantHigh && !wantVrr)
    rc = SetOutputMode(kOutputModeDefault); // leave VRR: re-peg via the default mode
  if (rc == 0 && (wantHigh != m_highRefreshActive || (vrrActive && !wantVrr)))
    rc = SetOutputMode(wantHigh ? kOutputModeHighRefresh : kOutputModeDefault);
  if (rc != 0)
  {
    CLog::Log(LOGWARNING, "CWinSystemPS5: switching to {} failed ({:#x}); staying at {:.3f} Hz",
              wantHigh ? "120 Hz" : "the system rate", static_cast<uint32_t>(rc),
              m_fRefreshRate);
    if (wantHigh)
      m_highRefreshAvailable = false; // do not offer it again this session
    OnResetDevice();
    return m_fRefreshRate;
  }
  m_highRefreshActive = wantHigh;
  m_vrrTargetHz = 0.0f;

  float hz = 0.0f;
  if (wantVrr)
  {
    const int vrr = VrrUnpegFromFixedRate();
    if (vrr == 0)
    {
      m_vrrTargetHz = kVrrModeHz;
      hz = kVrrModeHz;
      CLog::Log(LOGINFO, "CWinSystemPS5: VRR active, presenting at {:.3f} Hz", hz);
    }
    else
    {
      CLog::Log(LOGWARNING, "CWinSystemPS5: VRR unpeg failed ({:#x}); staying at 120 Hz",
                static_cast<uint32_t>(vrr));
      m_vrrAvailable = false; // not offered again this session
    }
  }
  if (hz <= 0.0f)
    hz = QueryRefreshRate();
  if (hz <= 0.0f)
    hz = wantHigh ? 119.88f : (m_systemRefresh > 0.0f ? m_systemRefresh : 60.0f);
  m_fRefreshRate = hz;
  m_outputRefresh = hz;
  CLog::Log(LOGINFO, "CWinSystemPS5: output switched to {:.3f} Hz{}", hz,
            m_vrrTargetHz > 0.0f ? " (VRR)" : "");
  OnResetDevice();
  return hz;
}

void CWinSystemPS5::RestoreOutputMode()
{
  m_vrrTargetHz = 0.0f;
  if (!m_highRefreshActive)
    return;
  const int rc = KODI::PLATFORM::PS5::SetOutputMode(KODI::PLATFORM::PS5::kOutputModeDefault);
  CLog::Log(rc == 0 ? LOGINFO : LOGWARNING, "CWinSystemPS5: restored the system output mode ({:#x})",
            static_cast<uint32_t>(rc));
  m_highRefreshActive = false;
}
