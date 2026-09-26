/*
 *  Copyright (C) 2026 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "application/AppEnvironment.h"
#include "application/AppParamParser.h"
#include "application/AppParams.h"
#include "platform/xbmc.h"

#include "platform/posix/PlatformPosix.h"

#include <cerrno>
#include <clocale>
#include <cstdarg>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" int sceKernelDebugOutText(int channel, const char* text);
// FreeBSD <dirent.h> declares these only with __BSD_VISIBLE; used by the
// directory diagnostics below.
extern "C" int getdents(int fd, char* buf, int nbytes);
extern "C" int getdirentries(int fd, char* buf, int nbytes, long* basep);

// The few SQLite calls the startup probe needs (libsqlite3 is linked into
// Kodi; its header lives outside the kodi target's include path).
extern "C"
{
typedef struct sqlite3 sqlite3;
int sqlite3_open_v2(const char* filename, sqlite3** db, int flags, const char* vfs);
int sqlite3_exec(sqlite3* db, const char* sql, int (*cb)(void*, int, char**, char**), void* arg,
                 char** errmsg);
const char* sqlite3_errmsg(sqlite3* db);
int sqlite3_close(sqlite3* db);
int sqlite3_extended_errcode(sqlite3* db);
int sqlite3_config(int op, ...);
typedef struct sqlite3_vfs sqlite3_vfs;
sqlite3_vfs* sqlite3_vfs_find(const char* name);
int sqlite3_vfs_register(sqlite3_vfs* vfs, int makeDefault);
}
// Public, stable layout of sqlite3_vfs (sqlite3.h, iVersion 3); only the
// system-call override hook is used.
typedef void (*sqlite3_syscall_ptr)(void);
struct Sqlite3VfsV3
{
  int iVersion;
  int szOsFile;
  int mxPathname;
  void* pNext;
  const char* zName;
  void* pAppData;
  void* xOpen;
  void* xDelete;
  void* xAccess;
  void* xFullPathname;
  void* xDlOpen;
  void* xDlError;
  void* xDlSym;
  void* xDlClose;
  void* xRandomness;
  void* xSleep;
  void* xCurrentTime;
  void* xGetLastError;
  void* xCurrentTimeInt64;
  int (*xSetSystemCall)(sqlite3_vfs*, const char* name, sqlite3_syscall_ptr call);
  sqlite3_syscall_ptr (*xGetSystemCall)(sqlite3_vfs*, const char* name);
  const char* (*xNextSystemCall)(sqlite3_vfs*, const char* name);
};
constexpr int SQLITE_OK = 0;
constexpr int SQLITE_CONFIG_LOG = 16;
constexpr int SQLITE_OPEN_READWRITE = 0x00000002;
constexpr int SQLITE_OPEN_CREATE = 0x00000004;

namespace
{
extern "C" void XBMC_PS5_HandleSignal(int sig)
{
  CPlatformPosix::RequestQuit();
}

/*
 * A title's stdout/stderr go nowhere; Kodi's log reaches klog through the
 * platform log sink (PS5InterfaceForCLog). These markers bracket startup and
 * are written straight to klog, so they appear even if logging never starts.
 */
void Klog(const char* text)
{
  sceKernelDebugOutText(0, text);
}

void Klogf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Klogf(const char* fmt, ...)
{
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Klog(buf);
}

void SqliteLog(void*, int code, const char* msg)
{
  Klogf("[kodi-ps5]   sqlite log (%d): %s\n", code, msg);
}

/*
 * SQLite resolves symlinks in every database path by lstat()-ing each
 * component, and the title sandbox refuses lstat() on its own mount points
 * (/app0, /download0) with EPERM, so every open fails. For exactly that case,
 * answer "a plain directory" - which is what those mount points are.
 */
