/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "windowing/VideoSync.h"

#include <cstdint>

class CWinSystemBase;

/*!
 * Vertical-sync source for Kodi's reference clock on the PS5: polls the
 * video output's vblank counter (read-only; presentation is untouched) and
 * reports each new vblank with its timestamp, like the GBM implementation.
 */
class CVideoSyncPS5 : public CVideoSync
{
public:
  explicit CVideoSyncPS5(CVideoReferenceClock* clock);
  bool Setup() override;
  void Run(CEvent& stopEvent) override;
  void Cleanup() override;
  float GetFps() override;
  void RefreshChanged() override;

private:
  CWinSystemBase* m_winSystem;
  uint64_t m_lastCount = 0;
  bool m_abort = false;
};
