#!/usr/bin/env bash
# Build one firmware sketch for the Waveshare ESP32-S3-ETH.
#   ./build.sh <sketch-folder> [out-dir]
# Produces <name>-v<version>-OTA.bin (upload through the device's /update page)
# and <name>-v<version>-FULL-FLASH-0x0.bin (first-time USB flash at 0x0).
# Needs arduino-cli with the esp32 core 3.3.x; set ARDUINO_CLI / ARDUINO_CONFIG to override.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SKETCH="$(cd "$1" && pwd)"; NAME="$(basename "$SKETCH")"
OUT="${2:-$HERE/../out/$NAME}"
CLI="${ARDUINO_CLI:-arduino-cli}"; CFG=(); [ -n "${ARDUINO_CONFIG:-}" ] && CFG=(--config-file "$ARDUINO_CONFIG")
FQBN="espressif:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,USBMode=hwcdc"
EXTRA=(); [ -n "${CTAGS_DIR:-}" ] && EXTRA=(--build-property "runtime.tools.ctags.path=$CTAGS_DIR")

rm -rf "$OUT"; mkdir -p "$OUT"
"$CLI" "${CFG[@]}" compile -b "$FQBN" "${EXTRA[@]}" --libraries "$HERE/libraries" --warnings default \
  --output-dir "$OUT/raw" "$SKETCH"
rm -rf "$SKETCH/build"

VER="$(grep -oE 'OTA_FIRMWARE\("[^"]+", *"[^"]+"\)' "$SKETCH"/*.ino | sed -E 's/.*"([^"]+)"\)/\1/')"
cp "$OUT/raw/$NAME.ino.bin" "$OUT/$NAME-v$VER-OTA.bin"
ESPTOOL="${ESPTOOL:-esptool}"
BOOTAPP0="$(dirname "$(command -v "$CLI")")/../user/hardware/espressif/esp32/tools/partitions/boot_app0.bin"
[ -f "${BOOT_APP0:-$BOOTAPP0}" ] && "$ESPTOOL" --chip esp32s3 merge-bin -o "$OUT/$NAME-v$VER-FULL-FLASH-0x0.bin" \
  --flash-mode keep --flash-freq keep --flash-size 16MB \
  0x0 "$OUT/raw/$NAME.ino.bootloader.bin" 0x8000 "$OUT/raw/$NAME.ino.partitions.bin" \
  0xe000 "${BOOT_APP0:-$BOOTAPP0}" 0x10000 "$OUT/raw/$NAME.ino.bin" >/dev/null
ls -1 "$OUT"/*.bin
