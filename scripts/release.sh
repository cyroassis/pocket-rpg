#!/bin/bash
# Publishes build/PocketRPG_update.bin as a GitHub release, so boards can update over WiFi.
# Usage: scripts/release.sh "What changed"   (the version comes from firmware/PocketRPG/version.h)
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
V=$(grep -oP '#define FW_VERSION \K[0-9]+' $R/firmware/PocketRPG/version.h)
NOTES=${1:-"Version $V"}
python3 -c 'import json,sys; print(json.dumps({"version": int(sys.argv[1]), "notes": sys.argv[2][:60]}))' "$V" "$NOTES" > $R/build/version.json
gh release create "v$V" $R/build/PocketRPG_update.bin $R/build/version.json $R/build/PocketRPG_full_0x0.bin \
  --repo cyroassis/pocket-rpg --title "Version $V" --notes "$NOTES"
