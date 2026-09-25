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

for rel, anchor, addition in ADDITIONS:
    path = root / rel
    text = path.read_text()
    if addition.strip() in text:
        print(f"  {rel}: already applied")
        continue
    if text.count(anchor) != 1:
        sys.exit(f"!! {rel}: anchor not found exactly once: {anchor.strip()}")
    path.write_text(text.replace(anchor, anchor + addition, 1))
    print(f"  {rel}: applied")
