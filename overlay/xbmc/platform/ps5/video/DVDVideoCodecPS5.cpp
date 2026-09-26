/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVDVideoCodecPS5.h"

#include "VideoCodecRegistration.h"

#include "cores/VideoPlayer/Buffers/VideoBuffer.h"
#include "cores/VideoPlayer/DVDCodecs/DVDFactoryCodec.h"
#include "cores/VideoPlayer/DVDStreamInfo.h"
#include "cores/VideoPlayer/Interface/DemuxPacket.h"
#include "cores/VideoPlayer/Interface/TimingConstants.h"
#include "cores/VideoPlayer/Process/ProcessInfo.h"
#include "utils/log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include <unistd.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
}

using namespace KODI::PLATFORM::PS5;

namespace
{
constexpr unsigned kMaxErrorsInRow = 60; // then give up (Kodi shows an error)

bool IsH264Supported(int profile)
{
  // 8-bit 4:2:0 profiles; High 10, 4:2:2 and 4:4:4 go to FFmpeg
  switch (profile)
  {
    case AV_PROFILE_H264_BASELINE:
    case AV_PROFILE_H264_CONSTRAINED_BASELINE:
    case AV_PROFILE_H264_MAIN:
    case AV_PROFILE_H264_EXTENDED:
    case AV_PROFILE_H264_HIGH:
    case AV_PROFILE_UNKNOWN:
      return true;
    default:
      return false;
  }
}
} // namespace

CDVDVideoCodecPS5::CDVDVideoCodecPS5(CProcessInfo& processInfo) : CDVDVideoCodec(processInfo)
{
}

CDVDVideoCodecPS5::~CDVDVideoCodecPS5()
{
  ClearQueue();
  av_bsf_free(&m_bsf);
  av_packet_free(&m_packet);
  m_decoder.Close();
}

std::unique_ptr<CDVDVideoCodec> CDVDVideoCodecPS5::Create(CProcessInfo& processInfo)
{
  return std::make_unique<CDVDVideoCodecPS5>(processInfo);
}

void CDVDVideoCodecPS5::Register()
{
  CDVDFactoryCodec::RegisterHWVideoCodec("ps5-videodec2", &CDVDVideoCodecPS5::Create);
}

bool CDVDVideoCodecPS5::Open(CDVDStreamInfo& hints, CDVDCodecOptions& options)
{
  if (access("/app0/kodi-swdecode", F_OK) == 0)
  {
    CLog::Log(LOGINFO, "CDVDVideoCodecPS5: kodi-swdecode present, using software decoding");
    return false;
  }

  VideoDec2Codec codec;
  if (hints.codec == AV_CODEC_ID_H264 && IsH264Supported(hints.profile))
    codec = VideoDec2Codec::H264;
  else if (hints.codec == AV_CODEC_ID_HEVC &&
           (hints.profile == AV_PROFILE_HEVC_MAIN || hints.profile == AV_PROFILE_UNKNOWN) &&
           hints.bitsperpixel <= 8)
    codec = VideoDec2Codec::HEVC;
  else
    return false; // FFmpeg takes it

  if (hints.width <= 0 || hints.height <= 0 || hints.width > 3840 || hints.height > 2176)
    return false;
  if (hints.interlaced)
    return false; // the decoder is configured for progressive content

  std::string error;
  if (!m_decoder.Open(codec, hints.width, hints.height, error))
  {
    CLog::Log(LOGWARNING, "CDVDVideoCodecPS5: hardware decoder unavailable ({}), using FFmpeg",
              error);
    m_decoder.Close();
    return false;
  }
  if (!SetupBitstreamFilter(hints))
  {
    m_decoder.Close();
    return false;
  }

  m_width = static_cast<unsigned>(hints.width);
  m_height = static_cast<unsigned>(hints.height);
  m_displayWidth = m_width;
  m_displayHeight = m_height;
  if (hints.aspect > 0.0)
  {
    m_displayWidth = static_cast<unsigned>(std::lrint(m_height * hints.aspect)) & ~3u;
    if (m_displayWidth < m_width)
    {
      m_displayWidth = m_width;
      m_displayHeight = static_cast<unsigned>(std::lrint(m_width / hints.aspect)) & ~3u;
    }
  }
  m_frameDuration = (hints.fpsrate > 0 && hints.fpsscale > 0)
                        ? DVD_TIME_BASE * static_cast<double>(hints.fpsscale) / hints.fpsrate
                        : DVD_TIME_BASE / 25.0;
  m_colorSpace = hints.colorSpace;
  m_colorPrimaries = hints.colorPrimaries;
  m_colorTransfer = hints.colorTransferCharacteristic;
  m_fullRange = hints.colorRange == AVCOL_RANGE_JPEG;
  m_stereoMode = hints.stereo_mode;

  m_processInfo.SetVideoDecoderName(GetName(), true);
  m_processInfo.SetVideoPixelFormat("nv12");
  m_processInfo.SetVideoDimensions(hints.width, hints.height);
  m_processInfo.SetVideoDeintMethod("none");
  if (hints.fpsrate > 0 && hints.fpsscale > 0)
    m_processInfo.SetVideoFps(static_cast<float>(hints.fpsrate) / hints.fpsscale);

  CLog::Log(LOGINFO, "CDVDVideoCodecPS5: hardware {} decoding {}x{}",
            codec == VideoDec2Codec::H264 ? "H.264" : "HEVC", hints.width, hints.height);
  return true;
}

