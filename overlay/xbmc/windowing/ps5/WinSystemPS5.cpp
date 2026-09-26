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
#include "filesystem/SpecialProtocol.h"
#include "guilib/DispResource.h"
#include "settings/DisplaySettings.h"
#include "utils/StringUtils.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"

#include "platform/ps5/input/PS5PadInput.h"
#include "platform/ps5/video/VideoCodecRegistration.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <utility>
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

  // The desktop mode is the system's own rate (what the console ran at when
  // Kodi started): the GUI and every stopped state use it. 120 Hz is an extra
  // mode, registered only after the first frame (so Kodi never starts in
  // it), for "Adjust display refresh rate" during playback.
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
  }
  // explicit-rate modes verified by a kodi-probe-modes trial
  for (const auto& [available, hz] :
       {std::pair{m_rate23976Available, 23.976f}, std::pair{m_rate50Available, 50.0f}})
  {
    if (!available)
      continue;
    RESOLUTION_INFO mode;
    UpdateDesktopResolution(mode, "PS5", m_outputWidth, m_outputHeight, hz, 0);
    mode.strMode =
        StringUtils::Format("{}x{} @ {:.3f}Hz (PS5)", m_outputWidth, m_outputHeight, hz);
    GetGfxContext().ResetOverscan(mode);
    CDisplaySettings::GetInstance().AddResolutionInfo(mode);
  }
  CDisplaySettings::GetInstance().ApplyCalibrations();

  CLog::Log(LOGINFO, "CWinSystemPS5: output {}x{} @ {:.2f} Hz{}{}{}", m_outputWidth,
            m_outputHeight, desktopHz, m_highRefreshAvailable ? ", 119.88 Hz" : "",
            m_rate23976Available ? ", 23.976 Hz" : "", m_rate50Available ? ", 50 Hz" : "");
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
  if (hz > 0.0f && m_activeOutput == ActiveOutput::System)
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
  if (access("/app0/kodi-probe-modes", F_OK) == 0)
  {
    KODI::PLATFORM::PS5::LogOutputModeSurvey();
    KODI::PLATFORM::PS5::LogModeStructLayout();
    RunModeTrial();
  }
  LoadModeResults();
  CLog::Log(LOGINFO, "CWinSystemPS5: output modes besides the system rate:{}{}{}",
            m_highRefreshAvailable ? " 119.88 Hz" : "", m_rate23976Available ? " 23.976 Hz" : "",
            m_rate50Available ? " 50 Hz" : "");
  UpdateResolutions();
}

namespace
{
std::string ModeResultsPath()
{
  return CSpecialProtocol::TranslatePath("special://home/ps5-output-modes.txt");
}
} // namespace

void CWinSystemPS5::RunModeTrial()
{
  // One attempt per explicit rate, always back to the system mode after it.
  // The TV blanks briefly for each. Only verified rates are saved.
  using namespace KODI::PLATFORM::PS5;
  std::string verified;
  for (const auto& [code, hz] :
       {std::pair{kRefreshCode23_98, 23.976f}, std::pair{kRefreshCode50, 50.0f}})
  {
    const int rc = SetOutputRefreshCode(code);
    const float reported = rc == 0 ? QueryRefreshRate() : 0.0f;
    const bool ok = rc == 0 && std::abs(reported - hz) < 0.1f;
    CLog::Log(ok ? LOGINFO : LOGWARNING,
              "CWinSystemPS5: trial {:.3f} Hz (code {:#x}): configure {:#x}, system reports "
              "{:.3f} Hz -> {}",
              hz, code, static_cast<uint32_t>(rc), reported, ok ? "usable" : "not usable");
    const int back = SetOutputMode(kOutputModeDefault);
    if (back != 0)
      CLog::Log(LOGWARNING, "CWinSystemPS5: trial: return to the system mode failed ({:#x})",
                static_cast<uint32_t>(back));
    if (ok)
      verified += StringUtils::Format("{:.3f}\n", hz);
  }
  std::ofstream out(ModeResultsPath(), std::ios::trunc);
  out << verified;
  CLog::Log(LOGINFO, "CWinSystemPS5: trial results saved to {} ({})", ModeResultsPath(),
            verified.empty() ? "no explicit rates usable" : "explicit rates usable");
}

