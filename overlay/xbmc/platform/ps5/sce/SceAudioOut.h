/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

/*
 * Clean-room declarations for the PS5 audio output library (libSceAudioOut).
 * Behaviour, as observed by the public PS5 SDL backend
 * (github.com/ps5-payload-dev/SDL, src/audio/ps5):
 *   - the port runs at 48 kHz; `len` is frames per sceAudioOutOutput() call
 *     and must be one of 256, 512, 768, 1024, 1280, 1536, 1792, 2048;
 *   - sceAudioOutOutput() blocks until the previously queued block has been
 *     consumed, so it doubles as the pacing clock;
 *   - sceAudioOutOutput(handle, nullptr) waits for the queue to drain
 *     (PS4 SDK semantics; verify on hardware).
 */

#include <cstdint>

extern "C"
{
  int32_t sceAudioOutInit(void);
  int32_t sceAudioOutOpen(int32_t userId,
                          int32_t type,
                          int32_t index,
                          uint32_t len,
                          uint32_t freq,
                          uint32_t param);
  int32_t sceAudioOutOutput(int32_t handle, const void* buffer);
  int32_t sceAudioOutClose(int32_t handle);
}

namespace KODI::PLATFORM::PS5
{

constexpr int32_t AUDIO_OUT_PORT_TYPE_MAIN = 0;
// Open the port on behalf of the system user so no login is required.
constexpr int32_t AUDIO_OUT_USER_ID_SYSTEM = 0xFF;

// `param` values for sceAudioOutOpen(). Stereo variants verified by the SDL
// port; the 8-channel ones follow the PS4 numbering and are unverified.
constexpr uint32_t AUDIO_OUT_FORMAT_S16_MONO = 0;
constexpr uint32_t AUDIO_OUT_FORMAT_S16_STEREO = 1;
constexpr uint32_t AUDIO_OUT_FORMAT_S16_8CH = 2;
constexpr uint32_t AUDIO_OUT_FORMAT_FLOAT_MONO = 3;
constexpr uint32_t AUDIO_OUT_FORMAT_FLOAT_STEREO = 4;
constexpr uint32_t AUDIO_OUT_FORMAT_FLOAT_8CH = 5;

constexpr uint32_t AUDIO_OUT_SAMPLE_RATE = 48000;

} // namespace KODI::PLATFORM::PS5
