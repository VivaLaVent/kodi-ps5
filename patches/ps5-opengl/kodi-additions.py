#!/usr/bin/env python3
"""Kodi's additions to the ps5-opengl sources, applied by
scripts/18-build-ps5-opengl.sh before the SDK is rebuilt.

One addition remains: the driver exports its video out handle, which Kodi's
window system needs (refresh rate, vblank clock, VRR). Diagnostics that
earlier versions added (driver profile to klog, clear-path counters) are
removed from the source tree when found, so the tree converges to upstream
plus this one function.
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])

RUNTIME = "src/platform/ps5_agc_native_runtime.c"
SCREEN = "src/gallium/ps5/ps5_screen.c"

# (file, anchor, text inserted after the anchor)
ADDITIONS = [
    (RUNTIME, 'static int runtime_video_handle = -1;\n', '\n/* KODI-PS5: video out handle once presentation has opened it (-1 before). */\nint ps5_opengl_video_out_handle(void);\nint ps5_opengl_video_out_handle(void)\n{\n    return runtime_video_handle;\n}\n'),
]

# Texts earlier versions inserted; removed when present.
REMOVED = [
    (SCREEN, '/* KODI-PS5: profiler output to klog (a title\'s stdout goes nowhere). */\n#include <stdarg.h>\nint sceKernelDebugOutText(int channel, const char *text);\nuint64_t sceKernelGetProcessTime(void);\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n   __attribute__((format(printf, 1, 2)));\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n{\n   char line[512];\n   const int prefix = snprintf(line, sizeof(line), "[ps5-gl %.3f] ",\n                               (double)sceKernelGetProcessTime() / 1000.0);\n   va_list args;\n   va_start(args, format);\n   const int written = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix,\n                                 format, args);\n   va_end(args);\n   sceKernelDebugOutText(0, line);\n   return written;\n}\n#define printf ps5_kodi_klog_printf\n'),
    (SCREEN, '/* KODI-PS5: profiler output to klog (a title\'s stdout goes nowhere). */\n#include <stdarg.h>\nint sceKernelDebugOutText(int channel, const char *text);\nuint64_t sceKernelGetProcessTime(void);\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n   __attribute__((format(printf, 1, 2)));\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n{\n   char line[512];\n   const int prefix = snprintf(line, sizeof(line), "[ps5-gl %.3f] ",\n                               (double)sceKernelGetProcessTime() / 1000.0);\n   va_list args;\n   va_start(args, format);\n   const int written = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix,\n                                 format, args);\n   va_end(args);\n   sceKernelDebugOutText(0, line);\n   return written;\n}\n#define printf ps5_kodi_klog_printf\n/* KODI-PS5: which clear path each clear takes (GPU depth, CPU depth, GPU\n * color, CPU color), reported with the profile. */\nstatic uint64_t ps5_kodi_clear_paths[4];\n#define PS5_KODI_COUNT_CLEAR(path) \\\n   __atomic_fetch_add(&ps5_kodi_clear_paths[path], 1, __ATOMIC_RELAXED)\n'),
    (SCREEN, '/* KODI-PS5: profiler output to klog (a title\'s stdout goes nowhere). */\n#include <stdarg.h>\nint sceKernelDebugOutText(int channel, const char *text);\nuint64_t sceKernelGetProcessTime(void);\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n   __attribute__((format(printf, 1, 2)));\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n{\n   char line[512];\n   const int prefix = snprintf(line, sizeof(line), "[ps5-gl %.3f] ",\n                               (double)sceKernelGetProcessTime() / 1000.0);\n   va_list args;\n   va_start(args, format);\n   const int written = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix,\n                                 format, args);\n   va_end(args);\n   sceKernelDebugOutText(0, line);\n   return written;\n}\n#define printf ps5_kodi_klog_printf\n/* KODI-PS5: which path each clear takes; reported every 1000 clears. */\nstatic uint64_t ps5_kodi_clear_paths[4];\nstatic void ps5_kodi_count_clear(unsigned path) __attribute__((unused));\nstatic void ps5_kodi_count_clear(unsigned path)\n{\n   __atomic_fetch_add(&ps5_kodi_clear_paths[path], 1, __ATOMIC_RELAXED);\n   uint64_t total = 0;\n   for (unsigned i = 0; i < 4; ++i)\n      total += __atomic_load_n(&ps5_kodi_clear_paths[i], __ATOMIC_RELAXED);\n   if (total % 1000 == 0)\n      printf("[ps5-kodi-clears] gpu_depth=%" PRIu64 " cpu_depth=%" PRIu64\n             " gpu_color=%" PRIu64 " cpu_color=%" PRIu64 "\\n",\n             ps5_kodi_clear_paths[0], ps5_kodi_clear_paths[1],\n             ps5_kodi_clear_paths[2], ps5_kodi_clear_paths[3]);\n}\n'),
    (SCREEN, '/* KODI-PS5: profiler output to klog (a title\'s stdout goes nowhere). */\n#include <stdarg.h>\n#include <unistd.h>\nint sceKernelDebugOutText(int channel, const char *text);\nuint64_t sceKernelGetProcessTime(void);\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n   __attribute__((format(printf, 1, 2)));\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n{\n   /* only with Kodi\'s kodi-debug switch in the title folder */\n   static int enabled = -1;\n   if (enabled < 0)\n      enabled = access("/app0/kodi-debug", F_OK) == 0;\n   if (!enabled)\n      return 0;\n   char line[512];\n   const int prefix = snprintf(line, sizeof(line), "[ps5-gl %.3f] ",\n                               (double)sceKernelGetProcessTime() / 1000.0);\n   va_list args;\n   va_start(args, format);\n   const int written = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix,\n                                 format, args);\n   va_end(args);\n   sceKernelDebugOutText(0, line);\n   return written;\n}\n#define printf ps5_kodi_klog_printf\n/* KODI-PS5: which path each clear takes; reported every 1000 clears. */\nstatic uint64_t ps5_kodi_clear_paths[4];\nstatic void ps5_kodi_count_clear(unsigned path) __attribute__((unused));\nstatic void ps5_kodi_count_clear(unsigned path)\n{\n   __atomic_fetch_add(&ps5_kodi_clear_paths[path], 1, __ATOMIC_RELAXED);\n   uint64_t total = 0;\n   for (unsigned i = 0; i < 4; ++i)\n      total += __atomic_load_n(&ps5_kodi_clear_paths[i], __ATOMIC_RELAXED);\n   if (total % 1000 == 0)\n      printf("[ps5-kodi-clears] gpu_depth=%" PRIu64 " cpu_depth=%" PRIu64\n             " gpu_color=%" PRIu64 " cpu_color=%" PRIu64 "\\n",\n             ps5_kodi_clear_paths[0], ps5_kodi_clear_paths[1],\n             ps5_kodi_clear_paths[2], ps5_kodi_clear_paths[3]);\n}\n'),
    (SCREEN, '/* KODI-PS5: profiler output to klog (a title\'s stdout goes nowhere). */\n#include <stdarg.h>\n#include <stdlib.h>\nint sceKernelDebugOutText(int channel, const char *text);\nuint64_t sceKernelGetProcessTime(void);\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n   __attribute__((format(printf, 1, 2)));\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n{\n   /* only with Kodi\'s kodi-debug switch (read by Kodi at start-up) */\n   static int enabled = -1;\n   if (enabled < 0)\n      enabled = getenv("KODI_PS5_DEBUG") != NULL;\n   if (!enabled)\n      return 0;\n   char line[512];\n   const int prefix = snprintf(line, sizeof(line), "[ps5-gl %.3f] ",\n                               (double)sceKernelGetProcessTime() / 1000.0);\n   va_list args;\n   va_start(args, format);\n   const int written = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix,\n                                 format, args);\n   va_end(args);\n   sceKernelDebugOutText(0, line);\n   return written;\n}\n#define printf ps5_kodi_klog_printf\n/* KODI-PS5: which path each clear takes; reported every 1000 clears. */\nstatic uint64_t ps5_kodi_clear_paths[4];\nstatic void ps5_kodi_count_clear(unsigned path) __attribute__((unused));\nstatic void ps5_kodi_count_clear(unsigned path)\n{\n   __atomic_fetch_add(&ps5_kodi_clear_paths[path], 1, __ATOMIC_RELAXED);\n   uint64_t total = 0;\n   for (unsigned i = 0; i < 4; ++i)\n      total += __atomic_load_n(&ps5_kodi_clear_paths[i], __ATOMIC_RELAXED);\n   if (total % 1000 == 0)\n      printf("[ps5-kodi-clears] gpu_depth=%" PRIu64 " cpu_depth=%" PRIu64\n             " gpu_color=%" PRIu64 " cpu_color=%" PRIu64 "\\n",\n             ps5_kodi_clear_paths[0], ps5_kodi_clear_paths[1],\n             ps5_kodi_clear_paths[2], ps5_kodi_clear_paths[3]);\n}\n'),
    (SCREEN, '#if defined(PS5_NATIVE_TITLE_RUNTIME) && defined(PS5_DRAW_PROFILE)\n         ps5_kodi_count_clear(0); /* KODI-PS5: GPU depth/stencil clear */\n#endif\n'),
    (SCREEN, '#if defined(PS5_NATIVE_TITLE_RUNTIME) && defined(PS5_DRAW_PROFILE)\n         ps5_kodi_count_clear(1); /* KODI-PS5: CPU depth/stencil clear */\n#endif\n'),
    (SCREEN, '#if defined(PS5_NATIVE_TITLE_RUNTIME) && defined(PS5_DRAW_PROFILE)\n      ps5_kodi_count_clear(2); /* KODI-PS5: GPU color clear */\n#endif\n'),
    (SCREEN, '#if defined(PS5_NATIVE_TITLE_RUNTIME) && defined(PS5_DRAW_PROFILE)\n   if (buffers & PIPE_CLEAR_COLOR)\n      ps5_kodi_count_clear(3); /* KODI-PS5: CPU color clear */\n#endif\n'),
]

for rel, text in REMOVED:
    path = root / rel
    source = path.read_text()
    if text in source:
        path.write_text(source.replace(text, "", 1))
        print(f"  {rel}: removed an earlier Kodi diagnostic ({text.strip().splitlines()[0][:50]})")

failed = False
for rel, anchor, text in ADDITIONS:
    path = root / rel
    source = path.read_text()
    if text.strip() in source:
        print(f"  {rel}: already applied (video out handle export)")
        continue
    if source.count(anchor) != 1:
        print(f"!! {rel}: anchor not found exactly once: {anchor.strip()[:80]}")
        failed = True
        continue
    path.write_text(source.replace(anchor, anchor + text, 1))
    print(f"  {rel}: applied (video out handle export)")

leftover = [t for rel, t in REMOVED if t in (root / rel).read_text()]
if "KODI-PS5: profiler" in (root / SCREEN).read_text() or leftover:
    print("!! ps5_screen.c still carries Kodi diagnostics that no known version matches")
    failed = True
sys.exit(1 if failed else 0)
