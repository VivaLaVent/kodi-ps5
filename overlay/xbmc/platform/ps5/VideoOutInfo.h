/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

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

// Output modes accepted by sceVideoOutConfigureOutput: the system default
// (the rate the PS5 is set to, e.g. 59.94 Hz) and the 120 Hz preset.
constexpr uint32_t kOutputModeDefault = 1;
constexpr uint32_t kOutputModeHighRefresh = 15;

// Refresh-rate codes of the explicit mode API (SceVideoOutRefreshRate; 0x3,
// 59.94 Hz, is what the system reports for its default mode).
constexpr uint64_t kRefreshCode23_98 = 0x1;
constexpr uint64_t kRefreshCode50 = 0x2;

// Explicit refresh rate through the mode-structure API (ModeSetAny_ +
// ConfigureOutputMode_): every field "any" except the refresh-rate field,
// which is set to `field` as the trial verified it (a rate code, or a mask of
// codes). Waits for the output to settle. 0 on success.
int SetOutputRefreshCode(uint64_t field);

// Diagnostics: what ModeSetAny_ writes into a buffer of ours (the mode
// structure's layout on this firmware), to the log.
void LogModeStructLayout();

// kodi-probe-modes experiment: find a ConfigureOutputMode_ call shape the
// system accepts (baseline "any" mode, with and without an options structure
// from either initialiser), then try the given refresh codes with it. Every
// accepted call is followed by a return to the system mode. Returns, per
// code, whether the system then reported that rate.
// Returns, per rate, the refresh-field value that produced that rate (0 if
// none did). Both encodings are tried: the code itself and 1 << code.
std::vector<uint64_t> ExperimentExplicitRates(
    const std::vector<std::pair<uint64_t, float>>& rates);

// The ConfigureOutputMode_ call shape used by SetOutputRefreshCode: options
// initialiser 0 = none, 1 = ConfigureOptionsInitialize_, 2 =
// InitializeOutputOptions, with its size argument. The experiment sets it to
// the shape it found; the window system saves and restores it.
void SetModeCallShape(int initialiser, uint32_t size);
std::pair<int, uint32_t> GetModeCallShape();

// Whether the system accepts the 120 Hz preset for this title.
bool IsHighRefreshSupported();

// Switch the output mode; waits for the output to settle. 0 on success.
int SetOutputMode(uint32_t mode);

// Diagnostics ("kodi-probe-modes" switch): which output mode numbers the
// system accepts beyond 0-63 (read-only IsOutputSupported queries).
void LogOutputModeSurvey();

// Vblank counter and the process time (microseconds) of the latest vblank.
bool QueryVblank(uint64_t& count, uint64_t& processTimeUs);
} // namespace KODI::PLATFORM::PS5
