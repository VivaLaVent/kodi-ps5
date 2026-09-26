#!/usr/bin/env python3
"""Kodi's additions to the ps5-opengl sources, applied by
scripts/18-build-ps5-opengl.sh before the SDK is rebuilt.

Written as anchored insertions rather than a diff so they survive upstream
changes elsewhere in these large files. Each addition is applied once
(marked with KODI-PS5) and the script fails loudly if an anchor is missing.
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])

ADDITIONS = [
    # 2. The driver's built-in draw profiler (PS5_DRAW_PROFILE, on in the
    #    runtime build) reports with printf, which a title discards: send it to
    #    klog with a millisecond timestamp. Every 10,000 draws it lists the
    #    cycles spent per phase (draw prep, descriptors, GPU drain, clear,
    #    map/unmap) and the busiest buffer-map call sites.
    (
        "src/gallium/ps5/ps5_screen.c",
        "static uint64_t ps5_prepare_cycles[9], ps5_prepare_calls[9];\n",
        None,  # inserted before the anchor, see PREPEND below
    ),
    # 1. The video out handle, for Kodi's display-mode code (read-only
    #    status queries now; refresh-rate switching and vsync later).
    (
        "src/platform/ps5_agc_native_runtime.c",
        "static int runtime_video_handle = -1;\n",
        """
/* KODI-PS5: video out handle once presentation has opened it (-1 before). */
int ps5_opengl_video_out_handle(void);
int ps5_opengl_video_out_handle(void)
{
    return runtime_video_handle;
}
""",
    ),
]

PREPEND = {
    "src/gallium/ps5/ps5_screen.c": """/* KODI-PS5: profiler output to klog (a title's stdout goes nowhere). */
#include <stdarg.h>
int sceKernelDebugOutText(int channel, const char *text);
uint64_t sceKernelGetProcessTime(void);
static int ps5_kodi_klog_printf(const char *format, ...)
   __attribute__((format(printf, 1, 2)));
static int ps5_kodi_klog_printf(const char *format, ...)
{
   char line[512];
   const int prefix = snprintf(line, sizeof(line), "[ps5-gl %.3f] ",
                               (double)sceKernelGetProcessTime() / 1000.0);
   va_list args;
   va_start(args, format);
   const int written = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix,
                                 format, args);
   va_end(args);
   sceKernelDebugOutText(0, line);
   return written;
}
#define printf ps5_kodi_klog_printf
""",
}

for rel, anchor, addition in ADDITIONS:
    path = root / rel
    text = path.read_text()
    insert = addition if addition is not None else PREPEND[rel]
    if insert.strip() in text:
        print(f"  {rel}: already applied")
        continue
    if text.count(anchor) != 1:
        sys.exit(f"!! {rel}: anchor not found exactly once: {anchor.strip()}")
    if addition is None:
        text = text.replace(anchor, insert + anchor, 1)
    else:
        text = text.replace(anchor, anchor + insert, 1)
    path.write_text(text)
    print(f"  {rel}: applied")
