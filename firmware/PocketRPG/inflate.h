// Raw deflate decoder (zlib's format without the header). Returns the bytes written, 0 on bad data.
#pragma once
#include <stdint.h>

uint32_t inflateRaw(const uint8_t* in, uint32_t inLen, uint8_t* out, uint32_t outLen);
