// Small LZMA decoder for the packed art (tools/build_art.py and build_ui.py pack with Python's lzma module,
// raw LZMA1 stream, lc=3 lp=0 pb=2). The whole output is in memory, so it doubles as the dictionary.
// Follows the reference decoder in the LZMA SDK (LzmaSpec.cpp by Igor Pavlov, public domain).
#include "lzma_dec.h"
#include <stdlib.h>
#include <string.h>

namespace {
const int LC = 3, LP = 0, PB = 2;
const int kNumStates = 12, kNumPosBitsMax = 4, kNumLenToPosStates = 4, kNumAlignBits = 4;
const int kEndPosModelIndex = 14, kNumFullDistances = 1 << (kEndPosModelIndex >> 1), kMatchMinLen = 2;

struct Rc {
  const uint8_t* in; const uint8_t* end;
  uint32_t range, code;
  bool bad;
  uint8_t next() { if (in < end) return *in++; bad = true; return 0; }
  void init() { range = 0xFFFFFFFF; code = 0; bad = next() != 0; for (int i = 0; i < 4; i++) code = (code << 8) | next(); }
  void norm() { if (range < (1u << 24)) { range <<= 8; code = (code << 8) | next(); } }
  int bit(uint16_t* p) {
    uint32_t bound = (range >> 11) * *p;
    int b;
    if (code < bound) { *p += ((1 << 11) - *p) >> 5; range = bound; b = 0; }
    else { *p -= *p >> 5; code -= bound; range -= bound; b = 1; }
    norm();
    return b;
  }
  uint32_t direct(int n) {
    uint32_t res = 0;
    do {
      range >>= 1; code -= range;
      uint32_t t = 0 - (code >> 31);
      code += range & t;
      if (code == range) bad = true;
      norm();
      res = (res << 1) + (t + 1);
    } while (--n);
    return res;
  }
  unsigned tree(uint16_t* p, int bits) { unsigned m = 1; for (int i = 0; i < bits; i++) m = (m << 1) + bit(&p[m]); return m - (1u << bits); }
  unsigned rtree(uint16_t* p, int bits) {
    unsigned m = 1, sym = 0;
    for (int i = 0; i < bits; i++) { unsigned b = bit(&p[m]); m = (m << 1) + b; sym |= b << i; }
    return sym;
  }
};

struct Len {
  uint16_t choice, choice2, low[1 << kNumPosBitsMax][1 << 3], mid[1 << kNumPosBitsMax][1 << 3], high[1 << 8];
  unsigned decode(Rc& rc, unsigned posState) {
    if (!rc.bit(&choice)) return rc.tree(low[posState], 3);
    if (!rc.bit(&choice2)) return 8 + rc.tree(mid[posState], 3);
    return 16 + rc.tree(high, 8);
  }
};

struct Probs {
  uint16_t lit[0x300 << (LC + LP)];
  uint16_t posSlot[kNumLenToPosStates][1 << 6];
  uint16_t posDec[1 + kNumFullDistances - kEndPosModelIndex];
  uint16_t align[1 << kNumAlignBits];
  uint16_t isMatch[kNumStates << kNumPosBitsMax], isRep[kNumStates], isRepG0[kNumStates], isRepG1[kNumStates],
           isRepG2[kNumStates], isRep0Long[kNumStates << kNumPosBitsMax];
  Len len, repLen;
};
}

uint32_t lzmaDecode(const uint8_t* in, uint32_t inLen, uint8_t* out, uint32_t outLen) {
  Probs* P = (Probs*)malloc(sizeof(Probs));   // about 16 KB
  if (!P) return 0;
  uint16_t* all = (uint16_t*)P;
  for (size_t i = 0; i < sizeof(Probs) / 2; i++) all[i] = 1 << 10;
  Rc rc; rc.in = in; rc.end = in + inLen; rc.init();
  uint32_t pos = 0, rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
  unsigned state = 0;
  while (pos < outLen && !rc.bad) {
    unsigned posState = pos & ((1 << PB) - 1);
    if (!rc.bit(&P->isMatch[(state << kNumPosBitsMax) + posState])) {   // literal
      unsigned prev = pos ? out[pos - 1] : 0;
      uint16_t* probs = &P->lit[0x300 * (((pos & ((1 << LP) - 1)) << LC) + (prev >> (8 - LC)))];
      unsigned sym = 1;
      if (state >= 7) {
        unsigned matchByte = out[pos - rep0 - 1];
        do {
          unsigned mb = (matchByte >> 7) & 1; matchByte <<= 1;
          unsigned b = rc.bit(&probs[((1 + mb) << 8) + sym]);
          sym = (sym << 1) | b;
          if (mb != b) break;
        } while (sym < 0x100);
      }
      while (sym < 0x100) sym = (sym << 1) | rc.bit(&probs[sym]);
      out[pos++] = (uint8_t)sym;
      state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
      continue;
    }
    unsigned len;
    if (rc.bit(&P->isRep[state])) {
      if (!pos) break;
      if (!rc.bit(&P->isRepG0[state])) {
        if (!rc.bit(&P->isRep0Long[(state << kNumPosBitsMax) + posState])) {   // one byte from rep0
          state = state < 7 ? 9 : 11;
          out[pos] = out[pos - rep0 - 1]; pos++;
          continue;
        }
      } else {
        uint32_t dist;
        if (!rc.bit(&P->isRepG1[state])) dist = rep1;
        else {
          if (!rc.bit(&P->isRepG2[state])) dist = rep2;
          else { dist = rep3; rep3 = rep2; }
          rep2 = rep1;
        }
        rep1 = rep0; rep0 = dist;
      }
      len = P->repLen.decode(rc, posState);
      state = state < 7 ? 8 : 11;
    } else {
      rep3 = rep2; rep2 = rep1; rep1 = rep0;
      len = P->len.decode(rc, posState);
      state = state < 7 ? 7 : 10;
      unsigned lenState = len < kNumLenToPosStates - 1 ? len : kNumLenToPosStates - 1;
      unsigned slot = rc.tree(P->posSlot[lenState], 6);
      if (slot < 4) rep0 = slot;
      else {
        unsigned direct = (slot >> 1) - 1;
        uint32_t dist = (2 | (slot & 1)) << direct;
        if (slot < kEndPosModelIndex) dist += rc.rtree(P->posDec + dist - slot, direct);
        else { dist += rc.direct(direct - kNumAlignBits) << kNumAlignBits; dist += rc.rtree(P->align, kNumAlignBits); }
        rep0 = dist;
      }
      if (rep0 == 0xFFFFFFFF) break;   // end marker
      if (rep0 >= pos) break;          // bad data
    }
    len += kMatchMinLen;
    for (; len && pos < outLen; len--, pos++) out[pos] = out[pos - rep0 - 1];
  }
  free(P);
  return rc.bad ? 0 : pos;
}
