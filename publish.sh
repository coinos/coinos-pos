#!/usr/bin/env bash
# Build the firmware and publish it to the coinos-ui site as the "latest
# release" the web flasher (coinos.io/pos) offers. Writes:
#   <coinos-ui>/static/firmware/{bootloader,partitions,boot_app0,coinos-pos}.bin
#   <coinos-ui>/static/firmware/manifest.json
# Usage: ./publish.sh            (build + publish)
#        SKIP_BUILD=1 ./publish.sh   (publish whatever is in build/)
set -euo pipefail
cd "$(dirname "$0")"

UI=${COINOS_UI:-$HOME/coinos-ui}
OUT="$UI/static/firmware"
FQBN=esp32:esp32:esp32c3:CDCOnBoot=cdc
CORE=$HOME/.arduino15/packages/esp32/hardware/esp32/3.0.7

if [ -z "${SKIP_BUILD:-}" ]; then
  arduino-cli compile -b "$FQBN" --build-property build.partitions=min_spiffs --build-path=build coinos-pos.ino
fi

mkdir -p "$OUT"
cp build/coinos-pos.ino.bootloader.bin "$OUT/bootloader.bin"
cp build/coinos-pos.ino.partitions.bin "$OUT/partitions.bin"
cp "$CORE/tools/partitions/boot_app0.bin" "$OUT/boot_app0.bin"
cp build/coinos-pos.ino.bin            "$OUT/coinos-pos.bin"

COMMIT=$(git rev-parse --short HEAD)
DIRTY=$(git diff --quiet && echo "" || echo "-dirty")
VERSION="$(git log -1 --format=%cd --date=format:%Y.%m.%d)-$COMMIT$DIRTY"
BUILT=$(date -u +%Y-%m-%dT%H:%M:%SZ)

# Offsets match arduino-cli's esptool invocation for esp32c3 with the
# min_spiffs partition table; the LittleFS config partition at 0x3D0000 is
# deliberately not included so flashing a release keeps device credentials.
cat > "$OUT/manifest.json" <<JSON
{
  "version": "$VERSION",
  "commit": "$COMMIT",
  "built": "$BUILT",
  "chip": "ESP32-C3",
  "parts": [
    { "path": "/firmware/bootloader.bin", "address": 0,       "size": $(stat -c%s "$OUT/bootloader.bin") },
    { "path": "/firmware/partitions.bin", "address": 32768,   "size": $(stat -c%s "$OUT/partitions.bin") },
    { "path": "/firmware/boot_app0.bin",  "address": 57344,   "size": $(stat -c%s "$OUT/boot_app0.bin") },
    { "path": "/firmware/coinos-pos.bin", "address": 65536,   "size": $(stat -c%s "$OUT/coinos-pos.bin") }
  ]
}
JSON
echo "Published $VERSION to $OUT"
cat "$OUT/manifest.json"
