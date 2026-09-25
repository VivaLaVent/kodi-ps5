#!/usr/bin/env bash
# Rebuild the PS5 OpenGL SDK with Kodi's additions (patches/ps5-opengl/) and
# reinstall it into $PS5_OPENGL_PREFIX. Safe to run repeatedly; afterwards
# rebuild Kodi (the SDK is linked into it).
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${WORK:-$HOME/ps5-work}"
SRC="$WORK/ps5-opengl"
export PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}"
export PS5_OPENGL_PREFIX="${PS5_OPENGL_PREFIX:-/opt/ps5-opengl-gl46}"

[ -d "$SRC/src/platform" ] || { echo "!! ps5-opengl source not found at $SRC"; exit 1; }

echo "==> applying Kodi additions to $SRC"
python3 "$HERE/patches/ps5-opengl/kodi-additions.py" "$SRC"

# Our additions live in the runtime (src/platform, src/egl), which the SDK
# installer rebuilds itself; Mesa and the shader compiler are unchanged and
# already built by 00-setup-wsl.sh. So run the installer directly rather than
# "make sdk-gl46", whose host-side compiler self-tests are not needed here.
echo "==> rebuilding the runtime and packaging the SDK"
( cd "$SRC" && bash toolchain/install-ps5-opengl-gl46.sh build/sdk/ps5-opengl-gl46 ) \
  > "$WORK/ps5-opengl-build.log" 2>&1 || {
  echo "!! SDK build failed, last lines of $WORK/ps5-opengl-build.log:"; tail -25 "$WORK/ps5-opengl-build.log"; exit 1; }
( cd "$SRC" && python3 tests/ps5/verify_gl46_link_surface.py ) >> "$WORK/ps5-opengl-build.log" 2>&1 \
  || echo "   note: verify_gl46_link_surface.py reported a problem (see the log)"

echo "==> installing into $PS5_OPENGL_PREFIX"
sudo cp -a "$SRC/build/sdk/ps5-opengl-gl46/." "$PS5_OPENGL_PREFIX/"

if "$PS5_PAYLOAD_SDK/bin/llvm-nm" "$PS5_OPENGL_PREFIX/lib/libPS5OpenGL.a" 2>/dev/null \
     | grep -q " T ps5_opengl_video_out_handle"; then
  echo "SDK ready: ps5_opengl_video_out_handle exported"
else
  echo "!! installed libPS5OpenGL.a lacks ps5_opengl_video_out_handle"; exit 1
fi
