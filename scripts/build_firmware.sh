#!/bin/bash
# Builds the firmware with arduino-cli and puts both files in build/:
#   PocketRPG_update.bin      the program only: flash at 0x10000 (keeps the hero) or publish as a release
#   PocketRPG_full_0x0.bin    everything (bootloader, partitions, program): flash at 0x0 (erases the hero)
# Needs the esp32 core 3.3.x, "GFX Library for Arduino" 1.6.4 and SensorLib 0.3.3.
# ARDUINO_CLI can point to arduino-cli; extra --build-property options can be passed as arguments.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
CLI=${ARDUINO_CLI:-arduino-cli}
mkdir -p $R/build
$CLI compile --fqbn "espressif:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc" \
  "$@" --output-dir $R/build/out $R/firmware/PocketRPG 2>&1 | grep -v "Error initializing\|Downloading index" | grep -E "Sketch uses|Global|error|Error" || true
test $R/build/out/PocketRPG.ino.bin -nt $R/firmware/PocketRPG/PocketRPG.ino
cp $R/build/out/PocketRPG.ino.bin $R/build/PocketRPG_update.bin
cp $R/build/out/PocketRPG.ino.merged.bin $R/build/PocketRPG_full_0x0.bin
echo "build/PocketRPG_update.bin, build/PocketRPG_full_0x0.bin"
