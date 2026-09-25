/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "WinSystemPS5.h"

#include "ServiceBroker.h"
#include "guilib/DispResource.h"
#include "settings/DisplaySettings.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"

#include "platform/ps5/input/PS5PadInput.h"
#include "platform/ps5/video/VideoCodecRegistration.h"

#include <algorithm>
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

  RESOLUTION_INFO& desktop = CDisplaySettings::GetInstance().GetResolutionInfo(RES_DESKTOP);
  UpdateDesktopResolution(desktop, "PS5", m_outputWidth, m_outputHeight, m_outputRefresh, 0);
  CDisplaySettings::GetInstance().ClearCustomResolutions();

  CLog::Log(LOGINFO, "CWinSystemPS5: output {}x{} @ {:.2f} Hz", m_outputWidth, m_outputHeight,
            m_outputRefresh);
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
