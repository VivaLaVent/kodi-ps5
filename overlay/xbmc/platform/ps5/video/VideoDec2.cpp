/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "VideoDec2.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <sys/mman.h>

// ---------------------------------------------------------------------------
// libSceVideodec2 / libkernel / libSceSysmodule interface (clean-room)
// ---------------------------------------------------------------------------
extern "C"
{
struct sceVideodec2DecoderConfig
{
  uint64_t size;
  uint32_t resourceType, codecType, profile, maxLevel;
  int32_t maxWidth, maxHeight, maxDpbFrames;
  uint32_t pipelineDepth;
  uint64_t computeQueue, cpuAffinity;
  int32_t cpuPriority;
  uint32_t optimizeProgressive, checkMemoryType, reserved;
};

struct sceVideodec2DecoderMemory
{
  uint64_t size, cpuSize;
  void* cpu;
  uint64_t gpuSize;
  void* gpu;
  uint64_t cpuGpuSize;
  void* cpuGpu;
  uint64_t maxFrameSize;
  uint32_t frameAlignment, reserved;
};

struct sceVideodec2ComputeConfig
{
  uint64_t size;
  uint16_t pipeId, queueId;
  uint8_t checkMemoryType, reserved0;
  uint16_t reserved1;
};

struct sceVideodec2ComputeMemory
{
  uint64_t size, cpuGpuSize;
  void* cpuGpu;
};

struct sceVideodec2Input
{
  uint64_t size;
  void* au;
  uint64_t auSize, pts, dts, attached;
};

struct sceVideodec2Frame
{
  uint64_t size;
  void* buffer;
  uint64_t bufferSize;
  uint32_t accepted, reserved;
};

struct sceVideodec2Output
{
  uint64_t size;
  uint8_t valid, error, pictureCount, padding;
  uint32_t codec, width, pitch, height, reserved;
  void* buffer;
  uint64_t bufferSize;
  uint32_t frameFormat, pitchBytes;
};

int32_t sceVideodec2QueryComputeMemoryInfo(sceVideodec2ComputeMemory* memory);
int32_t sceVideodec2AllocateComputeQueue(const sceVideodec2ComputeConfig* config,
                                         const sceVideodec2ComputeMemory* memory, void** queue);
int32_t sceVideodec2ReleaseComputeQueue(void* queue);
int32_t sceVideodec2QueryDecoderMemoryInfo(const sceVideodec2DecoderConfig* config,
                                           sceVideodec2DecoderMemory* memory);
int32_t sceVideodec2CreateDecoder(const sceVideodec2DecoderConfig* config,
                                  const sceVideodec2DecoderMemory* memory, void** decoder);
int32_t sceVideodec2DeleteDecoder(void* decoder);
int32_t sceVideodec2Decode(void* decoder, sceVideodec2Input* input, sceVideodec2Frame* frame,
                           sceVideodec2Output* output);
int32_t sceVideodec2Flush(void* decoder, sceVideodec2Frame* frame, sceVideodec2Output* output);
int32_t sceVideodec2Reset(void* decoder);

int32_t sceSysmoduleLoadModule(uint32_t id);

int64_t sceKernelGetDirectMemorySize(void);
int sceKernelAllocateDirectMemory(int64_t searchStart, int64_t searchEnd, size_t length,
                                  size_t alignment, int memoryType, int64_t* start);
int sceKernelMapDirectMemory(void** address, size_t length, int protection, int flags,
                             int64_t start, size_t alignment);
int sceKernelReleaseDirectMemory(int64_t start, size_t length);
int sceKernelMapNamedFlexibleMemory(void** address, size_t length, int protection, int flags,
                                    const char* name);
int sceKernelDebugOutText(int channel, const char* text);
}

namespace
{
constexpr uint32_t kSysmoduleVideodec2 = 207;
constexpr uint32_t kCodecH264 = 1;
constexpr uint32_t kCodecHEVC = 0x000ee049;
constexpr int kMemoryType = 12;       // direct memory type used on hardware
constexpr int kProtGpu = 0x32;        // CPU read/write + GPU read/write
constexpr int kProtCpuGpu = 0x33;
constexpr int kProtCpu = 0x03;
constexpr size_t kInputSlot = 8 * 1024 * 1024;

size_t Align16k(size_t v)
{
  return (v + 0x3fff) & ~static_cast<size_t>(0x3fff);
}

void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Log(const char* fmt, ...)
{
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  sceKernelDebugOutText(0, buf);
}

std::string Hex(const char* what, int32_t rc)
{
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%s failed (0x%08x)", what, static_cast<uint32_t>(rc));
  return buf;
}

// The decoder writes frames behind the CPU's back: drop any cached copy of
// the lines before reading them.
void InvalidateForRead(const void* data, size_t size)
{
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t off = 0; off < size; off += 64)
    __builtin_ia32_clflush(p + off);
  __builtin_ia32_mfence();
}
} // namespace

