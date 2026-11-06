/* mxloader's shell: a carousel of the games on the device in use. Runs as an
   ordinary program on what mxlink offers; a game starts by mx_loader_launch,
   with the IOP as it is, and mxlink starts the shell again after it. */

#include <mx/debug.h>
#include <mx/mx.h>
#include <mx/hires.h>
#include <mx/pad.h>
#include <mx/sif.h>
#include <mx/string.h>

#include "shell.h"

enum { S_SEARCH, S_MAIN, S_INFO, S_DEVICE, S_EMPTY, S_START, S_FAILED };

#define ACCENT 0xF0A83Cu
#define WHITE 0xF4F4F0u
#define GREY 0x98A8ACu
#define BASE 280.0f /* the covers stand on this line */

static int screen = S_SEARCH, dev, dev_sel, dev_ready, pad_on;
static u32 sel, covers_new, ticks, start_at;
/* the carousel: at (and on its way to) a place that goes on past the ends
   when it can go round, game = place modulo the games */
static s32 place;
static float pos;
#define ROUND 5 /* games it takes to go round */

static s32 game_at(s32 p) {
  s32 n = (s32)ngames, g = p % n;
  return g < 0 ? g + n : g;
}

static const char *back_path; /* mxlink: the program that came back */
static int back_status;
static struct mx_crash crash;
static int crashed;

static int contains(const char *s, const char *what) {
  u32 n = strlen(what);
  for (; *s; s++)
    if (!strncmp(s, what, n))
      return 1;
  return 0;
}

static void background(void) {
  gfx_rect(0, 0, SCR_W, 190, RGB(0x0C2028), RGB(0x1A4A52));
  gfx_rect(0, 190, SCR_W, 300, RGB(0x1A4A52), RGB(0x0A1E26));
  gfx_rect(0, 300, SCR_W, SCR_H, RGB(0x0A1E26), RGB(0x05101A));
}

static void header(void) {
  gfx_text(FONT_BIG, 44, 62, "mxloader", RGB(WHITE), 0.9f);
  gfx_rect(44, 70, 96, 73, RGB(ACCENT), RGB(ACCENT));
  float x = 596;
  for (int i = NDEVS - 1; i >= 0; i--) {
    u32 c = i == dev && dev_ready ? 0x60D8A0u : 0x485860u;
    gfx_rect(x - 8, 50, x, 58, RGB(c), RGB(c));
    x -= 14 + gfx_width(FONT_SMALL, dev_label[i], 1);
    gfx_text(FONT_SMALL, x, 61, dev_label[i], RGB(i == dev ? WHITE : GREY), 1);
    x -= 24;
  }
}

static u32 scale_col(u32 v) { return v << 16 | v << 8 | v; }

static void cover(u32 i, float d) {
  float ad = d < 0 ? -d : d, s, off;
  if (ad > 3)
    return;
  s = ad < 1 ? 1 - 0.34f * ad : 0.66f - 0.14f * (ad - 1);
  off = ad < 1 ? 138 * ad : 138 + 92 * (ad - 1);
  float cx = SCR_W / 2 + (d < 0 ? -off : off), w = 146 * s, h = 200 * s;
  float x0 = cx - w / 2, x1 = cx + w / 2, y0 = BASE - h;
  u32 fade = ad > 2 ? (u32)((3 - ad) * 0x80) : 0x80;
  u32 lit = (u32)(0x80 * (1 - 0.3f * (ad > 2 ? 2 : ad)));
  u32 col = fade << 24 | scale_col(lit);
  u64 t = cover_tex0(i);
  if (ad < 0.5f) {
    /* the glow of the one chosen */
    u32 a = (u32)((0.5f - ad) * 2 * 0x50), g = a << 24 | ACCENT, n = ACCENT;
    float e = 10;
    gfx_rect(x0, y0 - e, x1, y0, n, g);
    gfx_rect(x0, BASE, x1, BASE + e, g, n);
    gfx_rect_h(x0 - e, y0, x0, BASE, n, g);
    gfx_rect_h(x1, y0, x1 + e, BASE, g, n);
  }
  gfx_image(t, x0, y0, x1, BASE, 0, 0, COVER_W, COVER_H, col, col);
  /* the title on the cover, on a dark foot */
  gfx_rect(x0, BASE - h * 0.3f, x1, BASE, 0x00000000u, fade * 3 / 4 << 24);
  float fs = s * 0.62f;
  float tw = gfx_width(FONT_BIG, games[i].title, fs);
  if (tw > w - 12 * s)
    fs *= (w - 12 * s) / tw;
  gfx_text(FONT_BIG, x0 + 8 * s, BASE - 10 * s, games[i].title, fade << 24 | scale_col(0xD0 * lit / 0x80), fs);
  if (ad < 0.5f) {
    u32 a = (u32)((0.5f - ad) * 2 * 0x80) << 24 | ACCENT;
    gfx_rect(x0 - 2, y0 - 2, x1 + 2, y0, a, a);
    gfx_rect(x0 - 2, BASE, x1 + 2, BASE + 2, a, a);
    gfx_rect(x0 - 2, y0, x0, BASE, a, a);
    gfx_rect(x1, y0, x1 + 2, BASE, a, a);
  }
  /* the reflection */
  u32 ra = fade * 3 / 10;
  float rh = h * 0.2f;
  gfx_image(t, x0, BASE + 3, x1, BASE + 3 + rh, 0, COVER_H, COVER_W, COVER_H * 0.8f,
            ra << 24 | scale_col(lit), scale_col(lit));
}

