#include "game.h"
#include "app.h"      // platformRandom
#include "render.h"   // material names
#include <string.h>
#include <stdio.h>

static const uint16_t LEVEL_STEPS[10] = { 100, 125, 150, 200, 250, 325, 400, 500, 650, 800 };
static const int MAX_LEVEL = 60;   // far beyond reach; keeps the numbers inside 32 bits

void gameReset(Game& g) { memset(&g, 0, sizeof g); g.ver = GAME_VERSION; }
bool gameValid(const Game& g) { return g.ver == GAME_VERSION && g.finds <= DAILY_FINDS; }

uint32_t xpToNext(int level) {
  if (level < 1) level = 1;
  uint32_t v = LEVEL_STEPS[(level - 1) % 10];
  for (int k = (level - 1) / 10; k > 0; k--) v *= 10;
  return v;
}
uint32_t xpAtLevel(int level) {
  uint32_t t = 0;
  for (int l = 1; l < level && l < MAX_LEVEL; l++) t += xpToNext(l);
  return t;
}
int levelOf(uint32_t xp) {
  int l = 1;
  while (l < MAX_LEVEL && xp >= xpToNext(l)) { xp -= xpToNext(l); l++; }
  return l;
}

uint8_t rollTier(int rolls, int maxTier) {
  int best = 1;
  for (int r = 0; r < rolls; r++) {
    int t = 1;
    while (t < 20 && platformRandom(1000000) < 625000) t++;   // P(tier >= N) = 0.625^(N-1)
    if (t > best) best = t;
  }
  return (uint8_t)(best > maxTier ? maxTier : best);
}

int bagCount(const Game& g) { int n = 0; for (int i = 0; i < BAG_SIZE; i++) if (g.bag[i].tier) n++; return n; }
int bagFreeSlot(const Game& g) { for (int i = 0; i < BAG_SIZE; i++) if (!g.bag[i].tier) return i; return -1; }

static void addXp(Game& g, uint32_t xp, int* levelsGained) {
  int before = levelOf(g.xp);
  g.xp += xp; g.xpDay += xp;
  if (levelsGained) *levelsGained += levelOf(g.xp) - before;
}

int gameWalk(Game& g, uint32_t steps, uint32_t day, FindEvent* out, int maxOut, int* levelsGained) {
  if (g.day != day) { g.stepsBefore += g.lastSteps; g.lastSteps = 0; g.day = day; g.counted = 0; g.finds = 0; g.xpDay = 0; }
  if (steps > g.lastSteps) g.lastSteps = steps;
  if (steps < g.counted) g.counted = steps;   // the step counter went back (e.g. a restart): just follow it
  int n = 0;
  while (steps - g.counted >= STEPS_PER_FIND) {
    g.counted += STEPS_PER_FIND;
    addXp(g, XP_PER_FIND, levelsGained);
    FindEvent e; memset(&e, 0, sizeof e); e.kind = F_XP;
    if (g.finds < DAILY_FINDS) {
      g.finds++;
      int slot = bagFreeSlot(g);
      if (slot >= 0 && platformRandom(ITEM_FIND_ODDS) == 0) {
        uint8_t type = (uint8_t)platformRandom(3);
        int kind = type == IT_WEAPON ? (int)platformRandom(weaponKindCount()) : 0;
        Item it = { type, rollTier(1, 3), (uint8_t)(1 | kind << 4) };
        g.bag[slot] = it; e.kind = F_ITEM; e.item = it;
      } else {
        if (g.mats < 9999) g.mats++;
        e.kind = F_MATERIAL;
      }
    }
    if (out && maxOut > 0) {   // keep the latest maxOut events
      if (n < maxOut) out[n] = e;
      else { memmove(out, out + 1, sizeof(FindEvent) * (maxOut - 1)); out[maxOut - 1] = e; }
    }
    n++;
  }
  return n;
}

int craftRolls(int materials) { return materials / CRAFT_MIN; }

int gameCraft(Game& g, uint8_t type, int kind, int materials, int* levelsGained) {
  materials = materials / CRAFT_MIN * CRAFT_MIN;
  if (materials < CRAFT_MIN || materials > CRAFT_MAX || materials > g.mats) return -1;
  int slot = bagFreeSlot(g);
  if (slot < 0) return -1;
  int rolls = craftRolls(materials);
  g.mats -= materials;
  if (type != IT_WEAPON || kind < 0 || kind >= weaponKindCount()) kind = 0;
  Item it = { type, rollTier(rolls, 20), (uint8_t)(rolls | kind << 4) };
  g.bag[slot] = it;
  g.crafted++;
  addXp(g, XP_PER_ROLL * rolls, levelsGained);
  return slot;
}

bool canEquip(const Game& g, const Item& it) { return it.tier && levelOf(g.xp) >= it.tier; }

bool gameEquip(Game& g, int s) {
  if (s < 0 || s >= BAG_SIZE || !g.bag[s].tier || !canEquip(g, g.bag[s]) || g.bag[s].type >= GEAR_SLOTS) return false;
  Item worn = g.gear[g.bag[s].type];
  g.gear[g.bag[s].type] = g.bag[s];
  g.bag[s] = worn;   // the old piece takes its place in the bag (or the slot empties)
  return true;
}
bool gameUnequip(Game& g, int gs) {
  if (gs < 0 || gs >= GEAR_SLOTS || !g.gear[gs].tier) return false;
  int s = bagFreeSlot(g);
  if (s < 0) return false;
  g.bag[s] = g.gear[gs]; memset(&g.gear[gs], 0, sizeof(Item));
  return true;
}
int salvageValue(const Item& it) { return 2 * (itemRolls(it) ? itemRolls(it) : 1); }
void gameSalvage(Game& g, int s) {
  if (s < 0 || s >= BAG_SIZE || !g.bag[s].tier) return;
  g.mats += salvageValue(g.bag[s]);
  memset(&g.bag[s], 0, sizeof(Item));
}

void itemName(const Item& it, char* out, int n) {
  int t = it.tier ? it.tier : 1;
  if (it.type == IT_WEAPON) snprintf(out, n, "%s %s", weaponMaterialName(t), weaponKindName(itemKind(it)));
  else if (it.type == IT_ARMOR) snprintf(out, n, "%s Armor", armorMaterialName(t));
  else snprintf(out, n, "%s Cape", capeName(t));
}
