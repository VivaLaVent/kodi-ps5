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
#include <cstdlib>
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

  // The desktop mode is the system's own rate: the GUI and every stopped
  // state use it. VRR modes (registered after the first frame, when the
  // system has shown it can do VRR) serve "Adjust display refresh rate".
  const float desktopHz = m_systemRefresh > 0.0f ? m_systemRefresh : m_outputRefresh;
  RESOLUTION_INFO& desktop = CDisplaySettings::GetInstance().GetResolutionInfo(RES_DESKTOP);
  UpdateDesktopResolution(desktop, "PS5", m_outputWidth, m_outputHeight, desktopHz, 0);
  CDisplaySettings::GetInstance().ClearCustomResolutions();

  if (m_vrrAvailable)
  {
    // the lowest multiple of common frame rates within the PS5's VRR range
    // (48-120 Hz): 23.976 -> 71.928, 24 -> 48, 25 -> 50, 29.97/30 -> 59.94/60
    for (const float hz : kVrrRates)
    {
      RESOLUTION_INFO vrr;
      UpdateDesktopResolution(vrr, "PS5", m_outputWidth, m_outputHeight, hz, 0);
      vrr.strMode = StringUtils::Format("{}x{} @ {:.3f}Hz {}", m_outputWidth, m_outputHeight, hz,
                                        kVrrModeTag);
      GetGfxContext().ResetOverscan(vrr);
      CDisplaySettings::GetInstance().AddResolutionInfo(vrr);
    }
  }
  CDisplaySettings::GetInstance().ApplyCalibrations();

  CLog::Log(LOGINFO, "CWinSystemPS5: output {}x{} @ {:.2f} Hz{}", m_outputWidth, m_outputHeight,
            desktopHz, m_vrrAvailable ? ", VRR modes for playback" : "");
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
  if (m_linkIsVrr)
    hz = 59.94f; // the link's ~120 Hz is not the rate Kodi should run at
  if (hz > 0.0f && !m_vrrActive)
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

void CWinSystemPS5::EnsureSystemMode()
{
  using namespace KODI::PLATFORM::PS5;
  const float before = QueryRefreshRate();
  const int rc = SetOutputMode(kOutputModeDefault);
  const float after = QueryRefreshRate();
  CLog::Log(rc == 0 ? LOGINFO : LOGWARNING,
            "CWinSystemPS5: system mode requested ({:#x}): output {:.3f} Hz before, {:.3f} Hz after",
            static_cast<uint32_t>(rc), before, after);
  // With the PS5's VRR setting on, the system keeps the title on a VRR link
  // (~120 Hz) whatever it requests; the display then follows Kodi's
  // presentation, so Kodi paces the menus at the system rate, 59.94 Hz.
  m_linkIsVrr = after > 100.0f;
  if (m_linkIsVrr)
    CLog::Log(LOGINFO, "CWinSystemPS5: the system runs Kodi on a VRR link ({:.3f} Hz): menus paced "
              "at 59.94 Hz", after);
}

void CWinSystemPS5::DetectOutputModes()
{
  // VRR needs the high-refresh preset (the PS5 turns it into VRR when its
  // VRR setting is on) and the function that releases its 120 Hz peg.
  using namespace KODI::PLATFORM::PS5;
  // VRR for playback: on the system's own VRR link, or through the high-
  // refresh preset plus the unpeg (as ProsperoLight engages it).
  const bool preset = IsHighRefreshSupported();
  const bool unpeg = IsVrrUnpegAvailable();
  m_vrrAvailable = m_linkIsVrr || (preset && unpeg);
  CLog::Log(LOGINFO, "CWinSystemPS5: VRR for playback {}{}",
            m_vrrAvailable ? "available" : "not available",
            m_linkIsVrr ? " (system VRR link)" : (m_vrrAvailable ? " (high-refresh preset)" : ""));
  if (m_vrrAvailable)
    UpdateResolutions();
}

