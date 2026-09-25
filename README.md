# Kodi for PlayStation 5 (homebrew)

A native `ps5` platform port of [Kodi](https://kodi.tv) 22 for jailbroken PS5
consoles, built on the open-source [ps5-payload-dev](https://github.com/ps5-payload-dev)
toolchain and [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl),
and installed as a regular home-screen title via ShadowMountPlus.

> **Unofficial.** This project is not affiliated with or endorsed by Team Kodi /
> the XBMC Foundation or Sony. "Kodi" and the Kodi logo are trademarks of the
> XBMC Foundation.

This repository contains no exploit, no Sony SDK code and no firmware files.
The Sony library prototypes in `overlay/xbmc/platform/ps5/sce/` are clean-room
declarations of the handful of functions Kodi needs.

## Status: early alpha — boots, browses, plays

Tested on firmware 10.01 (etaHEN + ShadowMountPlus + kstuff) and reported
working on 4.03 (ItemzFlow + etaHEN 2.3b).

| Works | Not yet |
| --- | --- |
| Estuary GUI at 1080p60 via OpenGL 4.6 on the PS5 GPU | HEVC Main10 / HDR in hardware (10-bit uses FFmpeg) |
| DualSense navigation (as keyboard events) | Internet access via curl (add-on repository, online streams) |
| Audio (UI sounds, playback) | Python add-ons (Python is not built yet) |
| Video playback: H.264 and HEVC Main in hardware (VideoDec2), everything else in FFmpeg | Binary add-ons (no `dlopen` in a title) |
| SMB2/3 and NFS network sources, UPnP | Listing under the Media tab (the GL driver fails in that sandbox) |
| Thumbnails, databases, settings | Network browsing of `smb://` (enter the server's IP) |

## How it works

Kodi is not forked. This repo is an **overlay**: a `ps5` platform directory
(`overlay/`) copied on top of a stock Kodi checkout, a handful of small Kodi
patches, C shims that fill gaps in what a title's system libraries provide,
and the scripts that set up the cross toolchain, configure, build and package.

| Piece | What it does |
| --- | --- |
| Graphics | OpenGL 4.6 Core via ps5-opengl's Mesa/Gallium build; EGL default display |
| Audio | `AESinkPS5`: 48 kHz stereo on the system audio port; the blocking write is the clock |
| Input | `PS5PadInput`: DualSense polled at 125 Hz, mapped to Kodi keyboard events |
| Network sources | `smb://` on libsmb2 (`xbmc/platform/ps5/filesystem`), NFS on libnfs, UPnP |
| Video decoding | `CDVDVideoCodecPS5` on the hardware decoder (libSceVideodec2, `xbmc/platform/ps5/video`), NV12 into Kodi's GL renderer |
| Logging | every log line goes to klog (`PS5InterfaceForCLog`) as well as `kodi.log` |
| C library gaps | `shims/native-app/`: resolver (`getaddrinfo` on `sceNetResolver`), locale, directory reading, time, thread stacks, … |
| Packaging | ps5-opengl's native-app template: `eboot.bin` + `sce_module/` + `sce_sys/` + Kodi's data in `share/` |

## Requirements

**Host** — Linux, or Windows 11 with WSL2 (Ubuntu 24.04). 8+ cores and ~40 GB
free disk recommended; the toolchain and library set take 1–3 hours to build
once, Kodi itself about as long again. Builds live on the Linux filesystem.

**Console** — a jailbroken PS5 with a HEN (etaHEN or equivalent), ShadowMountPlus
(or equivalent) for folder titles, an FTP server, and `klogsrv` (port 3232) for logs.

## Building

```bash
git clone https://github.com/<you>/kodi-ps5.git ~/kodi-ps5-src
cd ~/kodi-ps5-src

# 0. Toolchain: ps5-payload-sdk + pacbrew libraries + ps5-opengl + native-app template (1–3 h)
bash scripts/00-setup-wsl.sh
#    If a pacbrew package fails on a flaky download, resume from it:
bash scripts/01-pacbrew-resume.sh <package>

# 1. Kodi source
git clone https://github.com/xbmc/xbmc.git ~/kodi

# 2. Host tools and the libraries nobody packages
bash scripts/10-build-host-tools.sh      # TexturePacker + JsonSchemaBuilder, native
bash scripts/11-build-tinyxml.sh         # TinyXML 2.6.2 into the sysroot
bash scripts/12-build-libuuid-shim.sh    # small libuuid (crossguid) + libprocstat stub (exiv2)
bash scripts/13-build-brotli.sh          # brotli for Kodi's internal exiv2
bash scripts/14-sysroot-pc-files.sh      # .pc files pacbrew does not install (sqlite3)
bash scripts/16-build-ffmpeg.sh          # FFmpeg 7.1 (Kodi needs >= 7.1)
bash scripts/17-build-sce-stubs.sh       # link stub for libSceVideodec2 (also run by 20 if missing)

# 3. Configure (applies overlay + patches), build, package
bash scripts/20-configure-kodi.sh        # BUILD_TYPE=Debug for a debug build
cmake --build ~/kodi-ps5-build -j"$(nproc)"
bash scripts/30-deploy.sh                # -> ~/kodi-ps5-stage/app/dist/PPSA99420/
```

Each script documents its environment variables at the top (`TITLE_ID`,
`HEAP_MIB`, `KODI_CATEGORY`, `BUILD_TYPE`, paths).

## Installing

1. Copy the contents of `~/kodi-ps5-stage/app/dist/PPSA99420/` to
   `/data/homebrew/PPSA99420/` on the console over FTP.
2. Re-send ShadowMountPlus so it registers the title, then start Kodi from the home screen.

For updates, usually only `eboot.bin` changes (plus `sce_sys/` when title
metadata changes, `share/` when Kodi's data files change).

Kodi keeps its data in `/data/kodi` when the loader lets a title reach `/data`,
otherwise in the title folder itself (`/data/homebrew/PPSA99420/kodi`).

### Switches

Create an empty file with one of these names in `/data/homebrew/PPSA99420/`
and start Kodi. The console protects files a title creates from outside
processes, so FTP cannot delete Kodi's data — these let Kodi do it:

| File | Effect |
| --- | --- |
| `kodi-reset` | wipe Kodi's data once, then start fresh |
| `kodi-uninstall` | wipe Kodi's data and quit; the title folder can then be deleted over FTP |
| `kodi-debug` | debug-level logging (slower; remove when done) |
| `kodi-swdecode` | software (FFmpeg) video decoding only, no hardware decoder |

### Adding network sources

*Videos → Files → Add videos… → Browse → Add network location…*, protocol
**Windows network (SMB)** or **NFS**. Enter the server's **IP address**
(Windows/NetBIOS names are not resolved). SMB2 and SMB3 are supported, SMB1 is not.

## Debugging

```bash
nc <console-ip> 3232 | tee kodi-klog.txt          # capture while launching
grep -a "\[kodi" kodi-klog.txt | tail -100          # Kodi's log and the port's startup markers
```

A crash prints a report with `# backtrace:`. To turn its addresses into
function names (the build keeps symbols):

```bash
N=$(grep -a -n "# backtrace:" kodi-klog.txt | tail -1 | cut -d: -f1)
sed -n "$((N+1)),$((N+40))p" kodi-klog.txt | grep -a -o "^# [0-9a-f]\{16\}" | awk '{print $2}' |
  while read a; do printf '0x%x\n' $((0x$a - 0x400000 - 1)); done |
  llvm-symbolizer-18 --obj=$HOME/kodi-ps5-stage/app/build/llvm-pie.elf --demangle --inlining=false -p
```

## Repository layout

```
toolchain/ps5-kodi.cmake        wraps the SDK's prospero.cmake, selects CORE_SYSTEM_NAME=ps5
overlay/                        copied onto a Kodi checkout by scripts/20-configure-kodi.sh
  cmake/platform/ps5/           platform selection and dependency exclusions
  cmake/scripts/ps5/            ArchSetup / PathSetup / Install / Macros for the ps5 core system
  cmake/treedata/ps5/           which xbmc/ subdirectories are compiled
  xbmc/platform/ps5/            main.cpp, CPlatformPS5, CPU/GPU info, klog log sink, strptime
    audio/ input/ network/ storage/    AESinkPS5, PS5PadInput, NetworkPS5, PS5StorageProvider
    filesystem/                 smb:// over libsmb2 (SMB2Session, CSMB2File, CSMB2Directory)
    video/                      hardware decoder: CVideoDec2 (libSceVideodec2) + CDVDVideoCodecPS5
    sce/                        clean-room prototypes of the Sony libraries used
  xbmc/windowing/ps5/           CWinSystemPS5, CWinSystemPS5GLContext (EGL)
patches/kodi/                   small Kodi patches (charset, SMB hooks, log sink, …)
patches/                        fix for the native-app template's ELF converter
shims/native-app/               C library gaps, compiled into the title
shims/libuuid/ shims/libprocstat/   minimal libraries for crossguid and exiv2
shims/sce_stubs/                link stub for libSceVideodec2 (the SDK has none)
pacbrew/ffmpeg/                 PKGBUILD for FFmpeg 7.1 (scripts/16)
scripts/                        00 setup · 01 pacbrew resume · 10–17 dependencies · 20 configure · 30 package
title/sce_sys/                  Kodi's icon; see the README there for the switches
```

## Platform notes

Things that differ from a FreeBSD desktop and cost a crash each to find:

- **Missing modules.** Titles do not get `libScePosixForWebKit` or
  `libkernel_sys`; anything only they export jumps to address 0. Their link
  stubs are removed so such symbols fail at link time and get a shim instead.
- **Memory.** A title's *flexible* memory (`mmap`) is only 448 MiB; the malloc
  heap is carved out of *direct* memory instead (`heap_dmem.c`). Thread stacks
  default to tiny sizes and are raised to 1 MiB (`thread_stack.c`).
- **ABI mismatches with the system C library:** `struct lconv` field order
  (own `localeconv`), 16-bit `wchar_t` on the PlayStation compiler target
  (patch 0005), directories that need 64 KiB `getdents` buffers (own
  `opendir`/`readdir`).
- **Sandbox.** `lstat` on the title's own mount points fails (SQLite gets a
  patched system call); files the title creates cannot be deleted from outside.
- **Media category.** Media apps get half the page tables and a stricter
  sandbox in which the GL driver fails (`EGL_BAD_ALLOC`), so Kodi is a Games title.

## Roadmap

1. Hardware decoding: HEVC Main10 and HDR, zero-copy into GL.
2. Internet access (curl/TLS).
3. A real DualSense joystick driver, on-screen keyboard, video sync.
4. Python, binary add-ons, release packaging.

## Contributing

Bug reports with a klog capture (see Debugging) and your firmware/loader
versions are the most useful thing. Pull requests welcome. Keep line endings
LF (enforced by `.gitattributes`); the scripts are bash and break on CRLF.

## Credits

- John Törnblom and contributors — [ps5-payload-dev](https://github.com/ps5-payload-dev)
  SDK, pacbrew-repo, ftpsrv, klogsrv.
- BlackBearReloaded — [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl)
  and the PS5 native-app template.
- Ronnie Sahlberg — [libsmb2](https://github.com/sahlberg/libsmb2).
- ProsperoLight — reference for direct-memory allocation on a PS5 title.
- The PS5 SDL backend, whose observations of the audio and pad libraries the
  `sce/` headers restate.
- Team Kodi — for Kodi itself.

## License

GPL-2.0-or-later, the same license as Kodi (see [LICENSE](LICENSE)). Files under
`overlay/` carry Kodi's standard file header because they are written to be
upstreamed into Kodi's tree.

This project is for running free software on hardware you own. It does not
enable, and must not be used for, copyright infringement of any kind.