bool CDVDVideoCodecPS5::SetupBitstreamFilter(const CDVDStreamInfo& hints)
{
  m_packet = av_packet_alloc();
  if (!m_packet)
    return false;

  // MP4/MKV carry avcC/hvcC (length-prefixed NAL units, parameter sets in
  // extradata); the decoder wants Annex-B. Already Annex-B: no filter.
  const uint8_t* extra = hints.extradata.GetData();
  const size_t extraSize = hints.extradata.GetSize();
  if (!extra || extraSize < 4 || extra[0] != 1)
    return true;

  const char* name = hints.codec == AV_CODEC_ID_H264 ? "h264_mp4toannexb" : "hevc_mp4toannexb";
  const AVBitStreamFilter* filter = av_bsf_get_by_name(name);
  if (!filter || av_bsf_alloc(filter, &m_bsf) < 0)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecPS5: bitstream filter {} unavailable", name);
    return false;
  }
  m_bsf->par_in->codec_type = AVMEDIA_TYPE_VIDEO;
  m_bsf->par_in->codec_id = hints.codec;
  m_bsf->par_in->extradata =
      static_cast<uint8_t*>(av_mallocz(extraSize + AV_INPUT_BUFFER_PADDING_SIZE));
  if (!m_bsf->par_in->extradata)
    return false;
  std::memcpy(m_bsf->par_in->extradata, extra, extraSize);
  m_bsf->par_in->extradata_size = static_cast<int>(extraSize);
  if (av_bsf_init(m_bsf) < 0)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecPS5: cannot initialise {}", name);
    av_bsf_free(&m_bsf);
    return false;
  }
  return true;
}

bool CDVDVideoCodecPS5::AddData(const DemuxPacket& packet)
{
  if (m_fatal)
    return false;
  if (!packet.pData || packet.iSize <= 0)
    return true;
  // one decode per packet may leave a picture queued; take it first
  if (m_decoded.size() >= 2)
    return false;

  const double pts = packet.pts != DVD_NOPTS_VALUE ? packet.pts : packet.dts;
  if (pts != DVD_NOPTS_VALUE)
    m_pts.insert(pts);

  if (!m_bsf)
    return DecodeOne(packet.pData, static_cast<size_t>(packet.iSize)) || !m_fatal;

  av_packet_unref(m_packet);
  if (av_new_packet(m_packet, packet.iSize) < 0)
    return true;
  std::memcpy(m_packet->data, packet.pData, static_cast<size_t>(packet.iSize));
  if (av_bsf_send_packet(m_bsf, m_packet) < 0)
    return true; // bad packet: drop it
  while (av_bsf_receive_packet(m_bsf, m_packet) == 0)
  {
    DecodeOne(m_packet->data, static_cast<size_t>(m_packet->size));
    av_packet_unref(m_packet);
  }
  return !m_fatal;
}

