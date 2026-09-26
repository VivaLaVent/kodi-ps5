#!/usr/bin/env python3
"""Kodi's additions to the ps5-opengl sources, applied by
scripts/18-build-ps5-opengl.sh before the SDK is rebuilt.

Anchored insertions rather than a diff, so they survive upstream changes
elsewhere in these large files. Each addition is applied once (recognised by
its text). A missing anchor stops the build only for required additions;
diagnostic ones are skipped with a note (driver revisions differ).
"""
import sys
from pathlib import Path

root = Path(sys.argv[1])

RUNTIME = "src/platform/ps5_agc_native_runtime.c"
SCREEN = "src/gallium/ps5/ps5_screen.c"
PROFILE_ANCHOR = "static uint64_t ps5_prepare_cycles[9], ps5_prepare_calls[9];\n"
GUARD = "#if defined(PS5_NATIVE_TITLE_RUNTIME) && defined(PS5_DRAW_PROFILE)\n"

KLOG_BLOCK = """/* KODI-PS5: profiler output to klog (a title's stdout goes nowhere). */
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
/* KODI-PS5: which path each clear takes; reported every 1000 clears. */
static uint64_t ps5_kodi_clear_paths[4];
static void ps5_kodi_count_clear(unsigned path) __attribute__((unused));
static void ps5_kodi_count_clear(unsigned path)
{
   __atomic_fetch_add(&ps5_kodi_clear_paths[path], 1, __ATOMIC_RELAXED);
   uint64_t total = 0;
   for (unsigned i = 0; i < 4; ++i)
      total += __atomic_load_n(&ps5_kodi_clear_paths[i], __ATOMIC_RELAXED);
   if (total % 1000 == 0)
      printf("[ps5-kodi-clears] gpu_depth=%" PRIu64 " cpu_depth=%" PRIu64
             " gpu_color=%" PRIu64 " cpu_color=%" PRIu64 "\\n",
             ps5_kodi_clear_paths[0], ps5_kodi_clear_paths[1],
             ps5_kodi_clear_paths[2], ps5_kodi_clear_paths[3]);
}
"""

def counter(path, what, indent):
    return (GUARD + f"{indent}ps5_kodi_count_clear({path}); /* KODI-PS5: {what} */\n#endif\n")

# (file, anchor, text, "after" | "before", required)
ADDITIONS = [
    # 1. The video out handle, for Kodi's display-mode code (status queries,
    #    refresh-rate switching, vblank clock). Kodi cannot link without it.
    (RUNTIME, "static int runtime_video_handle = -1;\n", """
/* KODI-PS5: video out handle once presentation has opened it (-1 before). */
int ps5_opengl_video_out_handle(void);
int ps5_opengl_video_out_handle(void)
{
    return runtime_video_handle;
}
""", "after", True),

    # 2. The driver's built-in draw profiler (PS5_DRAW_PROFILE, on in the
    #    runtime build) reports with printf, which a title discards: send it to
    #    klog with a millisecond timestamp; plus clear-path counters.
    (SCREEN, PROFILE_ANCHOR, KLOG_BLOCK, "before", False),

    # 3. Clear-path counters: GPU / CPU depth-stencil, GPU / CPU color.
    (SCREEN, """   if (depth_buffers) {
      if (ps5_clear_gpu_depth_stencil(context, depth_buffers, stencil_clear_mask,
                                      scissor_state, depth, stencil)) {
""", counter(0, "GPU depth/stencil clear", "         "), "after", False),
    (SCREEN, """      } else {
         ps5_draw_batch_drain_buffer(resource ? &resource->base : NULL);
         if (!ps5_clear_depth_stencil(context, depth_buffers, stencil_clear_mask,
""", counter(1, "CPU depth/stencil clear", "         "), "before", False),
    (SCREEN, "   if (ps5_clear_gpu_color(context, buffers, color_clear_mask, scissor_state, color)) {\n",
     counter(2, "GPU color clear", "      "), "after", False),
    (SCREEN, "   /* GPU-only color clears stay ordered in the queue. CPU fallback writes\n",
     GUARD + "   if (buffers & PIPE_CLEAR_COLOR)\n      ps5_kodi_count_clear(3); /* KODI-PS5: CPU color clear */\n#endif\n",
     "before", False),
]

# Earlier versions of addition 2 (replaced by the current one when found).
SUPERSEDED = [
    (SCREEN, '/* KODI-PS5: profiler output to klog (a title\'s stdout goes nowhere). */\n#include <stdarg.h>\nint sceKernelDebugOutText(int channel, const char *text);\nuint64_t sceKernelGetProcessTime(void);\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n   __attribute__((format(printf, 1, 2)));\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n{\n   char line[512];\n   const int prefix = snprintf(line, sizeof(line), "[ps5-gl %.3f] ",\n                               (double)sceKernelGetProcessTime() / 1000.0);\n   va_list args;\n   va_start(args, format);\n   const int written = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix,\n                                 format, args);\n   va_end(args);\n   sceKernelDebugOutText(0, line);\n   return written;\n}\n#define printf ps5_kodi_klog_printf\n'),
    (SCREEN, '/* KODI-PS5: profiler output to klog (a title\'s stdout goes nowhere). */\n#include <stdarg.h>\nint sceKernelDebugOutText(int channel, const char *text);\nuint64_t sceKernelGetProcessTime(void);\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n   __attribute__((format(printf, 1, 2)));\nstatic int ps5_kodi_klog_printf(const char *format, ...)\n{\n   char line[512];\n   const int prefix = snprintf(line, sizeof(line), "[ps5-gl %.3f] ",\n                               (double)sceKernelGetProcessTime() / 1000.0);\n   va_list args;\n   va_start(args, format);\n   const int written = vsnprintf(line + prefix, sizeof(line) - (size_t)prefix,\n                                 format, args);\n   va_end(args);\n   sceKernelDebugOutText(0, line);\n   return written;\n}\n#define printf ps5_kodi_klog_printf\n/* KODI-PS5: which clear path each clear takes (GPU depth, CPU depth, GPU\n * color, CPU color), reported with the profile. */\nstatic uint64_t ps5_kodi_clear_paths[4];\n#define PS5_KODI_COUNT_CLEAR(path) \\\n   __atomic_fetch_add(&ps5_kodi_clear_paths[path], 1, __ATOMIC_RELAXED)\n'),
]
for rel, old_text in SUPERSEDED:
    path = root / rel
    source = path.read_text()
    if old_text + PROFILE_ANCHOR in source:
        path.write_text(source.replace(old_text + PROFILE_ANCHOR, PROFILE_ANCHOR, 1))
        print(f"  {rel}: removed an earlier Kodi addition (superseded)")

failed = False
for rel, anchor, text, where, required in ADDITIONS:
    path = root / rel
    source = path.read_text()
    label = text.strip().splitlines()[-2 if text.startswith(GUARD) else 0][:64]
    if text.strip() in source:
        print(f"  {rel}: already applied ({label})")
        continue
    if source.count(anchor) != 1:
        if required:
            print(f"!! {rel}: anchor not found exactly once: {anchor.strip()[:80]}")
            failed = True
        else:
            print(f"  {rel}: skipped, anchor differs in this revision ({label})")
        continue
    replacement = anchor + text if where == "after" else text + anchor
    path.write_text(source.replace(anchor, replacement, 1))
    print(f"  {rel}: applied ({label})")
sys.exit(1 if failed else 0)
