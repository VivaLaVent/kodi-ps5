/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "VideoOutInfo.h"

#include "utils/StringUtils.h"
#include "utils/log.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
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
// explicit mode API (shapes as documented by the shadPS4 project for the PS4)
void sceVideoOutModeSetAny_(void* mode, uint32_t size);
int sceVideoOutConfigureOutputMode_(int32_t handle, uint32_t reserved, const void* mode,
                                    const void* options, uint32_t modeSize, uint32_t optionsSize);
// option-structure initialisers (shapes unknown: called on an oversized
// zeroed buffer with a size argument; a pointer-only function ignores it)
void sceVideoOutConfigureOptionsInitialize_(void* options, uint32_t size);
void sceVideoOutInitializeOutputOptions(void* options, uint32_t size);
// shape undocumented; called through the two candidate prototypes
int sceVideoOutGetCurrentOutputMode_(int32_t handle, void* mode, uint32_t size);
}

namespace
{
// SceVideoOutMode: size, encoding, range, colorimetry, depth, refresh rate,
// resolution, reserved. ModeSetAny_ sets every field to "any" (0xff bytes).
struct VideoOutMode
{
  uint32_t size;
  uint8_t encoding;
  uint8_t range;
  uint8_t colorimetry;
  uint8_t depth;
  uint64_t refreshRate;
  uint64_t resolution;
  uint8_t reserved[8];
};
static_assert(sizeof(VideoOutMode) == 32 && offsetof(VideoOutMode, refreshRate) == 8);
} // namespace

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

void KODI::PLATFORM::PS5::LogOutputModeSurvey()
{
  const int32_t handle = ps5_opengl_video_out_handle();
  if (handle < 0)
    return;
  // Mode numbers 0-4095 and single bits up to bit 31; read-only queries.
  std::string supported;
  auto probe = [&](uint32_t mode)
  {
    const int r = sceVideoOutIsOutputSupported(handle, mode, nullptr, nullptr, nullptr);
    if (r > 0)
      supported += StringUtils::Format("{}{:#x}{}", supported.empty() ? "" : " ", mode,
                                       r != 1 ? StringUtils::Format("({})", r) : "");
  };
  for (uint32_t mode = 0; mode < 4096; ++mode)
    probe(mode);
  for (uint32_t bit = 12; bit < 32; ++bit)
    probe(1u << bit);
  CLog::Log(LOGINFO, "PS5 video out: output mode survey (0-0xfff, bits 12-31): {}",
            supported.empty() ? "none" : supported);
}

void KODI::PLATFORM::PS5::LogModeStructLayout()
{
  // A 64-byte buffer of zeroes, told it is 32 bytes: the log shows which
  // bytes the system writes (expected: size 0x20, then 0xff "any" to byte 31).
  uint8_t buffer[64] = {};
  sceVideoOutModeSetAny_(buffer, sizeof(VideoOutMode));
  std::string hex;
  for (size_t i = 0; i < sizeof(buffer); ++i)
    hex += StringUtils::Format("{}{:02x}", (i && i % 8 == 0) ? " " : "", buffer[i]);
  CLog::Log(LOGINFO, "PS5 video out: ModeSetAny_(32 bytes) wrote: {}", hex);
}

namespace
{
int g_shapeInitialiser = 0;
uint32_t g_shapeSize = 0;

// options buffer for a call shape (empty: none)
std::vector<uint8_t> BuildOptions(int initialiser, uint32_t size)
{
  std::vector<uint8_t> buffer;
  if (initialiser == 1 || initialiser == 2)
  {
    buffer.assign(256, 0);
    if (initialiser == 1)
      sceVideoOutConfigureOptionsInitialize_(buffer.data(), size);
    else
      sceVideoOutInitializeOutputOptions(buffer.data(), size);
  }
  return buffer;
}
} // namespace

namespace
{
int g_templateVariant = 0;
}

int KODI::PLATFORM::PS5::ReadCurrentMode(int variant, uint8_t* out)
{
  const int32_t handle = ps5_opengl_video_out_handle();
  if (handle < 0)
    return -1;
  std::memset(out, 0, 256);
  if (variant == 1)
    return sceVideoOutGetCurrentOutputMode_(handle, out, 32);
  using VariantB = int (*)(int32_t, void*, void*, uint32_t, uint32_t);
  static uint8_t options[256];
  std::memset(options, 0, sizeof(options));
  // deliberate: the same symbol called through the second candidate prototype
  const auto variantB = reinterpret_cast<VariantB>(
      reinterpret_cast<void*>(&sceVideoOutGetCurrentOutputMode_));
  return variantB(handle, out, options, 32, 16);
}

void KODI::PLATFORM::PS5::SetModeTemplateVariant(int variant)
{
  g_templateVariant = variant;
}

int KODI::PLATFORM::PS5::GetModeTemplateVariant()
{
  return g_templateVariant;
}