namespace
{
double NowMs()
{
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
} // namespace

void CDVDVideoCodecPS5::AccountTiming()
{
  const double now = NowMs();
  if (m_statsWindowStart == 0)
    m_statsWindowStart = now;
  const double window = now - m_statsWindowStart;
  if (window < 5000.0)
    return;
  CLog::Log(LOGDEBUG,
            "CDVDVideoCodecPS5: {:.1f} decodes/s, {:.1f} pictures/s; decode avg {:.2f} ms / max "
            "{:.2f} ms, copy-out avg {:.2f} ms / max {:.2f} ms",
            m_statsDecodes * 1000.0 / window, m_statsPictures * 1000.0 / window,
            m_statsDecodes ? m_statsDecodeMs / m_statsDecodes : 0.0, m_statsDecodeMaxMs,
            m_statsPictures ? m_statsCopyMs / m_statsPictures : 0.0, m_statsCopyMaxMs);
  m_statsWindowStart = now;
  m_statsDecodes = m_statsPictures = 0;
  m_statsDecodeMs = m_statsDecodeMaxMs = m_statsCopyMs = m_statsCopyMaxMs = 0;
}

bool CDVDVideoCodecPS5::DecodeOne(const uint8_t* data, size_t size)
{
  bool gotPicture = false;
  VideoDec2Picture picture;
  std::string error;
  const double start = NowMs();
  const bool ok = m_decoder.Decode(data, size, gotPicture, &picture, error);
  const double took = NowMs() - start;
  ++m_statsDecodes;
  m_statsDecodeMs += took;
  m_statsDecodeMaxMs = std::max(m_statsDecodeMaxMs, took);
  AccountTiming();
  if (!ok)
  {
    if (m_errorsInRow++ < 5)
      CLog::Log(LOGWARNING, "CDVDVideoCodecPS5: {}", error);
    if (m_errorsInRow >= kMaxErrorsInRow)
    {
      CLog::Log(LOGERROR, "CDVDVideoCodecPS5: {} decode errors in a row, giving up",
                m_errorsInRow);
      m_fatal = true;
    }
    return false;
  }
  m_errorsInRow = 0;
  if (gotPicture)
    Keep(picture);
  return true;
}

double CDVDVideoCodecPS5::NextPts()
{
  if (!m_pts.empty())
  {
    m_lastPts = *m_pts.begin();
    m_pts.erase(m_pts.begin());
  }
  else
    m_lastPts += m_frameDuration;
  return m_lastPts;
}

bool CDVDVideoCodecPS5::Keep(const VideoDec2Picture& picture)
{
  // Visible area: the stream's size, never more than what was decoded.
  const unsigned width = std::min(m_width, picture.width);
  const unsigned height = std::min(m_height, picture.height);
  const size_t pitch = picture.pitch;
  const size_t size = pitch * height * 3 / 2;

  CVideoBuffer* buffer =
      m_processInfo.GetVideoBufferManager().Get(AV_PIX_FMT_NV12, static_cast<int>(size), nullptr);
  if (!buffer)
  {
    CLog::Log(LOGERROR, "CDVDVideoCodecPS5: no video buffer available");
    NextPts(); // keep timestamps in step
    return false;
  }
  const int strides[YuvImage::MAX_PLANES] = {static_cast<int>(pitch), static_cast<int>(pitch), 0};
  buffer->SetDimensions(static_cast<int>(width), static_cast<int>(height), strides);
  uint8_t* planes[YuvImage::MAX_PLANES];
  buffer->GetPlanes(planes);
  // luma: `height` visible rows; chroma starts after the full coded height
  const double start = NowMs();
  std::memcpy(planes[0], picture.data, pitch * height);
  std::memcpy(planes[1], picture.data + pitch * picture.height, pitch * height / 2);
  const double took = NowMs() - start;
  ++m_statsPictures;
  m_statsCopyMs += took;
  m_statsCopyMaxMs = std::max(m_statsCopyMaxMs, took);

  m_decoded.push_back(Decoded{buffer, NextPts()});
  return true;
}

void CDVDVideoCodecPS5::ClearQueue()
{
  for (auto& d : m_decoded)
    if (d.buffer)
      d.buffer->Release();
  m_decoded.clear();
}

void CDVDVideoCodecPS5::Reset()
{
  ClearQueue();
  m_pts.clear();
  m_decoder.Reset();
  if (m_bsf)
    av_bsf_flush(m_bsf);
  m_errorsInRow = 0;
  m_codecControlFlags = 0;
}

CDVDVideoCodec::VCReturn CDVDVideoCodecPS5::GetPicture(VideoPicture* pVideoPicture)
{
  if (m_fatal)
    return VC_ERROR;

  if (m_decoded.empty() && (m_codecControlFlags & DVD_CODEC_CTRL_DRAIN))
  {
    VideoDec2Picture picture;
    if (m_decoder.Flush(&picture))
      Keep(picture);
    else
      return VC_EOF;
  }
  if (m_decoded.empty())
    return VC_BUFFER;

  Decoded d = m_decoded.front();
  m_decoded.pop_front();

  pVideoPicture->Reset(); // releases the previous picture's buffer
  pVideoPicture->videoBuffer = d.buffer;
  pVideoPicture->pts = d.pts;
  pVideoPicture->dts = DVD_NOPTS_VALUE;
  pVideoPicture->iDuration = m_frameDuration;
  pVideoPicture->iWidth = m_width;
  pVideoPicture->iHeight = m_height;
  pVideoPicture->iDisplayWidth = m_displayWidth;
  pVideoPicture->iDisplayHeight = m_displayHeight;
  pVideoPicture->pixelFormat = AV_PIX_FMT_NV12;
  pVideoPicture->colorBits = 8;
  pVideoPicture->color_space = m_colorSpace;
  pVideoPicture->color_primaries = m_colorPrimaries;
  pVideoPicture->m_originalColorPrimaries = m_colorPrimaries;
  pVideoPicture->color_transfer = m_colorTransfer;
  pVideoPicture->color_range = m_fullRange ? 1 : 0;
  pVideoPicture->stereoMode = m_stereoMode;
  if (m_codecControlFlags & DVD_CODEC_CTRL_DROP)
    pVideoPicture->iFlags |= DVP_FLAG_DROPPED;
  return VC_PICTURE;
}

void KODI::PLATFORM::PS5::RegisterVideoCodecs()
{
  CDVDVideoCodecPS5::Register();
}
