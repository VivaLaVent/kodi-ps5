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
#include <dlfcn.h>
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
int ps5_opengl_video_out_handle(void);
int sceVideoOutGetVblankStatus(int32_t handle, void* status);
int sceVideoOutConfigureOutput(int32_t handle, uint32_t mode, const void*, const void*,
                               const void*);
int sceVideoOutWaitVblank(int32_t handle);
}

bool KODI::PLATFORM::PS5::LogVideoOutInfo()
{
  // Exported by our addition to the GL driver (patches/ps5-opengl).
  const int32_t handle = ps5_opengl_video_out_handle();
  if (handle < 0)
    return false;
  ResolutionStatus resolution{};
  const int resRc = sceVideoOutGetResolutionStatus(handle, &resolution);
  if (resRc != 0)
  {
    CLog::Log(LOGWARNING, "PS5 video out: handle {} resolution status failed ({:#x})", handle,
              static_cast<uint32_t>(resRc));
    return true;
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
  return true;
}

int KODI::PLATFORM::PS5::VideoOutHandle()
{
  return ps5_opengl_video_out_handle();
}

namespace
{
// libSceVideoOut refresh-rate codes (0x3 observed for a 59.94 Hz output)
float RefreshFromId(uint64_t id)
{
  switch (id)
  {
    case 0x1:
      return 23.976f;
    case 0x2:
      return 50.0f;
    case 0x3:
      return 59.94f;
    case 0x4:
      return 29.97f;
    case 0xd:
      return 119.88f;
    case 0x23:
      return 89.91f;
    default:
      return 0.0f;
  }
}
} // namespace

float KODI::PLATFORM::PS5::QueryRefreshRate()
{
  const int32_t handle = ps5_opengl_video_out_handle();
  if (handle < 0)
    return 0.0f;
  ResolutionStatus resolution{};
  if (sceVideoOutGetResolutionStatus(handle, &resolution) != 0)
    return 0.0f;
  return RefreshFromId(resolution.refreshRate);
}

bool KODI::PLATFORM::PS5::QueryVblank(uint64_t& count, uint64_t& processTimeUs)
{
  const int32_t handle = ps5_opengl_video_out_handle();
  if (handle < 0)
    return false;
  // SceVideoOutVblankStatus: count, processTime, tsc, reserved, flags (40
  // bytes); a larger zeroed buffer keeps us safe if the layout grew.
  uint64_t status[8] = {};
  if (sceVideoOutGetVblankStatus(handle, status) != 0)
    return false;
  count = status[0];
  processTimeUs = status[1];
  return true;
}

bool KODI::PLATFORM::PS5::QuerySystemResolution(unsigned& width, unsigned& height)
{
  const int32_t handle = ps5_opengl_video_out_handle();
  if (handle < 0)
    return false;
  ResolutionStatus resolution{};
  if (sceVideoOutGetResolutionStatus(handle, &resolution) != 0 || !resolution.fullWidth)
    return false;
  width = resolution.fullWidth;
  height = resolution.fullHeight;
  return true;
}

bool KODI::PLATFORM::PS5::IsHighRefreshSupported()
{
  const int32_t handle = ps5_opengl_video_out_handle();
  return handle >= 0 &&
         sceVideoOutIsOutputSupported(handle, kOutputModeHighRefresh, nullptr, nullptr, nullptr) >
             0;
}

int KODI::PLATFORM::PS5::SetOutputMode(uint32_t mode)
{
  const int32_t handle = ps5_opengl_video_out_handle();
  if (handle < 0)
    return -1;
  const int rc = sceVideoOutConfigureOutput(handle, mode, nullptr, nullptr, nullptr);
  if (rc != 0)
    return rc;
  // as the GL driver does after a mode change: let two vblanks pass
  for (int i = 0; i < 2; ++i)
    if (const int wait = sceVideoOutWaitVblank(handle); wait != 0)
      return wait;
  return 0;
}

int KODI::PLATFORM::PS5::VrrUnpegFromFixedRate()
{
  const int32_t handle = ps5_opengl_video_out_handle();
  if (handle < 0)
    return -1;
  using UnpegFn = int (*)(int32_t);
  static UnpegFn unpeg = []() -> UnpegFn
  {
    // the GL driver loads its video out functions the same way
    void* module = dlopen("libSceVideoOut.sprx", RTLD_NOW | RTLD_LOCAL);
    void* sym = module ? dlsym(module, "sceVideoOutVrrUnpegFromFixedRate") : nullptr;
    CLog::Log(sym ? LOGINFO : LOGWARNING, "PS5 video out: VRR unpeg function {}",
              sym ? "found" : "not available");
    return reinterpret_cast<UnpegFn>(sym);
  }();
  if (!unpeg)
    return -2;
  return unpeg(handle);
}
