/* The picture: beam racing (<mx/hires.h>), one framebuffer drawn a strip at a
   time just ahead of the beam. CT24, 640 x 224 (PAL: 256) in interlaced FRAME
   mode: both fields show all lines, the lower one half a line lower, so
   coordinates of the 448-line screen stay sharp. Everything is drawn in
   context 2 (no Z writes). */

#include <mx/cpu.h>
#include <mx/hires.h>
#include <mx/mx.h>
#include <mx/tex.h>
#include <mx/vif.h>

#include "font.h"
#include "shell.h"

#define BUF_W 640u

static u8 rec[3 * 128 * 1024] MX_ALIGNED(64);
static float ys;
static u32 vram_next;
static struct mx_tex font_tex;
static u64 font_tex0;
static u32 font_clut[16] MX_ALIGNED(64);
static int font_sent;

extern const u8 font_bin[]; /* font.S */

#define PRIM(p, iip, tme, abe) mx_gs_prim(p, iip, tme, 0, abe, 0, 1, 1, 0)

static u32 px(float x) { return mx_gs_px(0) + (u32)(s32)(x * 16.0f); }
static u32 py(float y) { return mx_gs_py(0) + (u32)(s32)(y * ys * 16.0f); }

int gfx_init(void) {
  struct mx_video_mode m = *mx_video_default();
  m.ffmd = 1;
  u32 h = m.lines / 2;
  ys = (float)h / SCR_H;
  if (mx_hires_init(&m, BUF_W, h, MX_GS_PSM_CT24, rec, sizeof rec, (void *)0x70002000, 0x2000) < 0)
    return -1;
  vram_next = mx_gs.tbp;
  u32 tbp = gfx_alloc(0, 1);
  mx_tex_init(&font_tex, tbp, MX_GS_PSM_T4, 9, 8, 1, MX_TEX_PACKED);
  gfx_alloc(font_tex.blocks, 0);
  font_tex.cbp = gfx_alloc(1, 0);
  font_tex.cpsm = MX_GS_PSM_CT32;
  font_tex0 = mx_tex_tex0(&font_tex, 1, 0);
  for (u32 i = 0; i < 16; i++)
    font_clut[i] = (i * 0x80 / 15) << 24 | 0x808080u;
  mx_dcache_writeback(font_clut, sizeof font_clut);
  return 0;
}

void gfx_stop(void) { mx_frame_stop(); }

int gfx_restart(void) {
  font_sent = 0;
  return gfx_init();
}

u32 gfx_alloc(u32 blocks, int page) {
  if (page)
    vram_next = (vram_next + 31) & ~31u;
  u32 b = vram_next;
  vram_next += blocks;
  return b;
}

void gfx_upload(u32 dbp, u32 dbw, u32 psm, u32 w, u32 h, u32 bytes, const void *data) {
  u32 qwc = bytes / 16;
  mx_hires_once(1);
  u64 *c = mx_frame_reserve(6 + 3);
  c = mx_e_cnt(c, 5, mx_vif_nop(), mx_vif_direct(5));
  c = mx_e_ad_tag(c, 4, 0);
  c = mx_e_ad(c, MX_GS_REG_BITBLTBUF, mx_gs_bitbltbuf(0, 0, 0, dbp, dbw, psm));
  c = mx_e_ad(c, MX_GS_REG_TRXPOS, mx_gs_trxpos(0, 0, 0, 0, 0));
  c = mx_e_ad(c, MX_GS_REG_TRXREG, mx_gs_trxreg(w, h));
  c = mx_e_ad(c, MX_GS_REG_TRXDIR, 0);
  c = mx_e_cnt(c, 1, mx_vif_nop(), mx_vif_direct(1));
  c = mx_e_giftag(c, qwc, 1, -1, MX_GIF_FLG_IMAGE, 0, 0);
  c = mx_e_ref(c, data, qwc, mx_vif_nop(), mx_vif_direct(qwc));
  mx_frame_commit(c);
  mx_hires_once(0);
}

void gfx_begin(void) {
  mx_frame_begin_n();
  mx_hires_clear(0x06, 0x12, 0x1A);
  if (!font_sent) {
    gfx_upload(font_tex.tbp[0], font_tex.tbw[0], MX_GS_PSM_T4, 512, 256, 512 * 256 / 2, font_bin);
    gfx_upload(font_tex.cbp, 1, MX_GS_PSM_CT32, 8, 2, sizeof font_clut, font_clut);
    font_sent = 1;
  }
  u64 *c = mx_frame_direct(6);
  c = mx_e_ad_tag(c, 5, 1);
  c = mx_e_ad(c, MX_GS_REG_TEXFLUSH, 0);
  c = mx_e_ad(c, MX_GS_REG_TEST_2, mx_gs_test(0, 0, 0, 0, 0, 0, 1, 1));
  c = mx_e_ad(c, MX_GS_REG_ALPHA_2, mx_gs_alpha(0, 1, 0, 1, 0));
  c = mx_e_ad(c, MX_GS_REG_CLAMP_2, mx_gs_clamp(1, 1, 0, 0, 0, 0));
  mx_frame_commit(mx_e_ad(c, MX_GS_REG_TEX1_2, mx_gs_tex1(0, 0, 1, 1, 0, 0, 0)));
}

void gfx_end(void) { mx_frame_end(); }