void CWinSystemPS5::LoadModeResults()
{
  m_rate23976Available = m_rate50Available = false;
  std::ifstream in(ModeResultsPath());
  float hz = 0.0f;
  while (in >> hz)
  {
    if (std::abs(hz - 23.976f) < 0.01f)
      m_rate23976Available = true;
    else if (std::abs(hz - 50.0f) < 0.01f)
      m_rate50Available = true;
  }
}

float CWinSystemPS5::SwitchOutputRate(float requestedHz)
{
  // Fixed-rate output modes only: the system rate, the 120 Hz preset, and the
  // explicit 23.976/50 Hz modes a trial verified. Kodi requests a mode through
  // "Adjust display refresh rate" (start/stop of playback) or the GUI
  // resolution setting.
  using namespace KODI::PLATFORM::PS5;
  ActiveOutput target = ActiveOutput::System;
  if (m_highRefreshAvailable && requestedHz > 100.0f)
    target = ActiveOutput::High120;
  else if (m_rate23976Available && std::abs(requestedHz - 23.976f) < 0.05f)
    target = ActiveOutput::Rate23976;
  else if (m_rate50Available && std::abs(requestedHz - 50.0f) < 0.05f)
    target = ActiveOutput::Rate50;
  if (target == m_activeOutput)
    return m_fRefreshRate;

  // Like a mode switch elsewhere: display resources (renderer, vsync clock)
  // see a lost/reset display around it, so the clock restarts at the new rate.
  OnLostDevice();
  m_vblankClockUnreliable = false; // re-evaluated for the new mode

  // every change goes through the system mode
  int rc = m_activeOutput != ActiveOutput::System ? SetOutputMode(kOutputModeDefault) : 0;
  if (rc == 0)
  {
    switch (target)
    {
      case ActiveOutput::System:
        break;
      case ActiveOutput::High120:
        rc = SetOutputMode(kOutputModeHighRefresh);
        break;
      case ActiveOutput::Rate23976:
        rc = SetOutputRefreshCode(kRefreshCode23_98);
        break;
      case ActiveOutput::Rate50:
        rc = SetOutputRefreshCode(kRefreshCode50);
        break;
    }
  }
  if (rc != 0)
  {
    CLog::Log(LOGWARNING, "CWinSystemPS5: switching to {:.3f} Hz failed ({:#x}); back to the "
              "system rate", requestedHz, static_cast<uint32_t>(rc));
    // not offered again this session
    if (target == ActiveOutput::High120)
      m_highRefreshAvailable = false;
    else if (target == ActiveOutput::Rate23976)
      m_rate23976Available = false;
    else if (target == ActiveOutput::Rate50)
      m_rate50Available = false;
    SetOutputMode(kOutputModeDefault);
    target = ActiveOutput::System;
  }
  m_activeOutput = target;

  float hz = QueryRefreshRate();
  if (hz <= 0.0f)
    hz = target == ActiveOutput::System ? (m_systemRefresh > 0.0f ? m_systemRefresh : 60.0f)
                                        : requestedHz;
  m_fRefreshRate = hz;
  m_outputRefresh = hz;
  CLog::Log(LOGINFO, "CWinSystemPS5: output switched to {:.3f} Hz", hz);
  OnResetDevice();
  return hz;
}

void CWinSystemPS5::RestoreOutputMode()
{
  if (m_activeOutput == ActiveOutput::System)
    return;
  const int rc = KODI::PLATFORM::PS5::SetOutputMode(KODI::PLATFORM::PS5::kOutputModeDefault);
  CLog::Log(rc == 0 ? LOGINFO : LOGWARNING, "CWinSystemPS5: restored the system output mode ({:#x})",
            static_cast<uint32_t>(rc));
  m_activeOutput = ActiveOutput::System;
}
