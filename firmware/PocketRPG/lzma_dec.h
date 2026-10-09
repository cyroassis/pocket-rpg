// LZMA decoder for the packed art (raw LZMA1, lc=3 lp=0 pb=2). Returns the bytes written, 0 on bad data.
#pragma once
#include <stdint.h>

uint32_t lzmaDecode(const uint8_t* in, uint32_t inLen, uint8_t* out, uint32_t outLen);