void KODI::PLATFORM::PS5::SetModeCallShape(int initialiser, uint32_t size)
{
  g_shapeInitialiser = initialiser;
  g_shapeSize = size;
}

std::pair<int, uint32_t> KODI::PLATFORM::PS5::GetModeCallShape()
{
  return {g_shapeInitialiser, g_shapeSize};
}

int KODI::PLATFORM::PS5::SetOutputRefreshCode(uint64_t field)
{
  const int32_t handle = ps5_opengl_video_out_handle();
  if (handle < 0)
    return -1;
  VideoOutMode mode;
  sceVideoOutModeSetAny_(&mode, sizeof(mode));
  if (g_templateVariant)
  {
    // start from the current mode, as the trial verified
    uint8_t current[256];
    if (ReadCurrentMode(g_templateVariant, current) == 0)
      std::memcpy(&mode, current, sizeof(mode));
  }
  mode.refreshRate = field;
  const std::vector<uint8_t> options = BuildOptions(g_shapeInitialiser, g_shapeSize);
  const int rc = sceVideoOutConfigureOutputMode_(handle, 0, &mode,
                                                 options.empty() ? nullptr : options.data(),
                                                 sizeof(mode), options.empty() ? 0 : g_shapeSize);
  if (rc != 0)
    return rc;
  for (int i = 0; i < 2; ++i)
    if (const int wait = sceVideoOutWaitVblank(handle); wait != 0)
      return wait;
  return 0;
}

namespace
{
std::string Hex(const uint8_t* data, size_t size)
{
  std::string hex;
  for (size_t i = 0; i < size; ++i)
    hex += StringUtils::Format("{}{:02x}", (i && i % 8 == 0) ? " " : "", data[i]);
  return hex;
}

bool BackToSystemMode(int32_t handle)
{
  const int rc = sceVideoOutConfigureOutput(handle, KODI::PLATFORM::PS5::kOutputModeDefault,
                                            nullptr, nullptr, nullptr);
  for (int i = 0; i < 2 && rc == 0; ++i)
    sceVideoOutWaitVblank(handle);
  if (rc != 0)
    CLog::Log(LOGWARNING, "PS5 mode experiment: return to the system mode failed ({:#x})",
              static_cast<uint32_t>(rc));
  return rc == 0;
}
} // namespace

std::vector<uint64_t> KODI::PLATFORM::PS5::ExperimentExplicitRates(
    const std::vector<std::pair<uint64_t, float>>& rates)
{
  std::vector<uint64_t> usable(rates.size(), 0);
  const int32_t handle = ps5_opengl_video_out_handle();
  if (handle < 0)
    return usable;

  struct Shape
  {
    std::string name;
    std::vector<uint8_t> options; // empty: no options (nullptr, size 0)
    uint32_t optionsSize = 0;
    int initialiser = 0;
  };
  std::vector<Shape> shapes;
  shapes.push_back({"no options", {}, 0, 0});
  const char* const names[] = {"", "ConfigureOptionsInitialize_", "InitializeOutputOptions"};
  for (int init = 1; init <= 2; ++init)
  {
    for (uint32_t size : {8u, 16u, 24u, 32u, 48u, 64u})
    {
      std::vector<uint8_t> buffer = BuildOptions(init, size);
      size_t written = 0;
      for (size_t i = 0; i < buffer.size(); ++i)
        if (buffer[i])
          written = i + 1;
      CLog::Log(LOGINFO, "PS5 mode experiment: {}(size {}) wrote {} bytes: {}", names[init], size,
                written, Hex(buffer.data(), std::max<size_t>(written, 8)));
      shapes.push_back({StringUtils::Format("{}({})", names[init], size), buffer, size, init});
    }
  }

  // 1. which call shape does the library accept for an all-"any" mode?
  const Shape* accepted = nullptr;
  for (const auto& shape : shapes)
  {
    VideoOutMode mode;
    sceVideoOutModeSetAny_(&mode, sizeof(mode));
    const int rc = sceVideoOutConfigureOutputMode_(
        handle, 0, &mode, shape.options.empty() ? nullptr : shape.options.data(),
        sizeof(mode), shape.optionsSize);
    CLog::Log(rc == 0 ? LOGINFO : LOGWARNING,
              "PS5 mode experiment: any-mode with {}: {:#x}", shape.name,
              static_cast<uint32_t>(rc));
    if (rc == 0)
    {
      BackToSystemMode(handle);
      if (!accepted)
        accepted = &shape;
    }
  }
  if (!accepted)
  {
    CLog::Log(LOGWARNING, "PS5 mode experiment: no call shape accepted; explicit rates untested");
    return usable;
  }
  CLog::Log(LOGINFO, "PS5 mode experiment: using the call shape '{}'", accepted->name);
  SetModeCallShape(accepted->initialiser, accepted->optionsSize);

  // 2. the refresh field: a rate code, or a mask of codes ("any" is all
  //    bits set)? Controls at the current rate (59.94 Hz, code 3) first, then
  //    each rate in both encodings.
  auto attempt = [&](uint64_t field, float expectHz, const char* what) -> bool
  {
    VideoOutMode mode;
    sceVideoOutModeSetAny_(&mode, sizeof(mode));
    mode.refreshRate = field;
    const int rc = sceVideoOutConfigureOutputMode_(
        handle, 0, &mode, accepted->options.empty() ? nullptr : accepted->options.data(),
        sizeof(mode), accepted->optionsSize);
    float reported = 0.0f;
    if (rc == 0)
    {
      for (int i = 0; i < 2; ++i)
        sceVideoOutWaitVblank(handle);
      reported = QueryRefreshRate();
    }
    const bool ok = rc == 0 && std::abs(reported - expectHz) < 0.1f;
    CLog::Log(ok ? LOGINFO : LOGWARNING,
              "PS5 mode experiment: {:.3f} Hz as {} (field {:#x}): {:#x}, system reports "
              "{:.3f} Hz -> {}",
              expectHz, what, field, static_cast<uint32_t>(rc), reported,
              ok ? "usable" : "not usable");
    if (rc == 0)
      BackToSystemMode(handle);
    return ok;
  };
  attempt(0x3, 59.94f, "value (control)");
  attempt(1ull << 3, 59.94f, "mask (control)");
  for (size_t r = 0; r < rates.size(); ++r)
  {
    const uint64_t code = rates[r].first;
    if (attempt(code, rates[r].second, "value"))
      usable[r] = code;
    else if (attempt(1ull << code, rates[r].second, "mask"))
      usable[r] = 1ull << code;
  }
  return usable;
}

