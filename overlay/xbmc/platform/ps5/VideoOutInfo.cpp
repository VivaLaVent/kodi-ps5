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
#include <dlfcn.h>

namespace
{
// libSceVideoOut resolution status (48 bytes; layout as used on hardware)
struct ResolutionStatus
{
  uint32_t fullWidth, fullHeight, paneWidth, paneHeight;
  uint64_t refreshRate;
  float screenInches;
  uint32_t reserved[4];
};
static_assert(sizeof(ResolutionStatus) == 48 && offsetof(ResolutionStatus, refreshRate) == 16);

// SceVideoOutRefreshRate codes (0x3 is what the system reports at 59.94 Hz)
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

extern "C"
{
int sceVideoOutGetResolutionStatus(int32_t handle, ResolutionStatus* status);
int sceVideoOutIsOutputSupported(int32_t handle, uint32_t mode, const void*, const void*,
                                 const void*);
int sceVideoOutConfigureOutput(int32_t handle, uint32_t mode, const void*, const void*,
                               const void*);
int sceVideoOutWaitVblank(int32_t handle);
int sceVideoOutGetVblankStatus(int32_t handle, void* status);
int ps5_opengl_video_out_handle(void);
}

int KODI::PLATFORM::PS5::VideoOutHandle()
{
  return ps5_opengl_video_out_handle();
}

bool KODI::PLATFORM::PS5::LogVideoOutInfo()
{
  const int32_t handle = ps5_opengl_video_out_handle();
  if (handle < 0)
    return false;
  ResolutionStatus status{};
  const int rc = sceVideoOutGetResolutionStatus(handle, &status);
  if (rc != 0)
    CLog::Log(LOGWARNING, "PS5 video out: resolution status failed ({:#x})",
              static_cast<uint32_t>(rc));
  else
    CLog::Log(LOGINFO, "PS5 video out: system output {}x{} at {:.3f} Hz (refresh code {:#x})",
              status.fullWidth, status.fullHeight, RefreshFromId(status.refreshRate),
              status.refreshRate);
  return true;
}

bool KODI::PLATFORM::PS5::QuerySystemResolution(unsigned& width, unsigned& height)
{
  const int32_t handle = ps5_opengl_video_out_handle();
  ResolutionStatus status{};
  if (handle < 0 || sceVideoOutGetResolutionStatus(handle, &status) != 0 || !status.fullWidth)
    return false;
  width = status.fullWidth;
  height = status.fullHeight;
  return true;
}

float KODI::PLATFORM::PS5::QueryRefreshRate()
{
  const int32_t handle = ps5_opengl_video_out_handle();
  ResolutionStatus status{};
  if (handle < 0 || sceVideoOutGetResolutionStatus(handle, &status) != 0)
    return 0.0f;
  return RefreshFromId(status.refreshRate);
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

namespace
{
using UnpegFn = int (*)(int32_t);

UnpegFn LookUpUnpeg()
{
  static const UnpegFn fn = []() -> UnpegFn
  {
    // the GL driver resolves its video out functions the same way
    void* module = dlopen("libSceVideoOut.sprx", RTLD_NOW | RTLD_LOCAL);
    if (!module)
    {
      const char* err = dlerror();
      CLog::Log(LOGWARNING, "PS5 VRR: dlopen(libSceVideoOut.sprx) failed ({}): VRR unavailable",
                err ? err : "no error text");
      return nullptr;
    }
    void* sym = dlsym(module, "sceVideoOutVrrUnpegFromFixedRate");
    if (!sym)
    {
      // control: a function every build uses, resolved the same way
      void* control = dlsym(module, "sceVideoOutWaitVblank");
      CLog::Log(LOGWARNING,
                "PS5 VRR: sceVideoOutVrrUnpegFromFixedRate not found (control symbol {}): VRR "
                "unavailable",
                control ? "found, so this firmware lacks the function"
                        : "also missing, so the lookup itself fails");
      return nullptr;
    }
    CLog::Log(LOGINFO, "PS5 VRR: sceVideoOutVrrUnpegFromFixedRate available");
    return reinterpret_cast<UnpegFn>(sym);
  }();
  return fn;
}
} // namespace

bool KODI::PLATFORM::PS5::IsVrrUnpegAvailable()
{
  return LookUpUnpeg() != nullptr;
}

int KODI::PLATFORM::PS5::VrrUnpegFromFixedRate()
{
  const int32_t handle = ps5_opengl_video_out_handle();
  if (handle < 0)
    return -1;
  const UnpegFn unpeg = LookUpUnpeg();
  return unpeg ? unpeg(handle) : -2;
}
