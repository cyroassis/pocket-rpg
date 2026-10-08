// Small raw-deflate decoder (the format of zlib / PNG data, without headers), after Mark Adler's "puff".
// The art layers are stored compressed in flash and unpacked into RAM when the character is drawn.
#include "inflate.h"
#include <string.h>

namespace {
struct State {
  uint8_t* out; uint32_t outLen, outCnt;
  const uint8_t* in; uint32_t inLen, inCnt;
  uint32_t bitBuf; int bitCnt;
  bool err;
};
struct Huffman { uint16_t count[16]; uint16_t symbol[288]; };

int bits(State& s, int need) {
  uint32_t val = s.bitBuf;
  while (s.bitCnt < need) {
    if (s.inCnt == s.inLen) { s.err = true; return 0; }
    val |= (uint32_t)s.in[s.inCnt++] << s.bitCnt;
    s.bitCnt += 8;
  }
  s.bitBuf = val >> need; s.bitCnt -= need;
  return (int)(val & ((1u << need) - 1));
}

bool stored(State& s) {
  s.bitBuf = 0; s.bitCnt = 0;
  if (s.inCnt + 4 > s.inLen) return false;
  uint32_t len = s.in[s.inCnt] | (s.in[s.inCnt + 1] << 8); s.inCnt += 4;
  if (s.inCnt + len > s.inLen || s.outCnt + len > s.outLen) return false;
  memcpy(s.out + s.outCnt, s.in + s.inCnt, len); s.inCnt += len; s.outCnt += len;
  return true;
}

int decode(State& s, const Huffman& h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len < 16; len++) {
    code |= bits(s, 1);
    if (s.err) return -1;
    int count = h.count[len];
    if (code - count < first) return h.symbol[index + (code - first)];
    index += count; first += count; first <<= 1; code <<= 1;
  }
  return -1;
}

void construct(Huffman& h, const uint8_t* length, int n) {
  memset(h.count, 0, sizeof h.count);
  for (int s = 0; s < n; s++) h.count[length[s]]++;
  uint16_t offs[16]; offs[1] = 0;
  for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h.count[len];
  for (int s = 0; s < n; s++) if (length[s]) h.symbol[offs[length[s]]++] = (uint16_t)s;
}

const uint16_t LBASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
const uint8_t LEXT[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
const uint16_t DBASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
const uint8_t DEXT[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

bool codes(State& s, const Huffman& lencode, const Huffman& distcode) {
  for (;;) {
    int sym = decode(s, lencode);
    if (sym < 0) return false;
    if (sym < 256) { if (s.outCnt == s.outLen) return false; s.out[s.outCnt++] = (uint8_t)sym; }
    else if (sym == 256) return true;
    else {
      sym -= 257; if (sym >= 29) return false;
      int len = LBASE[sym] + bits(s, LEXT[sym]);
      int ds = decode(s, distcode); if (ds < 0 || ds >= 30) return false;
      uint32_t dist = DBASE[ds] + bits(s, DEXT[ds]);
      if (s.err || dist > s.outCnt || s.outCnt + len > s.outLen) return false;
      uint8_t* o = s.out + s.outCnt; const uint8_t* from = o - dist;
      for (int i = 0; i < len; i++) o[i] = from[i];
      s.outCnt += len;
    }
  }
}

bool fixedBlock(State& s) {
  static Huffman lencode, distcode; static bool built = false;
  if (!built) {
    uint8_t l[288]; int i = 0;
    for (; i < 144; i++) l[i] = 8;
    for (; i < 256; i++) l[i] = 9;
    for (; i < 280; i++) l[i] = 7;
    for (; i < 288; i++) l[i] = 8;
    construct(lencode, l, 288);
    for (i = 0; i < 30; i++) l[i] = 5;
    construct(distcode, l, 30);
    built = true;
  }
  return codes(s, lencode, distcode);
}

bool dynamicBlock(State& s) {
  static const uint8_t ORDER[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
  uint8_t lengths[320];
  int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
  if (s.err || nlen > 286 || ndist > 30) return false;
  int i = 0;
  for (; i < ncode; i++) lengths[ORDER[i]] = (uint8_t)bits(s, 3);
  for (; i < 19; i++) lengths[ORDER[i]] = 0;
  Huffman lencode, distcode;
  construct(lencode, lengths, 19);
  int idx = 0;
  while (idx < nlen + ndist) {
    int sym = decode(s, lencode);
    if (sym < 0) return false;
    if (sym < 16) lengths[idx++] = (uint8_t)sym;
    else {
      int len = 0, rep;
      if (sym == 16) { if (idx == 0) return false; len = lengths[idx - 1]; rep = 3 + bits(s, 2); }
      else if (sym == 17) rep = 3 + bits(s, 3);
      else rep = 11 + bits(s, 7);
      if (idx + rep > nlen + ndist) return false;
      while (rep--) lengths[idx++] = (uint8_t)len;
    }
  }
  construct(lencode, lengths, nlen);
  construct(distcode, lengths + nlen, ndist);
  return codes(s, lencode, distcode);
}
}  // namespace

uint32_t inflateRaw(const uint8_t* in, uint32_t inLen, uint8_t* out, uint32_t outLen) {
  State s; memset(&s, 0, sizeof s);
  s.out = out; s.outLen = outLen; s.in = in; s.inLen = inLen;
  int last;
  do {
    last = bits(s, 1);
    int type = bits(s, 2);
    if (s.err) return 0;
    bool ok = type == 0 ? stored(s) : type == 1 ? fixedBlock(s) : type == 2 ? dynamicBlock(s) : false;
    if (!ok || s.err) return 0;
  } while (!last);
  return s.outCnt;
}
