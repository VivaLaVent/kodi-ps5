#!/usr/bin/env bash
# Link stubs for Sony system libraries the payload SDK does not ship a stub
# for, installed next to the SDK's own stubs in $PS5_PAYLOAD_SDK/target/lib.
#   libSceVideodec2  hardware video decoder (xbmc/platform/ps5/video)
# Run automatically by 20-configure-kodi.sh when a stub is missing.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
export PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}"
DEST="$PS5_PAYLOAD_SDK/target/lib"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

for stub in "$HERE"/shims/sce_stubs/*.c; do
  name="$(basename "$stub" .c)"
  "$PS5_PAYLOAD_SDK/bin/prospero-clang" -ffreestanding -fno-builtin -nostdlib -fPIC \
    -c -o "$WORK/$name.o" "$stub"
  "$PS5_PAYLOAD_SDK/bin/prospero-lld" -m elf_x86_64 -shared -soname "$name.sprx" \
    -o "$WORK/$name.so" "$WORK/$name.o"
  sudo install -m644 "$WORK/$name.so" "$DEST/"
  echo "installed $name stub into $DEST"
done