static u64 *colour(u64 *c, u32 v) {
  return mx_e_rgbaq(c, v >> 16 & 0xFF, v >> 8 & 0xFF, v & 0xFF, v >> 24);
}

static void quad(float x0, float y0, float x1, float y1, u32 c00, u32 c10, u32 c01, u32 c11) {
  u64 *c = mx_frame_direct(1 + 8);
  c = mx_e_giftag(c, 4, 1, (s32)PRIM(MX_GS_PRIM_TRISTRIP, 1, 0, 1), MX_GIF_FLG_PACKED, 2,
                  mx_gif_regs2(MX_GIF_REG_RGBAQ, MX_GIF_REG_XYZ2));
  c = mx_e_xyz2(colour(c, c00), px(x0), py(y0), 0, 0);
  c = mx_e_xyz2(colour(c, c10), px(x1), py(y0), 0, 0);
  c = mx_e_xyz2(colour(c, c01), px(x0), py(y1), 0, 0);
  mx_frame_commit(mx_e_xyz2(colour(c, c11), px(x1), py(y1), 0, 0));
}

void gfx_rect(float x0, float y0, float x1, float y1, u32 top, u32 bottom) {
  quad(x0, y0, x1, y1, top, top, bottom, bottom);
}

void gfx_rect_h(float x0, float y0, float x1, float y1, u32 left, u32 right) {
  quad(x0, y0, x1, y1, left, right, left, right);
}

void gfx_image(u64 tex0, float x0, float y0, float x1, float y1, float u0, float v0, float u1,
               float v1, u32 top, u32 bottom) {
  u64 *c = mx_frame_direct(2 + 1 + 12);
  c = mx_e_ad(mx_e_ad_tag(c, 1, 0), MX_GS_REG_TEX0_2, tex0);
  c = mx_e_giftag(c, 4, 1, (s32)PRIM(MX_GS_PRIM_TRISTRIP, 1, 1, 1), MX_GIF_FLG_PACKED, 3,
                  mx_gif_regs3(MX_GIF_REG_RGBAQ, MX_GIF_REG_UV, MX_GIF_REG_XYZ2));
  u32 a0 = (u32)(u0 * 16), a1 = (u32)(u1 * 16), b0 = (u32)(v0 * 16), b1 = (u32)(v1 * 16);
  c = mx_e_xyz2(mx_e_uv(colour(c, top), a0, b0), px(x0), py(y0), 0, 0);
  c = mx_e_xyz2(mx_e_uv(colour(c, top), a1, b0), px(x1), py(y0), 0, 0);
  c = mx_e_xyz2(mx_e_uv(colour(c, bottom), a0, b1), px(x0), py(y1), 0, 0);
  mx_frame_commit(mx_e_xyz2(mx_e_uv(colour(c, bottom), a1, b1), px(x1), py(y1), 0, 0));
}

static const struct glyph *font_of(int font) { return font == FONT_BIG ? font_big : font_small; }

float gfx_width(int font, const char *s, float scale) {
  const struct glyph *g = font_of(font);
  float w = 0;
  for (; *s; s++)
    w += (float)g[(u8)*s & 127].adv * scale;
  return w;
}

#define CHUNK 24 /* characters a packet */

float gfx_text(int font, float x, float y, const char *s, u32 col, float scale) {
  const struct glyph *g = font_of(font);
  u64 *c = mx_frame_direct(3);
  c = mx_e_ad_tag(c, 2, 1);
  c = mx_e_ad(c, MX_GS_REG_TEX0_2, font_tex0);
  mx_frame_commit(mx_e_ad(c, MX_GS_REG_RGBAQ,
                          mx_gs_rgbaq(col >> 16 & 0xFF, col >> 8 & 0xFF, col & 0xFF, col >> 24, 1.0f)));
  while (*s) {
    const struct glyph *q[CHUNK];
    float at[CHUNK];
    u32 n = 0;
    for (; *s && n < CHUNK; s++) {
      const struct glyph *e = &g[(u8)*s & 127];
      if (e->w > 1 || e->h > 1)
        at[n] = x, q[n++] = e;
      x += (float)e->adv * scale;
    }
    if (!n)
      continue;
    c = mx_frame_direct(1 + 4 * n);
    c = mx_e_giftag(c, 2 * n, 1, (s32)PRIM(MX_GS_PRIM_SPRITE, 0, 1, 1), MX_GIF_FLG_PACKED, 2,
                    mx_gif_regs2(MX_GIF_REG_UV, MX_GIF_REG_XYZ2));
    for (u32 i = 0; i < n; i++) {
      const struct glyph *e = q[i];
      float gx = at[i] + (float)e->xo * scale, gy = y + (float)e->yo * scale;
      c = mx_e_xyz2(mx_e_uv(c, (u32)e->u << 4, (u32)e->v << 4), px(gx), py(gy), 0, 0);
      c = mx_e_xyz2(mx_e_uv(c, (u32)(e->u + e->w) << 4, (u32)(e->v + e->h) << 4),
                    px(gx + (float)e->w * scale), py(gy + (float)e->h * scale), 0, 0);
    }
    mx_frame_commit(c);
  }
  return x;
}

void gfx_center(int font, float x, float y, const char *s, u32 col, float scale) {
  gfx_text(font, x - gfx_width(font, s, scale) / 2, y, s, col, scale);
}
