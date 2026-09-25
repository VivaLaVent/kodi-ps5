/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

namespace KODI::PLATFORM::PS5
{
// Read-only report of the system's video output (resolution, refresh rate)
// and of the output modes it accepts for this title, written to the log.
// Needs the GL driver's video out handle, which exists once the first frame
// has been presented; returns false (nothing logged) before that.
bool LogVideoOutInfo();
} // namespace KODI::PLATFORM::PS5