static void carousel(void) {
  s32 c = (s32)(pos + (pos < 0 ? -0.5f : 0.5f)), n = (s32)ngames;
  for (s32 k = 3; k >= 0; k--) {
    s32 l = c - k, r = c + k;
    if (n >= ROUND || (l >= 0 && l < n))
      cover((u32)game_at(l), (float)l - pos);
    if (k && (n >= ROUND || (r >= 0 && r < n)))
      cover((u32)game_at(r), (float)r - pos);
  }
}

static void footer(void) {
  float y = 396, x = 172;
  x = gfx_text(FONT_SMALL, x, y, SYM_CROSS, RGB(0x88B0F0), 1) + 4;
  x = gfx_text(FONT_SMALL, x, y, "Start", RGB(WHITE), 1) + 34;
  x = gfx_text(FONT_SMALL, x, y, SYM_TRIANGLE, RGB(0x60D8A0), 1) + 4;
  x = gfx_text(FONT_SMALL, x, y, "Info", RGB(WHITE), 1) + 34;
  x = gfx_text(FONT_SMALL, x, y, SYM_SQUARE, RGB(0xE890C0), 1) + 4;
  gfx_text(FONT_SMALL, x, y, "Apparaat", RGB(WHITE), 1);
}

static void titles(void) {
  char m[48];
  const struct game *g = &games[sel];
  gfx_center(FONT_BIG, SCR_W / 2, 332, g->title, RGB(WHITE), 1);
  char *e = m;
  e += mx_snprintf(e, sizeof m, "%s  -  %s", g->year, dev_root[dev]);
  gfx_center(FONT_SMALL, SCR_W / 2, 360, m, RGB(GREY), 1);
}

static void panel(float x0, float y0, float x1, float y1) {
  gfx_rect(0, 0, SCR_W, SCR_H, 0x50000000u, 0x50000000u);
  gfx_rect(x0, y0, x1, y1, RGB(0x10262E), RGB(0x0A1820));
  gfx_rect(x0, y0, x1, y0 + 2, RGB(ACCENT), RGB(ACCENT));
}

/* s wrapped at width w from y, lines apart */
static void paragraph(float x, float y, float w, const char *s, u32 col) {
  char line[64];
  while (*s) {
    u32 n = 0, cut = 0;
    while (s[n] && n < sizeof line - 1) {
      line[n] = s[n];
      line[n + 1] = 0;
      if (s[n] == ' ')
        cut = n;
      if (gfx_width(FONT_SMALL, line, 1) > w)
        break;
      n++;
    }
    if (s[n] && cut)
      n = cut;
    line[n] = 0;
    gfx_text(FONT_SMALL, x, y, line, col, 1);
    y += 24;
    s += n;
    while (*s == ' ')
      s++;
  }
}

