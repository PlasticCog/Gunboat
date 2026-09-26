#pragma once
/* Gunboat adapter: the reused TD3 VGA presenter only needs its frame memory.
 * Gunboat assets and state are typed C++ objects, not TD3's DS address layout. */
#include "types.h"
#define VRAM_SEG 0xa000
extern u8 mem[0x110000];
static inline u8 *mp(u16 seg,u16 off){return mem+((u32)seg<<4)+off;}