float CWinSystemPS5::SwitchOutputRate(const RESOLUTION_INFO& res)
{
  // Kodi requests a "(PS5 VRR)" mode only through "Adjust display refresh
  // rate" (patch 0012: On start/stop, during playback) and the desktop mode
  // otherwise, so the requested mode alone decides. "Sync playback to
  // display" plays no part in it.
  using namespace KODI::PLATFORM::PS5;
  const bool vrrMode = res.strMode.find(kVrrModeTag) != std::string::npos;
  const bool wantVrr = vrrMode && m_vrrAvailable && res.fRefreshRate > 0.0f;

  if (!wantVrr)
  {
    if (m_vrrActive)
    {
      OnLostDevice();
      // as ProsperoLight returns to its launcher: request the system mode
      const int rc = SetOutputMode(kOutputModeDefault);
      m_vrrActive = false;
      m_vrrTargetHz = 0.0f;
      m_vblankClockUnreliable = false;
      RefreshLinkState();
      m_fRefreshRate = m_outputRefresh = m_systemRefresh > 0.0f ? m_systemRefresh : 59.94f;
      CLog::Log(LOGINFO, "CWinSystemPS5: display mode {}: VRR off ({:#x}); {}", res.strMode,
                static_cast<uint32_t>(rc), PacingDescription());
      OnResetDevice();
    }
    else if (vrrMode)
      CLog::Log(LOGWARNING, "CWinSystemPS5: display mode {} requested, but VRR is not available: "
                "system rate; {}", res.strMode, PacingDescription());
    return m_fRefreshRate;
  }

  if (!m_vrrActive)
  {
    // Engage VRR. On the system's VRR link the display already follows our
    // presentation; the unpeg is still issued, as ProsperoLight does, and only
    // logged. Otherwise: the high-refresh preset (VRR pegged at 120 Hz), then
    // the unpeg; if either fails, back to the system mode at once.
    OnLostDevice();
    int rc = 0;
    const char* step = "VRR unpeg";
    if (m_linkIsVrr)
    {
      const int unpegRc = VrrUnpegFromFixedRate();
      CLog::Log(LOGINFO, "CWinSystemPS5: VRR link: unpeg {:#x}", static_cast<uint32_t>(unpegRc));
    }
    else
    {
      rc = SetOutputMode(kOutputModeHighRefresh);
      step = "high-refresh preset";
      if (rc == 0)
      {
        step = "VRR unpeg";
        rc = VrrUnpegFromFixedRate();
      }
    }
    if (rc != 0)
    {
      SetOutputMode(kOutputModeDefault);
      RefreshLinkState();
      m_vrrAvailable = m_linkIsVrr; // without a VRR link, not offered again this session
      CLog::Log(LOGWARNING, "CWinSystemPS5: display mode {}: VRR not engaged ({} {:#x}); {}",
                res.strMode, step, static_cast<uint32_t>(rc), PacingDescription());
      OnResetDevice();
      if (!m_linkIsVrr)
        return m_fRefreshRate;
    }
    m_vrrActive = true;
    m_vblankClockUnreliable = false;
    OnResetDevice();
  }
  m_vrrTargetHz = res.fRefreshRate;
  m_fRefreshRate = m_outputRefresh = res.fRefreshRate;
  CLog::Log(LOGINFO, "CWinSystemPS5: display mode {}: VRR on; {}", res.strMode,
            PacingDescription());
  return m_fRefreshRate;
}

void CWinSystemPS5::RefreshLinkState()
{
  // A VRR link reports ~120 Hz; the display then follows our presentation,
  // so presentation must always be paced on it (never unthrottled).
  const float hz = KODI::PLATFORM::PS5::QueryRefreshRate();
  if (hz > 0.0f)
    m_linkIsVrr = hz > 100.0f;
}

std::string CWinSystemPS5::PacingDescription() const
{
  const float pace = VrrTargetRate();
  return pace > 0.0f ? StringUtils::Format("presenting at {:.3f} Hz on the VRR link", pace)
                     : StringUtils::Format("fixed-rate output at {:.3f} Hz", m_fRefreshRate);
}

void CWinSystemPS5::RestoreOutputMode()
{
  if (!m_vrrActive)
    return;
  const int rc = KODI::PLATFORM::PS5::SetOutputMode(KODI::PLATFORM::PS5::kOutputModeDefault);
  CLog::Log(rc == 0 ? LOGINFO : LOGWARNING, "CWinSystemPS5: restored the system output mode ({:#x})",
            static_cast<uint32_t>(rc));
  m_vrrActive = false;
  m_vrrTargetHz = 0.0f;
}