int SandboxLstat(const char* path, struct stat* st)
{
  if (lstat(path, st) == 0)
    return 0;
  if (errno != EPERM)
    return -1;
  if (stat(path, st) == 0)
    return 0;
  std::memset(st, 0, sizeof(*st));
  st->st_mode = S_IFDIR | 0755;
  errno = 0;
  return 0;
}

// The console filesystems report st_nlink == 0 for every file, which SQLite
// reads as "database file was deleted" and warns about on every open.
int NlinkFstat(int fd, struct stat* st)
{
  const int r = fstat(fd, st);
  if (r == 0 && st->st_nlink == 0)
    st->st_nlink = 1;
  return r;
}

void InstallSqliteSandboxFix()
{
  // The syscall table is shared by all unix* VFSes, so patch it once.
  auto* vfs = reinterpret_cast<Sqlite3VfsV3*>(sqlite3_vfs_find("unix"));
  if (!vfs || vfs->iVersion < 3 || !vfs->xSetSystemCall)
  {
    Klog("[kodi-ps5] sqlite: unix VFS without syscall hooks - sandbox fix not installed\n");
    return;
  }
  const int rc = vfs->xSetSystemCall(reinterpret_cast<sqlite3_vfs*>(vfs), "lstat",
                                     reinterpret_cast<sqlite3_syscall_ptr>(&SandboxLstat));
  const int rc2 = vfs->xSetSystemCall(reinterpret_cast<sqlite3_vfs*>(vfs), "fstat",
                                      reinterpret_cast<sqlite3_syscall_ptr>(&NlinkFstat));
  Klogf("[kodi-ps5] sqlite: sandbox lstat fix %s, nlink fix %s\n",
        rc == SQLITE_OK ? "installed" : "FAILED", rc2 == SQLITE_OK ? "installed" : "FAILED");
}

// BEGIN-FS-HELPERS
/*
 * Files the title creates belong to the title's own account, and with the
 * usual 0755/0644 modes other accounts - e.g. the FTP server - can read but
 * not delete them. Kodi's data is therefore kept world-writable.
 */
// Returns how many chmod() calls failed.
int OpenUpTree(const std::string& path)
{
  struct stat st;
  if (lstat(path.c_str(), &st) != 0)
    return 0;
  int failed = 0;
  if (S_ISDIR(st.st_mode))
  {
    if (chmod(path.c_str(), 0777) != 0)
      ++failed;
    DIR* dir = opendir(path.c_str());
    if (!dir)
      return failed;
    while (struct dirent* e = readdir(dir))
    {
      if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, ".."))
        continue;
      failed += OpenUpTree(path + "/" + e->d_name);
    }
    closedir(dir);
  }
  else if (S_ISREG(st.st_mode) && chmod(path.c_str(), 0666) != 0)
    ++failed;
  return failed;
}

// Delete a directory tree (the reset switch).
int RemoveTree(const std::string& path)
{
  struct stat st;
  if (lstat(path.c_str(), &st) != 0)
    return 0;
  int removed = 0;
  if (S_ISDIR(st.st_mode))
  {
    if (DIR* dir = opendir(path.c_str()))
    {
      while (struct dirent* e = readdir(dir))
      {
        if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, ".."))
          continue;
        removed += RemoveTree(path + "/" + e->d_name);
      }
      closedir(dir);
    }
    if (rmdir(path.c_str()) == 0)
      ++removed;
  }
  else if (unlink(path.c_str()) == 0)
    ++removed;
  return removed;
}
// END-FS-HELPERS

bool MakeDirs(const std::string& path)
{
  for (size_t pos = 1; pos <= path.size(); ++pos)
  {
    if (pos != path.size() && path[pos] != '/')
      continue;
    const std::string part = path.substr(0, pos);
    if (mkdir(part.c_str(), 0777) != 0 && errno != EEXIST)
    {
      Klogf("[kodi-ps5]   mkdir %s: errno %d (%s)\n", part.c_str(), errno, std::strerror(errno));
      return false;
    }
  }
  return true;
}

