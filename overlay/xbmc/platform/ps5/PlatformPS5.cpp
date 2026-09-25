/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "PlatformPS5.h"

#include "platform/ps5/audio/AESinkPS5.h"
#include "windowing/ps5/WinSystemPS5GLContext.h"

CPlatform* CPlatform::CreateInstance()
{
  return new CPlatformPS5();
}

bool CPlatformPS5::InitStageOne()
{
  if (!CPlatformPosix::InitStageOne())
    return false;

  // Exactly one window system and one audio sink exist on the console.
  KODI::WINDOWING::PS5::CWinSystemPS5GLContext::Register();
  CAESinkPS5::Register();

  return true;
}
