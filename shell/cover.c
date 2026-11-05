/* Covers until the shell reads images: an abstract pattern from the title,
   one of five, in a palette of one colour. T8, 128 x 176, in a 128 x 256
   texture. */

#include <mx/cpu.h>
#include <mx/mx.h>
#include <mx/tex.h>

#include "shell.h"

#define CW 128
#define CH 176

static u8 pix[MAX_GAMES][CW * CH] MX_ALIGNED(64);
static u32 clut[MAX_GAMES][256] MX_ALIGNED(64);
static struct mx_tex tex[MAX_GAMES];
static u64 tex0[MAX_GAMES];

static const u32 accents[] = {0xE8963C, 0x3CC8C0, 0x9CC850, 0xD84868, 0xA070E0, 0x5898F0};

static u32 hash(const char *s) {
  u32 h = 2166136261u;
  for (; *s; s++)
    h = (h ^ (u8)*s) * 16777619u;
  return h ^ h >> 15;
}

/* 0 .. 255 .. 0 over a period */
static u32 wave(float v, float period) {
  v = v / period;
  v -= (float)(s32)v;
  v = v < 0.5f ? v * 2 : 2 - v * 2;
  return (u32)(v * 255);
}

static float absf(float v) { return v < 0 ? -v : v; }

static u32 pattern(u32 kind, u32 seed, float x, float y) {
  float dx = x - CW / 2, dy = y - CH / 2;
  switch (kind) {
  case 0: /* rings */
    return wave(__builtin_sqrtf(dx * dx + dy * dy), 22.0f);
  case 1: { /* squares */
    float a = absf(dx), b = absf(dy);
    return wave(a > b ? a : b, 18.0f);
  }
  case 2: { /* bars */
    u32 col = (u32)x / 12, h = (hash("b") * (col + seed) >> 24) * 120 / 255 + 30;
    return CH - y < (float)h && (u32)x % 12 < 9 ? 230 : 30;
  }
  case 3: /* triangles */
    return wave(absf(dx) * 1.2f + y * 0.6f, 24.0f);
  default: { /* blocks */
    u32 bx = (u32)x / 16, by = (u32)y / 16;
    return (hash("k") * (bx * 31 + by * 17 + seed) >> 24) & 0xC0 ? 40 : 200;
  }
  }
}

void cover_make(u32 i, const char *title) {
  u32 h = hash(title), kind = h % 5, seed = h >> 8 & 0xFF, acc = accents[(h >> 4) % 6];
  for (u32 y = 0; y < CH; y++)
    for (u32 x = 0; x < CW; x++)
      pix[i][y * CW + x] = (u8)pattern(kind, seed, (float)x, (float)y);
  /* a CLUT of CSM1: entries with bits 3 and 4 swapped */
  for (u32 k = 0; k < 256; k++) {
    u32 r = (acc >> 16 & 0xFF) * k / 255, g = (acc >> 8 & 0xFF) * k / 255, b = (acc & 0xFF) * k / 255;
    r = r * 7 / 8 + 14, g = g * 7 / 8 + 12, b = b * 7 / 8 + 14;
    u32 at = (k & ~0x18u) | (k & 8) << 1 | (k & 0x10) >> 1;
    clut[i][at] = 0x80u << 24 | b << 16 | g << 8 | r;
  }
  mx_dcache_writeback(pix[i], sizeof pix[i]);
  mx_dcache_writeback(clut[i], sizeof clut[i]);
}

void cover_upload(u32 n) {
  static u32 base, cbase;
  if (!base) {
    base = gfx_alloc(0, 1);
    u32 b = 0;
    for (u32 i = 0; i < MAX_GAMES; i++) {
      b = mx_tex_init(&tex[i], gfx_alloc(0, 1), MX_GS_PSM_T8, 7, 8, 1, MX_TEX_PACKED);
      gfx_alloc(b, 0);
    }
    cbase = gfx_alloc(MAX_GAMES * 4, 0);
  }
  for (u32 i = 0; i < n; i++) {
    tex[i].cbp = cbase + i * 4;
    tex[i].cpsm = MX_GS_PSM_CT32;
    tex0[i] = mx_tex_tex0(&tex[i], 0, 0);
    gfx_upload(tex[i].tbp[0], tex[i].tbw[0], MX_GS_PSM_T8, CW, CH, CW * CH, pix[i]);
    gfx_upload(tex[i].cbp, 1, MX_GS_PSM_CT32, 16, 16, sizeof clut[i], clut[i]);
  }
}

u64 cover_tex0(u32 i) { return tex0[i]; }