/*
 * Can Kodi live here? Create $home/.kodi/userdata/Database, write a file and
 * create a real SQLite database (Kodi's first action that needs the disk).
 */
bool ProbeHome(const std::string& home)
{
  Klogf("[kodi-ps5] probing %s\n", home.c_str());
  const std::string dir = home + "/.kodi/userdata/Database";
  if (!MakeDirs(dir))
    return false;

  const std::string file = home + "/.kodi/temp-probe.txt";
  const int fd = open(file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0)
  {
    Klogf("[kodi-ps5]   open %s: errno %d (%s)\n", file.c_str(), errno, std::strerror(errno));
    return false;
  }
  const ssize_t written = write(fd, "ok\n", 3);
  close(fd);
  unlink(file.c_str());
  if (written != 3)
  {
    Klogf("[kodi-ps5]   write: errno %d (%s)\n", errno, std::strerror(errno));
    return false;
  }

  // SQLite: try its default file access first, then the alternatives that
  // avoid POSIX advisory locks. The first that works becomes the default for
  // all of Kodi (single process, so OS-level locking is not needed).
  const std::string db = dir + "/probe.db";
  static const char* const vfsNames[] = {"unix", "unix-none", "unix-dotfile", "unix-excl"};
  bool sqliteOk = false;
  for (const char* vfs : vfsNames)
  {
    unlink(db.c_str());
    unlink((db + "-journal").c_str());
    rmdir((db + ".lock").c_str());
    sqlite3* handle = nullptr;
    int rc = sqlite3_open_v2(db.c_str(), &handle, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, vfs);
    if (rc == SQLITE_OK)
      rc = sqlite3_exec(handle, "CREATE TABLE IF NOT EXISTS t(x); INSERT INTO t VALUES(1);", nullptr,
                        nullptr, nullptr);
    if (rc == SQLITE_OK)
    {
      Klogf("[kodi-ps5]   sqlite vfs '%s' works\n", vfs);
      if (std::strcmp(vfs, "unix") != 0)
      {
        sqlite3_vfs_register(sqlite3_vfs_find(vfs), 1);
        Klogf("[kodi-ps5]   made sqlite vfs '%s' the default\n", vfs);
      }
      sqliteOk = true;
    }
    else
      Klogf("[kodi-ps5]   sqlite vfs '%s': rc %d/%d (%s), errno %d (%s)\n", vfs, rc,
            handle ? sqlite3_extended_errcode(handle) : -1,
            handle ? sqlite3_errmsg(handle) : "no handle", errno, std::strerror(errno));
    sqlite3_close(handle);
    unlink(db.c_str());
    unlink((db + "-journal").c_str());
    if (sqliteOk)
      break;
  }
  if (!sqliteOk)
    return false;

  Klogf("[kodi-ps5]   %s is usable\n", home.c_str());
  return true;
}
} // namespace

// Switch files in the title folder. access() is refused inside the title
// sandbox (the switches were never seen with it), while stat() works on the
// same paths, so presence is tested with stat().
static bool SwitchPresent(const char* path)
{
  struct stat st;
  if (stat(path, &st) == 0)
    return true;
  if (errno != ENOENT)
    Klogf("[kodi-ps5] switch check %s: errno %d (%s)\n", path, errno, strerror(errno));
  return false;
}