static void info(void) {
  const struct game *g = &games[sel];
  char path[96];
  panel(70, 96, 570, 352);
  gfx_image(cover_tex0(sel), 90, 116, 240, 322, 0, 0, COVER_W, COVER_H, RGB(0x808080), RGB(0x808080));
  gfx_text(FONT_BIG, 260, 148, g->title, RGB(WHITE), 0.8f);
  float tx = gfx_text(FONT_SMALL, 260, 174, g->year, RGB(ACCENT), 1);
  if (g->type[0])
    gfx_text(FONT_SMALL, tx + (g->year[0] ? 14 : 0), 174, g->type, RGB(GREY), 1);
  paragraph(260, 208, 290, g->desc, RGB(GREY));
  mx_snprintf(path, sizeof path, "%s/PS2MAX/%s", dev_root[dev], g->dir);
  gfx_text(FONT_SMALL, 260, 290, path, RGB(GREY), 0.9f);
  float x = gfx_text(FONT_SMALL, 260, 330, SYM_CROSS, RGB(0x88B0F0), 1) + 4;
  x = gfx_text(FONT_SMALL, x, 330, "Start", RGB(WHITE), 1) + 30;
  x = gfx_text(FONT_SMALL, x, 330, SYM_CIRCLE, RGB(0xF08878), 1) + 4;
  gfx_text(FONT_SMALL, x, 330, "Terug", RGB(WHITE), 1);
}

static void device(void) {
  panel(170, 120, 470, 320);
  gfx_text(FONT_BIG, 192, 164, "Kies een apparaat", RGB(WHITE), 0.7f);
  for (int i = 0; i < NDEVS; i++) {
    float y = 186 + (float)i * 44;
    if (i == dev_sel)
      gfx_rect(186, y, 454, y + 38, 0x30F0A83Cu, 0x18F0A83Cu);
    gfx_text(FONT_SMALL, 200, y + 26, dev_label[i], RGB(WHITE), 1);
    gfx_text(FONT_SMALL, 372, y + 26, dev_root[i], RGB(i == dev ? ACCENT : GREY), 1);
  }
  float x = gfx_text(FONT_SMALL, 200, 300, SYM_CROSS, RGB(0x88B0F0), 1) + 4;
  x = gfx_text(FONT_SMALL, x, 300, "Kiezen", RGB(WHITE), 1) + 30;
  x = gfx_text(FONT_SMALL, x, 300, SYM_CIRCLE, RGB(0xF08878), 1) + 4;
  gfx_text(FONT_SMALL, x, 300, "Terug", RGB(WHITE), 1);
}

static void bar(float x0, float y, float x1, float part) {
  gfx_rect(x0, y, x1, y + 4, RGB(0x203840), RGB(0x203840));
  gfx_rect(x0, y, x0 + (x1 - x0) * part, y + 4, RGB(ACCENT), RGB(ACCENT));
}

static void searching(void) {
  char m[48];
  mx_snprintf(m, sizeof m, "%s zoeken", dev_label[dev]);
  gfx_center(FONT_BIG, SCR_W / 2, 224, m, RGB(WHITE), 0.8f);
  float t = (float)(ticks % 120) / 120;
  bar(220, 250, 420, t);
}

static void empty(void) {
  char m[64];
  gfx_center(FONT_BIG, SCR_W / 2, 200, "Geen spellen gevonden", RGB(WHITE), 0.8f);
  mx_snprintf(m, sizeof m, "Zet spellen in de map PS2MAX op %s", dev_label[dev]);
  gfx_center(FONT_SMALL, SCR_W / 2, 236, m, RGB(GREY), 1);
  float x = 180;
  x = gfx_text(FONT_SMALL, x, 396, SYM_CROSS, RGB(0x88B0F0), 1) + 4;
  x = gfx_text(FONT_SMALL, x, 396, "Opnieuw zoeken", RGB(WHITE), 1) + 34;
  x = gfx_text(FONT_SMALL, x, 396, SYM_SQUARE, RGB(0xE890C0), 1) + 4;
  gfx_text(FONT_SMALL, x, 396, "Apparaat", RGB(WHITE), 1);
}

static void starting(void) {
  char m[64];
  const struct game *g = &games[sel];
  mx_snprintf(m, sizeof m, "%s wordt gestart", g->title);
  gfx_image(cover_tex0(sel), 245, 80, 395, 286, 0, 0, COVER_W, COVER_H, RGB(0x808080), RGB(0x808080));
  gfx_center(FONT_BIG, SCR_W / 2, 330, m, RGB(WHITE), 0.7f);
  bar(220, 350, 420, (float)(ticks - start_at) / 20);
}

