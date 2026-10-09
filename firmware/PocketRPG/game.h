// Pocket RPG game rules: XP and levels, materials found by walking, crafting rolls, the bag and the gear.
// Pure logic (no drawing, no hardware), so it runs the same on the board, on a PC and in the browser.
#pragma once
#include <stdint.h>
#include <stddef.h>

enum ItemType : uint8_t { IT_WEAPON = 0, IT_ARMOR = 1, IT_CAPE = 2 };
struct Item {
  uint8_t type;    // ItemType
  uint8_t tier;    // 1..20 (rarity); 0 = empty slot
  uint8_t rolls;   // low 4 bits: crafting rolls used (materials / 5), salvage gives back 2 materials per roll;
                   // high 4 bits: weapon kind (0 sword, 1 axe, 2 mace, ... see weaponKindName)
};
inline int itemRolls(const Item& it) { return it.rolls & 15; }
inline int itemKind(const Item& it) { return it.rolls >> 4; }

#define BAG_SIZE 20
#define GEAR_SLOTS 3          // weapon, armor, cape (same order as ItemType)
#define DAILY_FINDS 20        // materials per day from walking
#define STEPS_PER_FIND 200    // every 200 steps: XP, and a material while today's finds last
#define XP_PER_FIND 5         // walking XP has no daily limit
#define ITEM_FIND_ODDS 50     // 1 in 50 finds is an item (tier 1..3) instead of a material
#define CRAFT_MIN 5           // materials per craft: 5..50 in steps of 5, each 5 = one rarity roll
#define CRAFT_MAX 50
#define XP_PER_ROLL 2         // crafting XP: 2 per 5 materials
#define GAME_VERSION 1
#define TEST_START_MATERIALS 10000   // TESTING: new heroes start with this many materials (0 for the real game)

// Trade safety: when both heroes approve, each board first "commits" (saved here), and swaps only after it
// hears the other board's commit. If the boards lose each other right then, the trade stays pending and the
// offered items stay locked; the next time the two boards meet, they finish it or cancel it together.
struct TradeRec {
  uint32_t key;         // the unfinished trade (0 = none)
  uint8_t mac[6];       // the other board
  int8_t slots[3];      // my bag slots on offer (locked while pending)
  Item give[3], get[3];
  char name[11];        // the other hero (for the "finish it" note)
  uint32_t done[4];     // keys of the last trades this board finished (the other board asks about them)
};

struct Game {
  uint8_t ver;
  uint8_t finds;        // material finds today (0..DAILY_FINDS)
  uint16_t mats;        // materials owned
  uint32_t xp;          // total XP ever
  uint32_t day;         // yyyymmdd of "today" for the counters below
  uint32_t counted;     // today's steps already turned into finds
  uint32_t xpDay;       // XP earned today
  uint32_t crafted;     // items crafted, all time
  Item bag[BAG_SIZE];
  Item gear[GEAR_SLOTS];
  // added later (older saves end before these and load with them at 0)
  uint32_t stepsBefore; // steps of all the days before today
  uint32_t lastSteps;   // today's steps, as last seen
  TradeRec trade;       // a trade that may be unfinished, and the trades this board finished
};
#define GAME_OLD_SIZE offsetof(Game, stepsBefore)   // saves made before the step total
#define GAME_V2_SIZE offsetof(Game, trade)          // saves made before the trade record
inline bool gameSaveSizeOk(size_t n) { return n == sizeof(Game) || n == GAME_V2_SIZE || n == GAME_OLD_SIZE; }
inline uint32_t totalSteps(const Game& g) { return g.stepsBefore + g.lastSteps; }

enum FindKind : uint8_t { F_MATERIAL, F_ITEM, F_XP };
struct FindEvent { uint8_t kind; Item item; };

void gameReset(Game& g);
bool gameValid(const Game& g);

// Levels: 100, 125, 150, 200, 250, 325, 400, 500, 650, 800 XP, then the same steps x10 every 10 levels, forever.
uint32_t xpToNext(int level);          // XP needed to go from level to level + 1
int levelOf(uint32_t xp);              // level for a total XP (starts at 1)
uint32_t xpAtLevel(int level);         // total XP at the start of a level

// Walking: turns new steps into finds. Returns how many finds happened; the latest ones are copied to out.
int gameWalk(Game& g, uint32_t stepsToday, uint32_t day, FindEvent* out, int maxOut, int* levelsGained);

// Crafting: one item of the given type with materials/5 rolls (best roll wins). Returns the bag slot, -1 if not possible.
int gameCraft(Game& g, uint8_t type, int kind, int materials, int* levelsGained);
int craftRolls(int materials);

// One rarity roll: tier 1 always, each next tier 1.6x rarer. Best of `rolls`, at most maxTier.
uint8_t rollTier(int rolls, int maxTier);

int bagCount(const Game& g);
int bagFreeSlot(const Game& g);
bool canEquip(const Game& g, const Item& it);   // hero level >= item tier
bool gameEquip(Game& g, int bagSlot);           // swaps with what is worn
bool gameUnequip(Game& g, int gearSlot);        // back to the bag (needs a free slot)
int salvageValue(const Item& it);
void gameSalvage(Game& g, int bagSlot);
void itemName(const Item& it, char* out, int n);   // "Bronze Sword", "Iron Axe", "Iron Armor", "Knight Cape"
