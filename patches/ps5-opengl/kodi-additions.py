#!/usr/bin/env python3
"""Kodi's additions to the ps5-opengl sources, applied by
scripts/18-build-ps5-opengl.sh before the SDK is rebuilt.

Written as anchored insertions rather than a diff so they survive upstream
changes elsewhere in these large files. Each addition is applied once
(recognised by its text) and the script fails loudly if an anchor is missing.
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])

RUNTIME = "src/platform/ps5_agc_native_runtime.c"
SCREEN = "src/gallium/ps5/ps5_screen.c"

# (file, anchor, text, "after" | "before")
ADDITIONS = [
    # 1. The video out handle, for Kodi's display-mode code (status queries,
    #    refresh-rate switching, vblank clock).
    (RUNTIME, "static int runtime_video_handle = -1;\n", """
/* KODI-PS5: video out handle once presentation has opened it (-1 before). */
int ps5_opengl_video_out_handle(void);
int ps5_opengl_video_out_handle(void)
{
    return runtime_video_handle;
}
""", "after"),

    # 2. The driver's built-in draw profiler (PS5_DRAW_PROFILE, on in the
    #    runtime build) reports with printf, which a title discards: send it to
    #    klog with a millisecond timestamp. Also counters for the clear paths.
    (SCREEN, "static uint64_t ps5_prepare_cycles[9], ps5_prepare_calls[9];\n",
     """/* KODI-PS5: profiler output to klog (a title's stdout goes nowhere). */
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
/* KODI-PS5: which clear path each clear takes (GPU depth, CPU depth, GPU
 * color, CPU color), reported with the profile. */
static uint64_t ps5_kodi_clear_paths[4];
#define PS5_KODI_COUNT_CLEAR(path) \\
   __atomic_fetch_add(&ps5_kodi_clear_paths[path], 1, __ATOMIC_RELAXED)
""", "before"),

    (SCREEN, "   printf(\"[ps5-map-caller-anchor] report=%\" PRIxPTR \" overflow=%\" PRIu64 \"\\n\",\n",
     """   /* KODI-PS5 */
   printf("[ps5-kodi-clears] gpu_depth=%" PRIu64 " cpu_depth=%" PRIu64
          " gpu_color=%" PRIu64 " cpu_color=%" PRIu64 "\\n",
          __atomic_load_n(&ps5_kodi_clear_paths[0], __ATOMIC_RELAXED),
          __atomic_load_n(&ps5_kodi_clear_paths[1], __ATOMIC_RELAXED),
          __atomic_load_n(&ps5_kodi_clear_paths[2], __ATOMIC_RELAXED),
          __atomic_load_n(&ps5_kodi_clear_paths[3], __ATOMIC_RELAXED));
""", "before"),

    (SCREEN, """                                      scissor_state, depth, stencil)) {
         if (context->last_draw_status != 0)
            return;
      } else {
""", """#if defined(PS5_NATIVE_TITLE_RUNTIME) && defined(PS5_DRAW_PROFILE)
         PS5_KODI_COUNT_CLEAR(1); /* KODI-PS5: CPU depth/stencil clear */
#endif
""", "after"),

    (SCREEN, """   if (depth_buffers) {
      if (ps5_clear_gpu_depth_stencil(context, depth_buffers, stencil_clear_mask,
                                      scissor_state, depth, stencil)) {
""", """#if defined(PS5_NATIVE_TITLE_RUNTIME) && defined(PS5_DRAW_PROFILE)
         PS5_KODI_COUNT_CLEAR(0); /* KODI-PS5: GPU depth/stencil clear */
#endif
""", "after"),

    (SCREEN, "   if (ps5_clear_gpu_color(context, buffers, color_clear_mask, scissor_state, color)) {\n",
     """#if defined(PS5_NATIVE_TITLE_RUNTIME) && defined(PS5_DRAW_PROFILE)
      PS5_KODI_COUNT_CLEAR(2); /* KODI-PS5: GPU color clear */
#endif
""", "after"),

    (SCREEN, "   /* GPU-only color clears stay ordered in the queue. CPU fallback writes\n",
     """#if defined(PS5_NATIVE_TITLE_RUNTIME) && defined(PS5_DRAW_PROFILE)
   if (buffers & PIPE_CLEAR_COLOR)
      PS5_KODI_COUNT_CLEAR(3); /* KODI-PS5: CPU color clear */
#endif
""", "before"),
]

# Earlier versions of addition 2, replaced by the current one when found.
SUPERSEDED = [
    (SCREEN, """/* KODI-PS5: profiler output to klog (a title's stdout goes nowhere). */
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
static uint64_t ps5_prepare_cycles[9], ps5_prepare_calls[9];
""", "static uint64_t ps5_prepare_cycles[9], ps5_prepare_calls[9];\n"),
]
for rel, old_text, restored in SUPERSEDED:
    path = root / rel
    source = path.read_text()
    if old_text in source:
        path.write_text(source.replace(old_text, restored, 1))
        print(f"  {rel}: removed an earlier Kodi addition (superseded)")

for rel, anchor, text, where in ADDITIONS:
    path = root / rel
    source = path.read_text()
    if text.strip() in source:
        print(f"  {rel}: already applied ({text.strip().splitlines()[0][:60]})")
        continue
    if source.count(anchor) != 1:
        sys.exit(f"!! {rel}: anchor not found exactly once: {anchor.strip()[:80]}")
    replacement = anchor + text if where == "after" else text + anchor
    path.write_text(source.replace(anchor, replacement, 1))
    print(f"  {rel}: applied ({text.strip().splitlines()[0][:60]})")
