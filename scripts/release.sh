#!/bin/bash
# Publishes build/PocketRPG_update.bin in ota/, where the boards look for updates over WiFi.
# Usage: scripts/release.sh "What changed"   (the version comes from firmware/PocketRPG/version.h)
# Then commit and push: the boards see it on the next Check (GitHub may take a few minutes to refresh).
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
V=$(grep -oP '#define FW_VERSION \K[0-9]+' $R/firmware/PocketRPG/version.h)
NOTES=${1:-"Version $V"}
mkdir -p $R/ota
cp $R/build/PocketRPG_update.bin $R/ota/PocketRPG_update.bin
python3 -c 'import json,sys; print(json.dumps({"version": int(sys.argv[1]), "notes": sys.argv[2][:60]}))' "$V" "$NOTES" > $R/ota/version.json
cat $R/ota/version.json
