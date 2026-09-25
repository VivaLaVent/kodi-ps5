/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "VideoSyncPS5.h"

#include "ServiceBroker.h"
#include "cores/VideoPlayer/VideoReferenceClock.h"
#include "platform/ps5/VideoOutInfo.h"
#include "threads/Event.h"
#include "utils/TimeUtils.h"
#include "utils/log.h"
#include "windowing/GraphicContext.h"
#include "windowing/WinSystem.h"

#include <chrono>
#include <thread>

extern "C" uint64_t sceKernelGetProcessTime(void);

using namespace KODI::PLATFORM::PS5;

CVideoSyncPS5::CVideoSyncPS5(CVideoReferenceClock* clock)
  : CVideoSync(clock), m_winSystem(CServiceBroker::GetWinSystem())
{
}

bool CVideoSyncPS5::Setup()
{
  m_abort = false;
  uint64_t count = 0, when = 0;
  if (!QueryVblank(count, when))
  {
    CLog::Log(LOGWARNING, "CVideoSyncPS5: vblank status unavailable, no display sync");
    return false;
  }
  m_lastCount = count;
  CLog::Log(LOGINFO, "CVideoSyncPS5: vblank counter at {}, display {:.3f} Hz", count, GetFps());
  return true;
}

void CVideoSyncPS5::Run(CEvent& stopEvent)
{
  while (!stopEvent.Signaled() && !m_abort)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    uint64_t count = 0, when = 0;
    if (!QueryVblank(count, when))
    {
      CLog::Log(LOGWARNING, "CVideoSyncPS5: vblank status failed, stopping display sync");
      break;
    }
    if (count == m_lastCount)
      continue;

    // Timestamp of the vblank itself (process time, microseconds), so the
    // 1 ms polling interval does not add jitter; fall back to "now" if it
    // does not look like a recent process-time value.
    int64_t hostTime = CurrentHostCounter();
    const uint64_t now = sceKernelGetProcessTime();
    if (when != 0 && when <= now && now - when < 100000)
      hostTime -= static_cast<int64_t>((now - when) * CurrentHostFrequency() / 1000000);

    m_refClock->UpdateClock(static_cast<int>(count - m_lastCount), static_cast<uint64_t>(hostTime));
    m_lastCount = count;
  }
}

void CVideoSyncPS5::Cleanup()
{
}

float CVideoSyncPS5::GetFps()
{
  m_fps = m_winSystem ? m_winSystem->GetGfxContext().GetFPS() : 60.0f;
  return m_fps;
}

void CVideoSyncPS5::RefreshChanged()
{
  // the output mode changed (e.g. 59.94 <-> 119.88 Hz): restart the clock
  if (m_winSystem && m_fps != m_winSystem->GetGfxContext().GetFPS())
    m_abort = true;
}
