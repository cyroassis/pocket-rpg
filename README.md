# Pocket RPG

An endless pocket RPG for the **Waveshare ESP32-S3-Touch-AMOLED-1.8** (368×448 AMOLED, touch, step sensor).
Walk to find materials and XP, craft weapons, armor and capes, level up, and trade gear with a friend's board.

**Try it in the browser:** [cyroassis.github.io/pocket-rpg](https://cyroassis.github.io/pocket-rpg/). It runs the same game code as the board (compiled to WebAssembly), and can show two boards side by side to try trading.

## The game

- **Hero:** boy or girl, skin, eyes, hair, hair color, a profession (Blacksmith: weapons, Armorer: armor, Tailor: capes) and a name.
- **Walking:** every 200 steps gives 5 XP (no limit) and 1 material (up to 20 a day). About 1 find in 50 is an item instead.
- **Craft:** 5 to 50 materials, in steps of 5. Every 5 materials is one roll for rarity, and the best roll wins. 2 XP per 5 materials.
- **Rarity:** 20 tiers, each one 1.6× rarer than the one before.
- **Levels:** 100, 125, 150, 200, 250, 325, 400, 500, 650, 800 XP, then the same steps ×10 every 10 levels, forever. You can equip an item when your level is at least its tier.
- **Gear:** weapon (sword, axe or mace), armor and cape. The bag holds 20 items, and salvage gives materials back.
- **Trade:** two boards close together. Each side offers up to 3 items and approves. Any change clears both approvals, and when both approve, the items swap.

## Folders

| Folder | What is in it |
|---|---|
| `firmware/PocketRPG/` | The Arduino sketch. `app.cpp` (screens), `game.cpp` (rules), `render.cpp` (character drawing), `ota.cpp` (WiFi update), `PocketRPG.ino` (hardware). |
| `art/` | The original PNG layers (368×448): bodies, hair, armor, capes, weapons, gems. `art/ui/` has the screen art (mockups at about 3× size). |
| `tools/` | `build_art.py` turns `art/` into `firmware/PocketRPG/art_data.h`. `build_fonts.py` makes `fonts.h`. `build_ui.py` cuts the screen art into `ui_data.h`. |
| `web/` | The browser version: `web.cpp` + `web.js` + `shell.html` → `pocket-rpg.html`. |
| `creator/` | The character creator page used to tune the art. |
| `tests/` | PC programs that draw screens to images, for checking without a board. |
| `scripts/` | Build and release scripts. |
| `ota/` | The latest version, which boards download over WiFi. |

## Building

The firmware needs **arduino-cli** (or the Arduino IDE) with:
- the esp32 core 3.3.x
- GFX Library for Arduino 1.6.4
- SensorLib 0.3.3

Board settings: ESP32S3 Dev Module, PSRAM **OPI**, Flash **16 MB**, Partition **16M Flash (3MB APP/9.9MB FATFS)**, USB CDC On Boot **enabled**.

```
scripts/build_firmware.sh
```

This makes two files in `build/`:
- `PocketRPG_update.bin`: flash at **0x10000**. It keeps the hero.
- `PocketRPG_full_0x0.bin`: flash at **0x0**. It erases everything.

After changing the art or fonts, run `python3 tools/build_art.py`, `tools/build_ui.py` or `tools/build_fonts.py` first.

## Updating over WiFi

On the board, go to **Settings → page 2 → Update**.
- The first time, tap **WiFi**. The board opens a network called `PocketRPG-XXXX`. Join it from a phone, and a page opens where you pick your WiFi and type its password. The board keeps up to 3 networks.
- **Check** reads `ota/version.json` in this repository, and **Install** downloads `ota/PocketRPG_update.bin` and installs it. If the download fails, the old version keeps running. If a new version crashes on start, the board goes back to the old one.

To publish a new version:
1. Raise `FW_VERSION` in `firmware/PocketRPG/version.h`.
2. Build the firmware.
3. Run `scripts/release.sh "what changed"`, then commit and push.

The script copies the program to `ota/` and writes `ota/version.json`. GitHub can take a few minutes to serve the new files.

Note: after a WiFi update the board runs from its second program slot. To flash by USB after that, use the full `0x0` file (it erases the hero).

## Credits

Fonts: [Silkscreen](https://github.com/googlefonts/silkscreen) and [Atkinson Hyperlegible](https://github.com/googlefonts/atkinson-hyperlegible), both under the SIL Open Font License (see `tools/fonts/`).