/* The crash report, as far as it fits: what the host's symbolize.py needs. */
static void failed_crash(void) {
  char m[96];
  mx_snprintf(m, sizeof m, "%s %s", games[sel].title, "is gecrasht");
  panel(40, 96, 600, 352);
  gfx_center(FONT_BIG, SCR_W / 2, 140, m, RGB(WHITE), 0.7f);
  mx_snprintf(m, sizeof m, "%s op %08x, adres %08x", crash.what, crash.epc, crash.badvaddr);
  gfx_center(FONT_SMALL, SCR_W / 2, 172, m, RGB(WHITE), 0.9f);
  float y = 196;
  /* the note: on two lines, split at a space */
  if (crash.note) {
    u32 n = strlen(crash.note), cut = n;
    if (n > 64)
      for (cut = 64; cut && crash.note[cut] != ' '; cut--)
        ;
    mx_snprintf(m, sizeof m, "%.*s", (int)cut, crash.note);
    gfx_center(FONT_SMALL, SCR_W / 2, y, m, RGB(GREY), 0.8f);
    y += 18;
    if (cut < n) {
      gfx_center(FONT_SMALL, SCR_W / 2, y, crash.note + cut + 1, RGB(GREY), 0.8f);
      y += 18;
    }
  }
  y += 6;
  for (u32 i = 0; i < crash.ntrace; i += 6) {
    u32 k = 0;
    for (u32 j = i; j < i + 6 && j < crash.ntrace; j++)
      k += (u32)mx_snprintf(m + k, sizeof m - k, "%08x ", crash.trace[j]);
    gfx_center(FONT_SMALL, SCR_W / 2, y, m, RGB(GREY), 0.8f);
    y += 18;
  }
  float x = 296;
  x = gfx_text(FONT_SMALL, x, 336, SYM_CIRCLE, RGB(0xF08878), 1) + 4;
  gfx_text(FONT_SMALL, x, 336, "Terug", RGB(WHITE), 1);
}

static void failed(void) {
  if (crashed) {
    failed_crash();
    return;
  }
  char m[64];
  mx_snprintf(m, sizeof m, "%s stopte met een fout", games[sel].title);
  panel(90, 150, 550, 300);
  gfx_center(FONT_BIG, SCR_W / 2, 200, m, RGB(WHITE), 0.7f);
  mx_snprintf(m, sizeof m, "%s/PS2MAX/%s/%s", dev_root[dev], games[sel].dir, games[sel].elf);
  gfx_center(FONT_SMALL, SCR_W / 2, 232, m, RGB(GREY), 0.9f);
  mx_snprintf(m, sizeof m, "Exit-status %d", back_status);
  gfx_center(FONT_SMALL, SCR_W / 2, 256, m, RGB(GREY), 0.9f);
  float x = 296;
  x = gfx_text(FONT_SMALL, x, 286, SYM_CIRCLE, RGB(0xF08878), 1) + 4;
  gfx_text(FONT_SMALL, x, 286, "Terug", RGB(WHITE), 1);
}

static void draw(void) {
  gfx_begin();
  if (covers_new) {
    cover_upload(covers_new);
    covers_new = 0;
  }
  background();
  header();
  switch (screen) {
  case S_SEARCH:
    searching();
    break;
  case S_EMPTY:
    empty();
    break;
  case S_START:
    starting();
    break;
  default:
    carousel();
    titles();
    footer();
    if (screen == S_INFO)
      info();
    else if (screen == S_DEVICE)
      device();
    else if (screen == S_FAILED)
      failed();
    break;
  }
  gfx_end();
}

static void search(int d) {
  dev = d;
  dev_ready = 0;
  dev_restart();
  screen = S_SEARCH;
}

static void found(void) {
  dev_ready = ngames > 0;
  sel = 0;
  place = 0;
  pos = 0;
  for (u32 i = 0; i < ngames; i++) {
    char path[128];
    game_file(path, dev, &games[i], games[i].cover);
    if (!games[i].cover[0] || cover_load(i, path) < 0)
      cover_make(i, games[i].title);
  }
  covers_new = ngames;
  screen = ngames ? S_MAIN : S_EMPTY;
}

/* The carousel on the game that ended, its failure on the screen. */
static void came_back(void) {
  if (!back_path)
    return;
  for (u32 i = 0; i < ngames; i++) {
    char m[48];
    mx_snprintf(m, sizeof m, "/PS2MAX/%s/", games[i].dir);
    if (contains(back_path, m)) {
      place = (s32)i, sel = i, pos = (float)i;
      if (back_status) {
        crashed = mx_crash_last(&crash);
        screen = S_FAILED;
      }
    }
  }
  back_path = 0;
}

