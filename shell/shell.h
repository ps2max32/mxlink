#ifndef SHELL_H
#define SHELL_H

#include <mx/types.h>

/* ---- draw.c: the picture, in units of a 640 x 448 screen ------------------ */

#define SCR_W 640.0f
#define SCR_H 448.0f

/* colours: 0xAARRGGBB, alpha 0x80 opaque */
#define RGB(c) (0x80000000u | (c))

int gfx_init(void);
void gfx_stop(void);
/* Blocks of GS memory from the first free one: n, page aligned or not. */
u32 gfx_alloc(u32 blocks, int page);
void gfx_begin(void);
void gfx_end(void);
/* The frame being built: uploads go into its once record. */
void gfx_upload(u32 dbp, u32 dbw, u32 psm, u32 w, u32 h, u32 bytes, const void *data);
/* top and bottom colours */
void gfx_rect(float x0, float y0, float x1, float y1, u32 top, u32 bottom);
/* left and right colours */
void gfx_rect_h(float x0, float y0, float x1, float y1, u32 left, u32 right);
/* texture (tex0, CLUT loaded) on a rectangle, texels (u0, v0)-(u1, v1),
   multiplied by top and bottom (0x80 each: as it is) */
void gfx_image(u64 tex0, float x0, float y0, float x1, float y1, float u0, float v0, float u1,
               float v1, u32 top, u32 bottom);

enum { FONT_BIG, FONT_SMALL };
/* Characters 1-4 of FONT_SMALL: the pad's symbols */
#define SYM_CROSS "\1"
#define SYM_TRIANGLE "\2"
#define SYM_SQUARE "\3"
#define SYM_CIRCLE "\4"
float gfx_width(int font, const char *s, float scale);
/* At x, on baseline y; returns the x after it. */
float gfx_text(int font, float x, float y, const char *s, u32 colour, float scale);
void gfx_center(int font, float x, float y, const char *s, u32 colour, float scale);

/* ---- games.c: drivers, the scan ------------------------------------------- */

#define MAX_GAMES 32

struct game {
  char dir[32], title[40], year[8], type[24], elf[32], cover[32], args[64], desc[96];
};

extern struct game games[MAX_GAMES];
extern u32 ngames;

enum { DEV_USB, DEV_MX4SIO, DEV_HDD, NDEVS };
extern const char *const dev_label[NDEVS];
/* "usb0:" */
extern const char *const dev_root[NDEVS];

int dev_load_saved(void);
void dev_save(int dev);
/* The next step of bringing up the device: a module loaded, or a look for
   its PS2MAX directory. Returns 0: not yet, 1: scanned, ngames set; -1: the
   device's modules cannot be loaded. */
int dev_step(int dev);
/* The path of a file of a game, into out (128 bytes). */
void game_file(char *out, int dev, const struct game *g, const char *file);
void dev_restart(void);
/* Ends the shell and starts the game: returns only if that fails. */
int game_launch(int dev, const struct game *g);

/* ---- cover.c: covers until there are images -------------------------------- */

void cover_make(u32 i, const char *title);
/* A cover file (tools/mk-cover.py). Returns 0, or -1. */
int cover_load(u32 i, const char *path);
/* Uploads the covers made (in the frame being built). */
void cover_upload(u32 n);
u64 cover_tex0(u32 i);
#define COVER_W 128.0f
#define COVER_H 176.0f

#endif
