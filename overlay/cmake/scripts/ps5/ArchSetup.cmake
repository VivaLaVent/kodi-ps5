# PlayStation 5 (Prospero) architecture setup. Always a cross build from the
# ps5-payload-sdk toolchain file (toolchain/ps5-kodi.cmake in the port repo).

if(NOT CMAKE_TOOLCHAIN_FILE)
  message(FATAL_ERROR "The ps5 platform must be built with -DCMAKE_TOOLCHAIN_FILE=.../ps5-kodi.cmake")
endif()

set(CORE_MAIN_SOURCE ${CMAKE_SOURCE_DIR}/xbmc/platform/ps5/main.cpp)

# TARGET_FREEBSD is deliberately kept: the PS5 userland is FreeBSD-derived and
# most of Kodi's #if TARGET_FREEBSD branches (sysctl, kqueue, no /proc) are the
# ones that apply. If a specific branch pulls in something the SDK lacks,
# guard that spot with !defined(TARGET_PS5) rather than dropping the define.
# HAS_FILESYSTEM_SMB2: smb:// on libsmb2 (xbmc/platform/ps5/filesystem) -
# Kodi's usual SMB backend needs libsmbclient, which the console lacks.
set(ARCH_DEFINES -DTARGET_POSIX -DTARGET_FREEBSD -DTARGET_PS5 -DHAS_FILESYSTEM_SMB2)
set(SYSTEM_DEFINES -D__STDC_CONSTANT_MACROS -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64)
set(PLATFORM_DIR platform/ps5)
set(PLATFORMDEFS_DIR platform/posix)

set(ARCH x86_64-ps5)
set(CPU x86_64)
set(NEON False)

# Nothing we build here runs on the WSL/Linux host.
set(HOST_CAN_EXECUTE_TARGET FALSE)
# OFF: prefer the pacbrew-repo libraries already in the sysroot (Kodi's
# dependent_option() defaults every ENABLE_INTERNAL_* to this value; ON would
# also pull in internal curl, which needs brotli/nghttp2 we do not have).
# Libraries pacbrew does not ship are listed explicitly below.
set(USE_INTERNAL_LIBS OFF)

# The SDK's compiler wrapper already drives prospero-lld; never let Kodi
# substitute a host linker.
set(ENABLE_GOLD OFF CACHE BOOL "No gold on PS5" FORCE)
set(ENABLE_LLD  OFF CACHE BOOL "lld comes from the SDK wrapper" FORCE)
set(ENABLE_MOLD OFF CACHE BOOL "No mold on PS5" FORCE)

# Console realities for the first bring-up. These are defaults, not walls:
# pass -DENABLE_<X>=ON on the command line to override.
set(ENABLE_PYTHON      OFF CACHE BOOL "No CPython port for the payload SDK yet (phase 4)")
set(ENABLE_OPTICAL     OFF CACHE BOOL "No disc access from homebrew")
set(ENABLE_DVDCSS      OFF CACHE BOOL "No disc access from homebrew")
set(ENABLE_AIRTUNES    OFF CACHE BOOL "shairplay not ported")
set(ENABLE_EVENTCLIENTS OFF CACHE BOOL "Event clients are a desktop feature")
set(ENABLE_TESTING     OFF CACHE BOOL "Tests cannot run on the host")
set(ENABLE_UPNP        ON  CACHE BOOL "Platinum builds fine on POSIX")

# Everything pacbrew-repo does not ship is built by Kodi's own ExternalProject
# recipes with the same cross toolchain.
# FMT is in pacbrew, but its CMake package config does not resolve from the
# host and Kodi then silently ends up without a kodi::Fmt target; spdlog is
# built internally anyway and must match, so build both.
foreach(_dep CROSSGUID EXIV2 FLATBUFFERS FMT FSTRCMP LZO2 NLOHMANNJSON PCRE2 SPDLOG TAGLIB)
  set(ENABLE_INTERNAL_${_dep} ON CACHE BOOL "PS5: not in pacbrew-repo, build internally" FORCE)
endforeach()
unset(_dep)

# Optional: the ps5-opengl native-app template routes malloc through an
# app-owned heap (native-app/app_heap.c + linker wraps). Its default budget
# (128 MiB) is far too small for Kodi; if you adopt it, raise the constant in
# app_heap.c to at least 1536 MiB first.
set(PS5_APP_HEAP_SOURCE "" CACHE FILEPATH "Path to ps5-opengl native-app/app_heap.c (leave empty to use libc malloc)")
if(PS5_APP_HEAP_SOURCE)
  list(APPEND CORE_MAIN_SOURCE ${PS5_APP_HEAP_SOURCE})
  add_link_options(-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free
                   -Wl,--wrap=posix_memalign -Wl,--wrap=malloc_usable_size)
endif()

# Sony library stubs resolved by the payload runtime linker at load time.
# libSceAudioOut / libScePad / libSceUserService back the sink and pad input.
# -lprocstat: stub from shims/libprocstat (scripts/12); exiv2 references
# libprocstat on any __FreeBSD__ target for its library-info dump.
set(SYSTEM_LDFLAGS -lSceAudioOut -lScePad -lSceUserService -lSceNetCtl -lprocstat -lsmb2)

list(APPEND AUDIO_BACKENDS_LIST "ps5")
