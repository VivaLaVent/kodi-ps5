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
export PS5_OPENGL_PREFIX="${PS5_OPENGL_PREFIX:-/opt/ps5-opengl-gl46}"

[ -f "$KODI_SRC/version.txt" ] || { echo "Kodi source not found at $KODI_SRC"; exit 1; }

echo "==> applying overlay to $KODI_SRC"
cp -a "$HERE/overlay/." "$KODI_SRC/"

# Small patches to Kodi's own build files that the overlay cannot express.
# Each is applied once (patch -N refuses already-applied hunks).
for p in "$HERE"/patches/kodi/*.patch; do
  [ -f "$p" ] || continue
  ( cd "$KODI_SRC" && patch -p1 -N -r - --silent < "$p" ) || true
done

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
