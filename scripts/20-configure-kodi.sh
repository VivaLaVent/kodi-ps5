#!/usr/bin/env bash
# Apply the overlay to a Kodi checkout and configure the PS5 build.
#
#   KODI_SRC   Kodi source tree (master / 22.x)         default ~/kodi
#   BUILD      build directory                          default ~/kodi-ps5-build
#   NATIVE     host tools from 10-build-host-tools.sh   default ~/kodi-ps5-native
#   BUILD_TYPE Release (default; built with -O2 -g: optimised, symbols kept
#              for crash backtraces) or Debug (slow, extra assertions).
#              Not RelWithDebInfo: Kodi's dependency helpers then expect the
#              debug-named bundled libraries but link the release ones.
#
# Re-run after editing the overlay; it only copies files, so removing a file
# from the overlay does not remove it from the tree.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KODI_SRC="${KODI_SRC:-$HOME/kodi}"
BUILD="${BUILD:-$HOME/kodi-ps5-build}"
NATIVE="${NATIVE:-$HOME/kodi-ps5-native}"
export PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}"

# our Sony link stubs (the SDK has none for these) must be in the sysroot
for stub in "$HERE"/shims/sce_stubs/*.c; do
  [ -f "$PS5_PAYLOAD_SDK/target/lib/$(basename "$stub" .c).so" ] || {
    bash "$HERE/scripts/17-build-sce-stubs.sh" || { echo "!! stub build failed"; exit 1; }
    break; }
done
export PS5_OPENGL_PREFIX="${PS5_OPENGL_PREFIX:-/opt/ps5-opengl-gl46}"

[ -f "$KODI_SRC/version.txt" ] || { echo "Kodi source not found at $KODI_SRC"; exit 1; }

# Kodi patches always start from Kodi's own files: restore every file a patch
# touches from git, so a patch that changed between rounds (or one applied
# only partly before) cannot leave a mixed file behind.
PATCHED_FILES=$(grep -h '^+++ b/' "$HERE"/patches/kodi/*.patch | sed 's|^+++ b/||; s|\t.*||' | sort -u)
if git -C "$KODI_SRC" rev-parse --git-dir >/dev/null 2>&1; then
  echo "==> restoring the $(echo "$PATCHED_FILES" | wc -l) Kodi files our patches change"
  for f in $PATCHED_FILES; do
    git -C "$KODI_SRC" checkout -- "$f" 2>/dev/null || echo "   note: $f is not tracked by git"
  done
  CLEAN_BASE=1
else
  echo "   note: $KODI_SRC is not a git checkout: patches go on top of the current files"
  CLEAN_BASE=0
fi

echo "==> applying overlay to $KODI_SRC"
cp -a "$HERE/overlay/." "$KODI_SRC/"

# Small patches to Kodi's own files that the overlay cannot express, in order.
for p in "$HERE"/patches/kodi/*.patch; do
  [ -f "$p" ] || continue
  if [ "$CLEAN_BASE" = 1 ]; then
    ( cd "$KODI_SRC" && patch -p1 --forward --no-backup-if-mismatch -r - --silent < "$p" ) || {
      echo "!! Kodi patch $(basename "$p") does not apply to this Kodi revision"; exit 1; }
  else
    ( cd "$KODI_SRC" && patch -p1 -N --no-backup-if-mismatch -r - --silent < "$p" ) || true
  fi
done
echo "==> $(ls "$HERE"/patches/kodi/*.patch | wc -l) Kodi patches applied"

echo "==> configuring in $BUILD"
mkdir -p "$BUILD"

# pkg-config for two worlds: Kodi installs its internally built libraries into
# $BUILD/build and its meson sub-builds (libdvdnav) locate them through
# pkg-config, while pacbrew's .pc files need the SDK wrapper, which hard-wires
# the sysroot search path and would hide the build tree. Try the build tree
# first (no sysroot prefixing), then fall back to the SDK wrapper.
cat > "$BUILD/kodi-pkg-config" <<WRAP
#!/usr/bin/env bash
# Stage 1 only when called by meson for a Kodi sub-build: meson passes the
# cross file's pkg_config_libdir (inside the build tree) in PKG_CONFIG_LIBDIR.
# Kodi's own configure must keep seeing the sysroot only, otherwise libraries
# built in a previous run look "external" and their dependants stop building.
# lib/pkgconfig: CMake/autotools installs; libdata/pkgconfig: meson installs
# for a FreeBSD host machine.
DEPENDS_PC="$BUILD/build/lib/pkgconfig:$BUILD/build/libdata/pkgconfig"
case "\${PKG_CONFIG_LIBDIR:-}" in
  "$BUILD/build/"*)
    env -u PKG_CONFIG_SYSROOT_DIR PKG_CONFIG_LIBDIR="\$DEPENDS_PC" PKG_CONFIG_PATH= pkg-config "\$@" 2>/dev/null && exit 0
    ;;
esac
exec "$PS5_PAYLOAD_SDK/bin/prospero-pkg-config" "\$@"
WRAP
chmod +x "$BUILD/kodi-pkg-config"
cmake -S "$KODI_SRC" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$HERE/toolchain/ps5-kodi.cmake" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}" \
  -DCMAKE_C_FLAGS_RELEASE="-O2 -g -DNDEBUG" \
  -DCMAKE_CXX_FLAGS_RELEASE="-O2 -g -DNDEBUG" \
  -DCMAKE_INSTALL_PREFIX=/app0 \
  -DWITH_TEXTUREPACKER="$NATIVE/bin" \
  -DWITH_JSONSCHEMABUILDER="$NATIVE/bin" \
  -DNATIVEPREFIX="$NATIVE" \
  -DPKG_CONFIG_EXECUTABLE="$BUILD/kodi-pkg-config" \
  -DINTERNAL_TEXTUREPACKER_INSTALLABLE=FALSE \
  -DENABLE_INTERNAL_FFMPEG=OFF \
  -DENABLE_PYTHON=OFF \
  -DENABLE_TESTING=OFF \
  -DVERBOSE_FIND=ON \
  "$@"

cat <<MSG

Configured. Build with:
  cmake --build $BUILD -j\$(nproc)
Then stage + deploy with scripts/30-deploy.sh
MSG