static void input(void) {
  u32 p = pad_on ? mx_pad_pressed(0) | mx_pad_pressed(1) : 0;
  u32 held = pad_on ? mx_pad_buttons(0) | mx_pad_buttons(1) : 0;
  static u32 hold;
  hold = held & (MX_PAD_LEFT | MX_PAD_RIGHT) ? hold + 1 : 0;
  if (hold > 24 && !(hold & 7))
    p |= held & (MX_PAD_LEFT | MX_PAD_RIGHT);
  switch (screen) {
  case S_MAIN:
    if (p & MX_PAD_LEFT && (ngames >= ROUND || place > 0))
      place--;
    if (p & MX_PAD_RIGHT && (ngames >= ROUND || place + 1 < (s32)ngames))
      place++;
    sel = (u32)game_at(place);
    if (p & MX_PAD_TRIANGLE)
      screen = S_INFO;
    if (p & MX_PAD_CROSS)
      screen = S_START, start_at = ticks;
    /* fall through */
  case S_EMPTY:
    if (screen == S_EMPTY && p & MX_PAD_CROSS)
      search(dev);
    if (p & MX_PAD_SQUARE)
      screen = S_DEVICE, dev_sel = dev;
    if (p & MX_PAD_SELECT)
      mx_exit(0);
    break;
  case S_INFO:
    if (p & MX_PAD_CIRCLE)
      screen = S_MAIN;
    if (p & MX_PAD_CROSS)
      screen = S_START, start_at = ticks;
    break;
  case S_DEVICE:
    if (p & (MX_PAD_UP | MX_PAD_DOWN))
      dev_sel = !dev_sel;
    if (p & MX_PAD_CIRCLE)
      screen = ngames ? S_MAIN : S_EMPTY;
    if (p & MX_PAD_CROSS) {
      if (dev_sel != dev)
        dev_save(dev_sel);
      search(dev_sel);
    }
    break;
  case S_FAILED:
    if (p & (MX_PAD_CIRCLE | MX_PAD_CROSS))
      screen = S_MAIN;
    break;
  }
}

/* Arguments, for tests from the host: dev=mx4sio, right=<n> (that many games
   to the right), info, device, shot (the picture to host:shell.tga, then the
   end). */
static int test_shot, test_right, test_screen = -1;
static void arguments(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (contains(a, ":") && i + 1 < argc) {
      back_path = a;
      const char *s = argv[++i];
      int neg = *s == '-';
      for (s += neg; *s >= '0' && *s <= '9'; s++)
        back_status = back_status * 10 + *s - '0';
      back_status = neg ? -back_status : back_status;
    } else if (!strcmp(a, "dev=mx4sio"))
      dev = DEV_MX4SIO;
    else if (!strcmp(a, "dev=usb"))
      dev = DEV_USB;
    else if (!strcmp(a, "shot"))
      test_shot = 1;
    else if (!strcmp(a, "info"))
      test_screen = S_INFO;
    else if (!strcmp(a, "device"))
      test_screen = S_DEVICE;
    else if (!strcmp(a, "start"))
      test_screen = S_START;
    else if (!strncmp(a, "right=", 6))
      test_right = a[6] - '0';
  }
}

int main(int argc, char **argv) {
  mx_sif_init();
  dev = dev_load_saved();
  arguments(argc, argv);
  if (gfx_init() < 0) {
    mx_printf("shell: no picture\n");
    return 1;
  }
  pad_on = mx_pad_init() >= 0;
  search(dev);
  u32 shot_at = 0;
  for (;;) {
    u32 v = mx_gs.vsync;
    ticks++;
    if (screen == S_SEARCH && ticks % 15 == 0) {
      int r = dev_step(dev);
      if (r < 0)
        ngames = 0;
      if (r != 0) {
        found();
        came_back();
        if (test_right)
          place = test_right, sel = (u32)game_at(place), pos = (float)place;
        if (test_screen >= 0 && ngames)
          screen = test_screen, start_at = ticks;
        shot_at = ticks + 90;
      }
    }
    input();
    pos += ((float)place - pos) * 0.2f;
    draw();
    if (screen == S_START && ticks - start_at > 20) {
      game_launch(dev, &games[sel]);
      /* the picture is stopped: the failure shows on mxlink's screen */
      mx_exit(1);
    }
    if (test_shot && shot_at && ticks >= shot_at) {
      int r = mx_hires_save_tga("host:shell.tga");
      mx_printf("shell: %d, late %u\n", r, mx_hires_stats.late);
      MX_TEST_END(r ? 1 : 0);
      return 0;
    }
    while (mx_gs.vsync == v)
      ;
  }
}
