/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "AESinkPS5.h"

#include "cores/AudioEngine/AESinkFactory.h"
#include "cores/AudioEngine/Utils/AEUtil.h"
#include "utils/log.h"

#include "platform/ps5/sce/SceAudioOut.h"

#include <algorithm>
#include <cstring>

using namespace KODI::PLATFORM::PS5;

CAESinkPS5::~CAESinkPS5()
{
  Deinitialize();
}

void CAESinkPS5::Register()
{
  AE::AESinkRegEntry entry;
  entry.sinkName = "PS5";
  entry.createFunc = CAESinkPS5::Create;
  entry.enumerateFunc = CAESinkPS5::EnumerateDevicesEx;
  AE::CAESinkFactory::RegisterSink(entry);
}

std::unique_ptr<IAESink> CAESinkPS5::Create(std::string& device, AEAudioFormat& desiredFormat)
{
  auto sink = std::make_unique<CAESinkPS5>();
  if (sink->Initialize(desiredFormat, device))
    return sink;
  return {};
}

void CAESinkPS5::EnumerateDevicesEx(AEDeviceInfoList& list, bool force)
{
  CAEDeviceInfo info;
  info.m_deviceName = "main";
  info.m_displayName = "PlayStation 5";
  info.m_displayNameExtra = "HDMI / headset (system mixer)";
  info.m_deviceType = AE_DEVTYPE_PCM;
  info.m_wantsIECPassthrough = false;
  info.m_channels += AE_CH_FL;
  info.m_channels += AE_CH_FR;
  info.m_sampleRates.push_back(AUDIO_OUT_SAMPLE_RATE);
  info.m_dataFormats.push_back(AE_FMT_FLOAT);
  info.m_dataFormats.push_back(AE_FMT_S16NE);
  list.push_back(info);
}

bool CAESinkPS5::Initialize(AEAudioFormat& format, std::string& device)
{
  // The port is fixed at 48 kHz stereo; ActiveAE resamples/downmixes for us.
  format.m_sampleRate = AUDIO_OUT_SAMPLE_RATE;
  format.m_channelLayout = AE_CH_LAYOUT_2_0;
  m_channels = 2;

  uint32_t param;
  if (format.m_dataFormat == AE_FMT_S16NE)
  {
    param = AUDIO_OUT_FORMAT_S16_STEREO;
    m_frameSize = m_channels * sizeof(int16_t);
  }
  else
  {
    format.m_dataFormat = AE_FMT_FLOAT;
    param = AUDIO_OUT_FORMAT_FLOAT_STEREO;
    m_frameSize = m_channels * sizeof(float);
  }
  format.m_frameSize = m_frameSize;
  format.m_frames = GRAIN_FRAMES;

  const int32_t initResult = sceAudioOutInit();
  if (initResult != 0)
    CLog::Log(LOGDEBUG, "CAESinkPS5: sceAudioOutInit returned {:#x} (already initialised is fine)",
              static_cast<uint32_t>(initResult));

  m_handle = sceAudioOutOpen(AUDIO_OUT_USER_ID_SYSTEM, AUDIO_OUT_PORT_TYPE_MAIN, 0, GRAIN_FRAMES,
                             AUDIO_OUT_SAMPLE_RATE, param);
  if (m_handle <= 0)
  {
    CLog::Log(LOGERROR, "CAESinkPS5: sceAudioOutOpen failed: {:#x}", static_cast<uint32_t>(m_handle));
    m_handle = -1;
    return false;
  }

  m_block.assign(static_cast<size_t>(GRAIN_FRAMES) * m_frameSize, 0);
  m_blockFrames = 0;

  CLog::Log(LOGINFO, "CAESinkPS5: opened main port, {} Hz, {} ch, {} frames/block, {}",
            AUDIO_OUT_SAMPLE_RATE, m_channels, GRAIN_FRAMES,
            param == AUDIO_OUT_FORMAT_FLOAT_STEREO ? "float" : "s16");
  return true;
}

void CAESinkPS5::Deinitialize()
{
  if (m_handle > 0)
  {
    sceAudioOutClose(m_handle);
    m_handle = -1;
  }
  m_block.clear();
  m_blockFrames = 0;
}

bool CAESinkPS5::Output(const uint8_t* block)
{
  const int32_t result = sceAudioOutOutput(m_handle, block);
  if (result < 0)
  {
    CLog::Log(LOGERROR, "CAESinkPS5: sceAudioOutOutput failed: {:#x}", static_cast<uint32_t>(result));
    return false;
  }
  return true;
}

double CAESinkPS5::GetCacheTotal()
{
  return static_cast<double>(GRAIN_FRAMES * QUEUE_DEPTH) / AUDIO_OUT_SAMPLE_RATE;
}

double CAESinkPS5::GetLatency()
{
  // Unknown mixer latency downstream of the port; the AE sync loop measures
  // the rest through GetDelay().
  return 0.0;
}

unsigned int CAESinkPS5::AddPackets(uint8_t** data, unsigned int frames, unsigned int offset)
{
  if (m_handle <= 0)
    return 0;

  const uint8_t* src = data[0] + static_cast<size_t>(offset) * m_frameSize;
  unsigned int remaining = frames;

  while (remaining > 0)
  {
    const unsigned int space = GRAIN_FRAMES - m_blockFrames;
    const unsigned int n = std::min(space, remaining);
    std::memcpy(m_block.data() + static_cast<size_t>(m_blockFrames) * m_frameSize, src,
                static_cast<size_t>(n) * m_frameSize);
    m_blockFrames += n;
    src += static_cast<size_t>(n) * m_frameSize;
    remaining -= n;

    if (m_blockFrames == GRAIN_FRAMES)
    {
      // Blocks until the previous block has been consumed: this is our clock.
      if (!Output(m_block.data()))
        return frames - remaining;
      m_blockFrames = 0;
    }
  }
  return frames;
}

void CAESinkPS5::GetDelay(AEDelayStatus& status)
{
  // After a blocking write returns, at most one block is still playing plus
  // whatever we have assembled but not yet handed over.
  const double frames = static_cast<double>(GRAIN_FRAMES + m_blockFrames);
  status.SetDelay(frames / AUDIO_OUT_SAMPLE_RATE);
}

void CAESinkPS5::Drain()
{
  if (m_handle <= 0)
    return;

  if (m_blockFrames > 0)
  {
    std::memset(m_block.data() + static_cast<size_t>(m_blockFrames) * m_frameSize, 0,
                static_cast<size_t>(GRAIN_FRAMES - m_blockFrames) * m_frameSize);
    Output(m_block.data());
    m_blockFrames = 0;
  }
  // A null buffer waits for the queue to run dry (PS4 semantics, see header).
  sceAudioOutOutput(m_handle, nullptr);
}