std::vector<uint64_t> KODI::PLATFORM::PS5::ExperimentTemplateRates(
    int variant, const std::vector<std::pair<uint64_t, float>>& rates)
{
  std::vector<uint64_t> fields(rates.size(), 0);
  const int32_t handle = ps5_opengl_video_out_handle();
  if (handle < 0)
    return fields;
  uint8_t current[256];
  const int rc = ReadCurrentMode(variant, current);
  size_t written = 0;
  for (size_t i = 0; i < sizeof(current); ++i)
    if (current[i])
      written = i + 1;
  CLog::Log(rc == 0 ? LOGINFO : LOGWARNING,
            "PS5 mode template: GetCurrentOutputMode_ variant {}: {:#x}, {} bytes: {}", variant,
            static_cast<uint32_t>(rc), written, Hex(current, std::max<size_t>(written, 32)));
  if (rc != 0)
    return fields;

  VideoOutMode base;
  std::memcpy(&base, current, sizeof(base));
  CLog::Log(LOGINFO,
            "PS5 mode template: size {} encoding {:#x} range {:#x} colorimetry {:#x} depth {:#x} "
            "refresh {:#x} resolution {:#x}",
            base.size, base.encoding, base.range, base.colorimetry, base.depth, base.refreshRate,
            base.resolution);
  // the read mode shows the refresh field's encoding: 0x3 (value) or 0x8 (mask)
  const bool mask = base.refreshRate == (1ull << 3);

  const std::vector<uint8_t> options = BuildOptions(g_shapeInitialiser, g_shapeSize);
  auto apply = [&](const VideoOutMode& mode) {
    return sceVideoOutConfigureOutputMode_(handle, 0, &mode,
                                           options.empty() ? nullptr : options.data(),
                                           sizeof(mode), options.empty() ? 0 : g_shapeSize);
  };

  const int control = apply(base);
  CLog::Log(control == 0 ? LOGINFO : LOGWARNING,
            "PS5 mode template: control (current mode unchanged): {:#x}",
            static_cast<uint32_t>(control));
  if (control == 0)
    BackToSystemMode(handle);

  for (size_t r = 0; r < rates.size(); ++r)
  {
    VideoOutMode mode = base;
    mode.refreshRate = mask ? (1ull << rates[r].first) : rates[r].first;
    const int result = apply(mode);
    float reported = 0.0f;
    if (result == 0)
    {
      for (int i = 0; i < 2; ++i)
        sceVideoOutWaitVblank(handle);
      reported = QueryRefreshRate();
    }
    const bool ok = result == 0 && std::abs(reported - rates[r].second) < 0.1f;
    CLog::Log(ok ? LOGINFO : LOGWARNING,
              "PS5 mode template: {:.3f} Hz (field {:#x}): {:#x}, system reports {:.3f} Hz -> {}",
              rates[r].second, mode.refreshRate, static_cast<uint32_t>(result), reported,
              ok ? "usable" : "not usable");
    if (result == 0)
      BackToSystemMode(handle);
    if (ok)
      fields[r] = mode.refreshRate;
  }
  return fields;
}
