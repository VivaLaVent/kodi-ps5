/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "VideoOutInfo.h"

#include "utils/log.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace
{
// libSceVideoOut status structures (48 bytes each; layout as used on hardware)
struct ResolutionStatus
{
  uint32_t fullWidth, fullHeight, paneWidth, paneHeight;
  uint64_t refreshRate;
  float screenInches;
  uint32_t reserved[4];
};
struct OutputStatus
{
  uint32_t resolutionClass, outputClass;
  uint64_t refreshRate, flags;
  uint32_t mode, reserved[5];
};
static_assert(sizeof(ResolutionStatus) == 48 && offsetof(ResolutionStatus, refreshRate) == 16);
static_assert(sizeof(OutputStatus) == 48 && offsetof(OutputStatus, refreshRate) == 8);
} // namespace

extern "C"
{
int sceVideoOutGetResolutionStatus(int32_t handle, ResolutionStatus* status);
int sceVideoOutGetOutputStatus(int32_t handle, OutputStatus* status);
int sceVideoOutIsOutputSupported(int32_t handle, uint32_t mode, const void*, const void*,
                                 const void*);
}

void KODI::PLATFORM::PS5::LogVideoOutInfo()
{
  // The GL driver owns the video output handle and does not export it.
  // Handles are small integers: find ours with a read-only status query.
  int32_t handle = -1;
  ResolutionStatus resolution{};
  for (int32_t candidate = 0; candidate < 32; ++candidate)
  {
    ResolutionStatus probe{};
    if (sceVideoOutGetResolutionStatus(candidate, &probe) == 0 && probe.fullWidth > 0)
    {
      handle = candidate;
      resolution = probe;
      break;
    }
  }
  if (handle < 0)
  {
    CLog::Log(LOGWARNING, "PS5 video out: no readable video output handle found");
    return;
  }

  CLog::Log(LOGINFO,
            "PS5 video out: handle {}, system output {}x{} (pane {}x{}), refresh id {:#x}, "
            "screen {:.1f} in",
            handle, resolution.fullWidth, resolution.fullHeight, resolution.paneWidth,
            resolution.paneHeight, resolution.refreshRate, resolution.screenInches);

  OutputStatus output{};
  const int rc = sceVideoOutGetOutputStatus(handle, &output);
  if (rc == 0)
    CLog::Log(LOGINFO,
              "PS5 video out: output status resolution class {}, output class {}, refresh id "
              "{:#x}, flags {:#x}, mode {}",
              output.resolutionClass, output.outputClass, output.refreshRate, output.flags,
              output.mode);
  else
    CLog::Log(LOGINFO, "PS5 video out: output status unavailable ({:#x})",
              static_cast<uint32_t>(rc));

  // Which output modes would the system accept for this title? (15 is the
  // 120 Hz preset the GL driver uses when built for high refresh, 1 its
  // default.) Queries only - nothing is changed.
  std::string supported;
  for (uint32_t mode = 0; mode < 64; ++mode)
  {
    const int r = sceVideoOutIsOutputSupported(handle, mode, nullptr, nullptr, nullptr);
    if (r > 0)
      supported += (supported.empty() ? "" : " ") + std::to_string(mode) +
                   (r != 1 ? "(" + std::to_string(r) + ")" : "");
  }
  CLog::Log(LOGINFO, "PS5 video out: supported output modes: {}",
            supported.empty() ? "none reported" : supported);
}
