/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "PS5StorageProvider.h"

#include "MediaSource.h"

#include <sys/stat.h>

std::unique_ptr<IStorageProvider> IStorageProvider::CreateInstance()
{
  return std::make_unique<CPS5StorageProvider>();
}

namespace
{
bool Exists(const char* path)
{
  struct stat st;
  return stat(path, &st) == 0;
}

void Add(std::vector<CMediaSource>& drives, const char* path, const char* name)
{
  if (!Exists(path))
    return;
  CMediaSource share;
  share.strPath = path;
  share.strName = name;
  share.m_iDriveType = SourceType::LOCAL;
  drives.push_back(share);
}
} // namespace

void CPS5StorageProvider::GetLocalDrives(std::vector<CMediaSource>& localDrives)
{
  Add(localDrives, "/download0", "Kodi data (/download0)");
  Add(localDrives, "/app0", "Application image (/app0)");
  // Present when the loader has jailbroken the process (etaHEN / lapy daemon).
  Add(localDrives, "/data", "System data (/data)");
}

void CPS5StorageProvider::GetRemovableDrives(std::vector<CMediaSource>& removableDrives)
{
  // USB media appear at /mnt/usbN when the process is allowed to see them.
  const char* const usb[] = {"/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3"};
  for (const char* path : usb)
    Add(removableDrives, path, path);
}

std::vector<std::string> CPS5StorageProvider::GetDiskUsage()
{
  return {};
}
