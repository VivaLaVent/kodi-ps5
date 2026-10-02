/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "SandboxPS5.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" int sceKernelDebugOutText(int channel, const char* text);

namespace
{
constexpr const char* kRequest = "/download0/etahen_jailbreak";
constexpr const char* kEscapedTitleRoot = "/mnt/sandbox/PPSA99420_000";

void Klog(const char* text)
{
  sceKernelDebugOutText(0, text);
}

bool DropRequest()
{
  const int fd = open(kRequest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
  {
    Klog("[kodi-ps5] sandbox-thread: cannot write /download0/etahen_jailbreak\n");
    return false;
  }
  const std::string body = "{\"PID\":\"" + std::to_string(getpid()) + "\"}";
  const ssize_t written = write(fd, body.data(), body.size());
  close(fd);
  return written == static_cast<ssize_t>(body.size());
}

// One request: drop it, wait up to `ticks` x 250 ms for the sandbox to open.
bool Attempt(int ticks, bool& consumed)
{
  consumed = false;
  if (!DropRequest())
    return false;
  for (int i = 0; i < ticks; ++i)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    struct stat st;
    if (!consumed && stat(kRequest, &st) != 0)
      consumed = true; // the daemon unlinks the file when it acts
    if (KODI::PLATFORM::PS5::IsSandboxOpen())
      return true;
  }
  return false;
}

void Run()
{
  if (KODI::PLATFORM::PS5::IsSandboxOpen())
  {
    Klog("[kodi-ps5] sandbox-thread: already open\n");
    return;
  }
  bool consumed = false;
  bool opened = Attempt(12, consumed);
  if (!opened && consumed)
    opened = Attempt(12, consumed); // a first attempt can lose a timing race
  if (!consumed)
    unlink(kRequest); // no daemon: leave nothing behind for a later one
  Klog(opened ? "[kodi-ps5] sandbox-thread: request consumed/opened\n"
              : "[kodi-ps5] sandbox-thread: request did not open sandbox\n");
  if (opened)
    Klog("[kodi-ps5] sandbox-thread: sandbox opened by jailbreak daemon\n");
  else if (consumed)
    Klog("[kodi-ps5] sandbox-thread: daemon consumed request but title-root remap did not appear\n");
  else
    Klog("[kodi-ps5] sandbox-thread: no jailbreak daemon answered\n");
}
} // namespace

bool KODI::PLATFORM::PS5::IsSandboxOpen()
{
  // In escaped mode the jailed title root remains reachable as a real path.
  DIR* const rootedTitle = opendir(kEscapedTitleRoot);
  if (!rootedTitle)
    return false;

  closedir(rootedTitle);
  return true;
}

void KODI::PLATFORM::PS5::RequestSandboxOpen()
{
  std::thread(Run).detach();
}