namespace KODI::PLATFORM::PS5
{

CVideoDec2::~CVideoDec2()
{
  Close();
}

bool CVideoDec2::AllocateDirect(size_t size, int protection, DirectMemory& out, std::string& error)
{
  int64_t start = -1;
  int rc = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), size, 0x4000,
                                         kMemoryType, &start);
  if (rc != 0)
  {
    error = Hex("sceKernelAllocateDirectMemory", rc);
    return false;
  }
  void* address = nullptr;
  rc = sceKernelMapDirectMemory(&address, size, protection, 0, start, 0x4000);
  if (rc != 0 || !address)
  {
    sceKernelReleaseDirectMemory(start, size);
    error = Hex("sceKernelMapDirectMemory", rc);
    return false;
  }
  out.address = address;
  out.size = size;
  out.start = start;
  return true;
}

void CVideoDec2::FreeDirect(DirectMemory& mem)
{
  if (mem.address)
    munmap(mem.address, mem.size);
  if (mem.start >= 0)
    sceKernelReleaseDirectMemory(mem.start, mem.size);
  mem = DirectMemory{};
}

bool CVideoDec2::Open(VideoDec2Codec codec, int width, int height, std::string& error)
{
  Close();
  const bool uhd = width > 1920 || height > 1088;
  m_codecType = codec == VideoDec2Codec::H264 ? kCodecH264 : kCodecHEVC;

  int32_t rc = sceSysmoduleLoadModule(kSysmoduleVideodec2);
  if (rc < 0)
  {
    error = Hex("loading the video decoder module", rc);
    return false;
  }
  m_moduleLoaded = true;

  // compute queue the decoder runs its GPU work on
  sceVideodec2ComputeMemory compute{};
  compute.size = sizeof(compute);
  rc = sceVideodec2QueryComputeMemoryInfo(&compute);
  if (rc != 0)
  {
    error = Hex("sceVideodec2QueryComputeMemoryInfo", rc);
    return false;
  }
  if (!AllocateDirect(Align16k(compute.cpuGpuSize), kProtCpuGpu, m_computeMemory, error))
    return false;
  compute.cpuGpu = m_computeMemory.address;
  compute.cpuGpuSize = m_computeMemory.size;
  sceVideodec2ComputeConfig computeConfig{};
  computeConfig.size = sizeof(computeConfig);
  rc = sceVideodec2AllocateComputeQueue(&computeConfig, &compute, &m_computeQueue);
  if (rc != 0)
  {
    m_computeQueue = nullptr;
    error = Hex("sceVideodec2AllocateComputeQueue", rc);
    return false;
  }

  // decoder: sized for 1080p or 2160p streams, with a full-size DPB for files
  sceVideodec2DecoderConfig config{};
  config.size = sizeof(config);
  config.resourceType = 1;
  config.codecType = m_codecType;
  if (codec == VideoDec2Codec::H264)
  {
    config.profile = 100; // High (covers Baseline/Main)
    config.maxLevel = uhd ? 52 : 51;
  }
  else
  {
    config.profile = 1; // Main
    config.maxLevel = uhd ? 153 : 123;
  }
  config.maxWidth = uhd ? 3840 : 1920;
  config.maxHeight = uhd ? 2176 : 1088;
  config.maxDpbFrames = 16;
  config.pipelineDepth = 1;
  config.computeQueue = reinterpret_cast<uint64_t>(m_computeQueue);
  config.cpuAffinity = 0x3f;
  config.cpuPriority = 700;
  config.optimizeProgressive = 1;

  sceVideodec2DecoderMemory memory{};
  memory.size = sizeof(memory);
  rc = sceVideodec2QueryDecoderMemoryInfo(&config, &memory);
  if (rc != 0)
  {
    error = Hex("sceVideodec2QueryDecoderMemoryInfo", rc);
    return false;
  }
  Log("[kodi-ps5] videodec2: %s %dx%d needs cpu=%llx gpu=%llx shared=%llx frame=%llx\n",
      codec == VideoDec2Codec::H264 ? "H.264" : "HEVC", config.maxWidth, config.maxHeight,
      static_cast<unsigned long long>(memory.cpuSize),
      static_cast<unsigned long long>(memory.gpuSize),
      static_cast<unsigned long long>(memory.cpuGpuSize),
      static_cast<unsigned long long>(memory.maxFrameSize));

  m_cpuWorkspaceSize = Align16k(memory.cpuSize);
  if (m_cpuWorkspaceSize)
  {
    rc = sceKernelMapNamedFlexibleMemory(&m_cpuWorkspace, m_cpuWorkspaceSize, kProtCpu, 0,
                                         "KodiVdecCpu");
    if (rc != 0)
    {
      m_cpuWorkspace = nullptr;
      error = Hex("mapping the decoder's CPU workspace", rc);
      return false;
    }
  }
  memory.cpu = m_cpuWorkspace;
  memory.cpuSize = m_cpuWorkspaceSize;

  if (!AllocateDirect(Align16k(memory.gpuSize), kProtGpu, m_gpuMemory, error))
    return false;
  memory.gpu = m_gpuMemory.address;
  memory.gpuSize = m_gpuMemory.size;
  if (memory.cpuGpuSize)
  {
    if (!AllocateDirect(Align16k(memory.cpuGpuSize), kProtCpuGpu, m_cpuGpuMemory, error))
      return false;
    memory.cpuGpu = m_cpuGpuMemory.address;
    memory.cpuGpuSize = m_cpuGpuMemory.size;
  }

  m_inputSize = kInputSlot;
  if (!AllocateDirect(m_inputSize, kProtGpu, m_inputMemory, error))
    return false;
  m_frameSize = Align16k(memory.maxFrameSize);
  if (!AllocateDirect(m_frameSize * kFrameBuffers, kProtGpu, m_frameMemory, error))
    return false;

  rc = sceVideodec2CreateDecoder(&config, &memory, &m_decoder);
  if (rc != 0)
  {
    m_decoder = nullptr;
    error = Hex("sceVideodec2CreateDecoder", rc);
    return false;
  }
  rc = sceVideodec2Reset(m_decoder);
  if (rc != 0)
  {
    error = Hex("sceVideodec2Reset", rc);
    return false;
  }
  Log("[kodi-ps5] videodec2: decoder ready (%u frame buffers of %zu KiB)\n", kFrameBuffers,
      m_frameSize / 1024);
  return true;
}

