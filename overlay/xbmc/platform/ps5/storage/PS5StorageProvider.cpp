/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "PS5StorageProvider.h"

#include "utils/log.h"

#include "MediaSource.h"

#include <sys/stat.h>
#include <dirent.h>
#include <cerrno>

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
  // USB media appear at /mnt/usbN (and extended storage at /mnt/extN) when the
  // title's sandbox lets it see them. What it can see is logged once.
  static const char* const usb[] = {"/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3",
                                    "/mnt/usb4", "/mnt/usb5", "/mnt/usb6", "/mnt/usb7",
                                    "/mnt/ext0", "/mnt/ext1"};
  static bool logged = false;
  if (!logged)
  {
    logged = true;
    for (const char* path : usb)
    {
      struct stat st;
      if (stat(path, &st) != 0)
      {
        if (errno != ENOENT)
          CLog::Log(LOGINFO, "CPS5StorageProvider: {}: not accessible (errno {})", path, errno);
        continue;
      }
      DIR* dir = opendir(path);
      const int dirErr = dir ? 0 : errno;
      int entries = 0;
      if (dir)
      {
        while (readdir(dir) && entries < 1000)
          ++entries;
        closedir(dir);
      }
      if (dir)
        CLog::Log(LOGINFO, "CPS5StorageProvider: {}: present, {} entries readable", path,
                  entries);
      else
        CLog::Log(LOGINFO, "CPS5StorageProvider: {}: present, cannot be listed (errno {})", path,
                  dirErr);
    }
    CLog::Log(LOGINFO, "CPS5StorageProvider: USB check done (/mnt/usb0-7, /mnt/ext0-1)");
  }
  for (const char* path : usb)
    Add(removableDrives, path, path);
}

std::vector<std::string> CPS5StorageProvider::GetDiskUsage()
{
  return {};
}
