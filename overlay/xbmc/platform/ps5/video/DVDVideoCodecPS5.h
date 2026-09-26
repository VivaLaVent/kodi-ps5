/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "VideoDec2.h"
#include "cores/VideoPlayer/DVDCodecs/Video/DVDVideoCodec.h"

#include <deque>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct AVBSFContext;
struct AVPacket;
class CVideoBuffer;

namespace KODI::PLATFORM::PS5
{

/*!
 * Hardware H.264 / HEVC Main decoding on the PS5 (libSceVideodec2).
 *
 * Pictures are copied out of the decoder's frame buffers into Kodi's NV12
 * system-memory buffers, which the OpenGL renderer uploads as usual. Streams
 * the decoder cannot take (other codecs, 10-bit, 4:2:2 ...) fall back to FFmpeg.
 * An empty file "kodi-swdecode" in the title folder turns this decoder off.
 */
class CDVDVideoCodecPS5 : public CDVDVideoCodec
{
public:
  explicit CDVDVideoCodecPS5(CProcessInfo& processInfo);
  ~CDVDVideoCodecPS5() override;

  static std::unique_ptr<CDVDVideoCodec> Create(CProcessInfo& processInfo);
  static void Register();

  bool Open(CDVDStreamInfo& hints, CDVDCodecOptions& options) override;
  bool AddData(const DemuxPacket& packet) override;
  void Reset() override;
  VCReturn GetPicture(VideoPicture* pVideoPicture) override;
  const char* GetName() override { return "ps5-videodec2"; }
  unsigned GetAllowedReferences() override { return 4; }
  void SetCodecControl(int flags) override { m_codecControlFlags = flags; }

private:
  struct Decoded
  {
    CVideoBuffer* buffer = nullptr;
    double pts = 0;
  };

  bool SetupBitstreamFilter(const CDVDStreamInfo& hints);
  bool DecodeOne(const uint8_t* data, size_t size);
  bool Keep(const VideoDec2Picture& picture);
  double NextPts();
  void ClearQueue();

  CVideoDec2 m_decoder;
  AVBSFContext* m_bsf = nullptr;
  AVPacket* m_packet = nullptr;

  std::deque<Decoded> m_decoded;
  std::multiset<double> m_pts; // decoded in display order: the smallest pending pts is next
  double m_lastPts = 0;
  double m_frameDuration = 0;

  unsigned m_width = 0;
  unsigned m_height = 0;
  unsigned m_displayWidth = 0;
  unsigned m_displayHeight = 0;
  CDVDStreamInfo* m_hints = nullptr;
  AVColorSpace m_colorSpace = AVCOL_SPC_UNSPECIFIED;
  AVColorPrimaries m_colorPrimaries = AVCOL_PRI_UNSPECIFIED;
  AVColorTransferCharacteristic m_colorTransfer = AVCOL_TRC_UNSPECIFIED;
  bool m_fullRange = false;
  std::string m_stereoMode;

  int m_codecControlFlags = 0;
  unsigned m_errorsInRow = 0;
  bool m_fatal = false;
};

} // namespace KODI::PLATFORM::PS5
