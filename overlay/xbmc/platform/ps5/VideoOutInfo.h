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
// Video output access for the window system, through the GL driver's video
// out handle (exported by our ps5-opengl addition; -1 until the driver opens
// the output with the first presented frame).
int VideoOutHandle();

// One line to the log: system output size and refresh rate. Returns false
// before the output exists.
bool LogVideoOutInfo();

// The system's output size (false if unknown).
bool QuerySystemResolution(unsigned& width, unsigned& height);

// Current output refresh rate in Hz (0 if unknown).
float QueryRefreshRate();

// Vblank counter and the process time (microseconds) of the latest vblank.
bool QueryVblank(uint64_t& count, uint64_t& processTimeUs);

// Output presets a title may request (sceVideoOutConfigureOutput): the
// system's own mode, and the high-refresh preset that the PS5 turns into
// VRR (pegged at 120 Hz) when its VRR setting is on.
constexpr uint32_t kOutputModeDefault = 1;
constexpr uint32_t kOutputModeHighRefresh = 15;

// Whether the system offers the high-refresh preset to this title.
bool IsHighRefreshSupported();

// Switch the output preset; waits for the output to settle. 0 on success.
int SetOutputMode(uint32_t mode);

// Whether sceVideoOutVrrUnpegFromFixedRate exists on this firmware (looked
// up at runtime: the SDK's link stub lacks it). Logs the result once.
bool IsVrrUnpegAvailable();

// Release a VRR output from its fixed 120 Hz peg, so the display follows the
// title's presentation. 0 on success; negative if unavailable.
int VrrUnpegFromFixedRate();
} // namespace KODI::PLATFORM::PS5