int main(int argc, char* argv[])
{
  // Written straight to klog: visible even if everything after this fails.
  Klog("[kodi-ps5] main() reached\n");

  struct sigaction signalHandler;
  std::memset(&signalHandler, 0, sizeof(signalHandler));
  signalHandler.sa_handler = &XBMC_PS5_HandleSignal;
  signalHandler.sa_flags = SA_RESTART;
  sigaction(SIGINT, &signalHandler, nullptr);
  sigaction(SIGTERM, &signalHandler, nullptr);

  setlocale(LC_NUMERIC, "C");

  // Title layout: the application image is mounted read-only at /app0 and the
  // title's writable area is /download0 (requires downloadDataSize > 0 in
  // sce_sys/param.json). Kodi keeps everything mutable under $HOME/.kodi.
  // Both default with overwrite=0 so a loader-provided environment wins, which
  // lets you run a development copy from e.g. /data/kodi.
  sqlite3_config(SQLITE_CONFIG_LOG, &SqliteLog, nullptr);
  InstallSqliteSandboxFix();

  // Everything Kodi creates is made deletable over FTP by OpenUpTree()
  // below (at start) and before exit; umask() itself is a no-op on a title.

  // Switches, created as empty files in the title folder over FTP
  // (/data/homebrew/<TITLE_ID>/...). Files the title created cannot be
  // deleted from outside its sandbox (FTP gets "permission denied" whatever
  // their mode), so the title removes its own data:
  //   kodi-reset      wipe Kodi's data, then start Kodi fresh
  //   kodi-uninstall  wipe Kodi's data and quit; afterwards the whole title
  //                   folder can be deleted over FTP
  if (SwitchPresent("/app0/kodi-uninstall"))
  {
    const int n = RemoveTree("/app0/kodi");
    unlink("/app0/kodi-uninstall");
    Klogf("[kodi-ps5] kodi-uninstall found: removed %d files and folders of /app0/kodi, "
          "quitting\n", n);
    _exit(0);
  }
  if (SwitchPresent("/app0/kodi-reset"))
  {
    const int n = RemoveTree("/app0/kodi");
    unlink("/app0/kodi-reset");
    Klogf("[kodi-ps5] kodi-reset found: removed %d files and folders of /app0/kodi\n", n);
  }

  if (!std::getenv("HOME"))
  {
    // First location where directories, files and SQLite all work:
    //  /data/kodi  preferred, but a sandboxed title normally cannot reach /data
    //  /app0/kodi  the title folder itself = /data/homebrew/<TITLE_ID>/kodi on
    //              disk (reachable over FTP), if the loader mounts it writable
    //  /download0  the title's official writable data area (a disk image)
    static const char* const candidates[] = {"/data/kodi", "/app0/kodi", "/download0"};
    const char* chosen = nullptr;
    for (const char* candidate : candidates)
      if (ProbeHome(candidate))
      {
        chosen = candidate;
        break;
      }
    if (!chosen)
    {
      Klog("[kodi-ps5] no writable home found; Kodi will fail to start\n");
      chosen = "/download0";
    }
    Klogf("[kodi-ps5] HOME=%s\n", chosen);
    setenv("HOME", chosen, 1);
  }
  const std::string kodiData = std::string(std::getenv("HOME")) + "/.kodi";
  if (const int failed = OpenUpTree(kodiData)) // files left by earlier runs
    Klogf("[kodi-ps5] could not open up permissions of %d items in %s\n", failed,
          kodiData.c_str());
  setenv("KODI_HOME", "/app0/share/kodi", 0);

  // Directory listing check (Kodi's add-on scan starts with this folder).
  {
    const char* path = "/app0/share/kodi/addons";
    struct stat st;
    std::memset(&st, 0, sizeof(st));
    const int sr = stat(path, &st);
    Klogf("[kodi-ps5] stat %s: %d (errno %d) mode %o nlink %u size %lld\n", path, sr,
          sr ? errno : 0, static_cast<unsigned>(st.st_mode), static_cast<unsigned>(st.st_nlink),
          static_cast<long long>(st.st_size));
    const char* known = "/app0/share/kodi/addons/skin.estuary/addon.xml";
    const int kr = stat(known, &st);
    Klogf("[kodi-ps5] stat %s: %d (errno %d) size %lld\n", known, kr, kr ? errno : 0,
          static_cast<long long>(st.st_size));

    const int fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
      Klogf("[kodi-ps5] open dir: errno %d (%s)\n", errno, std::strerror(errno));
    else
    {
      static char buf[64 * 1024];
      errno = 0;
      const int n1 = getdents(fd, buf, sizeof(buf));
      Klogf("[kodi-ps5] getdents -> %d (errno %d %s)\n", n1, n1 < 0 ? errno : 0,
            n1 < 0 ? std::strerror(errno) : "");
      if (n1 > 0)
      {
        const struct dirent* e = reinterpret_cast<const struct dirent*>(buf);
        Klogf("[kodi-ps5]   first record: fileno %u reclen %u type %u namlen %u name '%.*s'\n",
              e->d_fileno, e->d_reclen, e->d_type, e->d_namlen, e->d_namlen, e->d_name);
      }
      lseek(fd, 0, SEEK_SET);
      long base = 0;
      errno = 0;
      const int n2 = getdirentries(fd, buf, sizeof(buf), &base);
      Klogf("[kodi-ps5] getdirentries -> %d (errno %d %s)\n", n2, n2 < 0 ? errno : 0,
            n2 < 0 ? std::strerror(errno) : "");
      close(fd);
    }

    errno = 0;
    DIR* dir = opendir(path);
    if (!dir)
      Klogf("[kodi-ps5] opendir %s: errno %d (%s)\n", path, errno, std::strerror(errno));
    else
    {
      int entries = 0;
      errno = 0;
      while (readdir(dir))
        ++entries;
      const int err = errno;
      closedir(dir);
      Klogf("[kodi-ps5] %s lists %d entries (errno %d)\n", path, entries, err);
    }
  }

  // Logging goes to kodi.log in $HOME/.kodi/temp and, via the platform sink,
  // to klog. Debug-level logging (slow: every line goes through the kernel)
  // only with an empty file "kodi-debug" in the title folder.
  std::vector<char*> args(argv, argv + argc);
  static char debugFlag[] = "--debug";

  // Switches that stay in place (unlike reset/uninstall) are read here, once,
  // and each found switch sets an environment variable that the rest of the
  // port (window system, decoder, renderer, GL profiler, GL driver) reads.
  static const struct
  {
    const char* file;
    const char* env;
  } kSwitches[] = {
      {"/app0/kodi-debug", "KODI_PS5_DEBUG"},
      {"/app0/kodi-swdecode", "KODI_PS5_SWDECODE"},
      {"/app0/kodi-pbo", "KODI_PS5_PBO"},
      {"/app0/kodi-tex2d", "KODI_PS5_TEX2D"},
      {"/app0/kodi-probe-modes", "KODI_PS5_PROBE_MODES"},
  };
  for (const auto& sw : kSwitches)
  {
    if (SwitchPresent(sw.file))
    {
      setenv(sw.env, "1", 1);
      Klogf("[kodi-ps5] switch %s found (%s=1)\n", sw.file + 6, sw.env);
    }
  }
  if (getenv("KODI_PS5_DEBUG"))
  {
    args.push_back(debugFlag);
    Klog("[kodi-ps5] kodi-debug: debug logging on\n");
  }

  CAppParamParser appParamParser;
  appParamParser.Parse(args.data(), static_cast<int>(args.size()));

  Klog("[kodi-ps5] setting up the app environment\n");
  CAppEnvironment::SetUp(appParamParser.GetAppParams());
  Klog("[kodi-ps5] starting XBMC_Run\n");
  const int status = XBMC_Run(true);
  char msg[96];
  std::snprintf(msg, sizeof(msg), "[kodi-ps5] XBMC_Run returned %d\n", status);
  Klog(msg);
  CAppEnvironment::TearDown();
  OpenUpTree(kodiData); // files created during this run (best effort)
  Klog("[kodi-ps5] exiting\n");
  // Leave immediately: returning from main in a title does not always end the
  // process, which leaves the launch screen up with nothing behind it.
  _exit(status);
}
