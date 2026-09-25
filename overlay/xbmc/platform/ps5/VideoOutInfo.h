/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <cstdint>

namespace KODI::PLATFORM::PS5
{
// Read-only report of the system's video output (resolution, refresh rate)
// and of the output modes it accepts for this title, written to the log.
// Needs the GL driver's video out handle, which exists once the first frame
// has been presented; returns false (nothing logged) before that.
bool LogVideoOutInfo();

// The GL driver's video out handle (-1 before the first presented frame).
int VideoOutHandle();

// The system's output size (false if unknown).
bool QuerySystemResolution(unsigned& width, unsigned& height);

// Current output refresh rate in Hz from the system (0 if unknown).
float QueryRefreshRate();

// Vblank counter and the process time (microseconds) of the latest vblank.
bool QueryVblank(uint64_t& count, uint64_t& processTimeUs);
} // namespace KODI::PLATFORM::PS5
