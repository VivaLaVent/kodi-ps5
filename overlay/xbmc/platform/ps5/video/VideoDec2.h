/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

/*
 * The PS5's hardware video decoder (system library libSceVideodec2).
 *
 * The structure layouts and call sequence follow what is known to work on
 * hardware (interface facts, as used by ProsperoLight). Decoded pictures come
 * back as linear NV12 in CPU-visible direct memory: luma rows of `pitch`
 * bytes, `height` rows (the coded height, e.g. 1088), then the interleaved
 * chroma plane with the same pitch.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KODI::PLATFORM::PS5
{

enum class VideoDec2Codec
{
  H264,
  HEVC,
};

struct VideoDec2Picture
{
  const uint8_t* data = nullptr; // luma; chroma at data + pitch * height
  uint32_t width = 0;  // decoded width
  uint32_t height = 0; // coded height (chroma offset in rows)
  uint32_t pitch = 0;  // bytes per row, both planes
};

class CVideoDec2
{
public:
  CVideoDec2() = default;
  ~CVideoDec2();
  CVideoDec2(const CVideoDec2&) = delete;
  CVideoDec2& operator=(const CVideoDec2&) = delete;

  // Sets up memory, compute queue and decoder for streams up to width x height.
  bool Open(VideoDec2Codec codec, int width, int height, std::string& error);
  void Close();

  // Decode one access unit (Annex-B). Returns false on a decoder error.
  // gotPicture tells whether *picture holds a decoded frame; its data stays
  // valid until the frame buffer is reused (kFrameBuffers decodes later).
  bool Decode(const uint8_t* au, size_t size, bool& gotPicture, VideoDec2Picture* picture,
              std::string& error);
  // Squeeze out a held-back picture at end of stream; false when none is left.
  bool Flush(VideoDec2Picture* picture);
  // After a seek: drop all state (next input must start with a keyframe).
  void Reset();

  size_t MaxAccessUnit() const { return m_inputSize; }

private:
  struct DirectMemory
  {
    void* address = nullptr;
    size_t size = 0;
    int64_t start = -1;
  };
  static bool AllocateDirect(size_t size, int protection, DirectMemory& out, std::string& error);
  static void FreeDirect(DirectMemory& mem);
  bool ToPicture(const void* output, VideoDec2Picture* picture) const;
  uint8_t* NextFrameBuffer();

  static constexpr unsigned kFrameBuffers = 20; // DPB (up to 16) + in flight + margin

  bool m_moduleLoaded = false;
  void* m_computeQueue = nullptr;
  void* m_decoder = nullptr;
  DirectMemory m_computeMemory;
  DirectMemory m_gpuMemory;
  DirectMemory m_cpuGpuMemory;
  DirectMemory m_inputMemory;
  DirectMemory m_frameMemory;
  void* m_cpuWorkspace = nullptr;
  size_t m_cpuWorkspaceSize = 0;
  size_t m_inputSize = 0;
  size_t m_frameSize = 0;
  unsigned m_nextFrame = 0;
  uint32_t m_codecType = 0;
};

} // namespace KODI::PLATFORM::PS5