void CVideoDec2::Close()
{
  if (m_decoder)
    sceVideodec2DeleteDecoder(m_decoder);
  m_decoder = nullptr;
  if (m_computeQueue)
    sceVideodec2ReleaseComputeQueue(m_computeQueue);
  m_computeQueue = nullptr;
  FreeDirect(m_frameMemory);
  FreeDirect(m_inputMemory);
  FreeDirect(m_cpuGpuMemory);
  FreeDirect(m_gpuMemory);
  FreeDirect(m_computeMemory);
  if (m_cpuWorkspace)
    munmap(m_cpuWorkspace, m_cpuWorkspaceSize);
  m_cpuWorkspace = nullptr;
  m_cpuWorkspaceSize = 0;
  m_nextFrame = 0;
}

uint8_t* CVideoDec2::NextFrameBuffer()
{
  uint8_t* buffer = static_cast<uint8_t*>(m_frameMemory.address) + m_nextFrame * m_frameSize;
  m_nextFrame = (m_nextFrame + 1) % kFrameBuffers;
  return buffer;
}

bool CVideoDec2::ToPicture(const void* out, VideoDec2Picture* picture) const
{
  const auto* output = static_cast<const sceVideodec2Output*>(out);
  if (!output->valid || output->error || !output->buffer || output->pitch == 0)
    return false;
  const auto* base = static_cast<const uint8_t*>(m_frameMemory.address);
  const auto* buf = static_cast<const uint8_t*>(output->buffer);
  if (buf < base || buf >= base + m_frameSize * kFrameBuffers)
    return false; // not one of ours
  picture->data = buf;
  picture->width = output->width;
  picture->height = output->height;
  picture->pitch = output->pitch;
  InvalidateForRead(buf, static_cast<size_t>(output->pitch) * output->height * 3 / 2);
  return true;
}

bool CVideoDec2::Decode(const uint8_t* au, size_t size, bool& gotPicture,
                        VideoDec2Picture* picture, std::string& error)
{
  gotPicture = false;
  if (!m_decoder)
  {
    error = "decoder not open";
    return false;
  }
  if (size == 0 || size > m_inputSize)
  {
    error = "access unit too large";
    return false;
  }
  std::memcpy(m_inputMemory.address, au, size);

  sceVideodec2Input input{};
  input.size = sizeof(input);
  input.au = m_inputMemory.address;
  input.auSize = size;
  input.pts = 0;
  input.dts = UINT64_MAX;
  sceVideodec2Frame frame{};
  frame.size = sizeof(frame);
  frame.buffer = NextFrameBuffer();
  frame.bufferSize = m_frameSize;
  sceVideodec2Output output{};
  output.size = sizeof(output);

  const int32_t rc = sceVideodec2Decode(m_decoder, &input, &frame, &output);
  if (rc != 0 || output.error)
  {
    error = rc != 0 ? Hex("sceVideodec2Decode", rc) : "decoder reported a stream error";
    return false;
  }
  gotPicture = ToPicture(&output, picture);
  return true;
}

bool CVideoDec2::Flush(VideoDec2Picture* picture)
{
  if (!m_decoder)
    return false;
  sceVideodec2Frame frame{};
  frame.size = sizeof(frame);
  frame.buffer = NextFrameBuffer();
  frame.bufferSize = m_frameSize;
  sceVideodec2Output output{};
  output.size = sizeof(output);
  if (sceVideodec2Flush(m_decoder, &frame, &output) != 0)
    return false;
  return ToPicture(&output, picture);
}

void CVideoDec2::Reset()
{
  if (m_decoder)
    sceVideodec2Reset(m_decoder);
}

} // namespace KODI::PLATFORM::PS5
