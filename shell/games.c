/* What the shell plays: the storage drivers of the device in use, loaded from
   mc0:/MXOS only when it is chosen (mx4sio_bd polls the SIO2 without a card,
   which makes the pad stutter), and the games in <device>/PS2MAX/<game>/. */

#include <mx/debug.h>
#include <mx/iop.h>
#include <mx/loader.h>
#include <mx/mx.h>
#include <mx/sif.h>
#include <mx/string.h>

#include "shell.h"

#define MXOS "mc0:/MXOS/"
#define CONFIG MXOS "SHELL.CFG"
#define MAX_TRIES 40 /* looks for the directory, a quarter of a second apart */

struct game games[MAX_GAMES];
u32 ngames;

const char *const dev_label[NDEVS] = {"USB", "MX4SIO"};
const char *const dev_root[NDEVS] = {"usb0:", "mx4sio0:"};

/* the file, and the name the IOP knows the module by */
static const char *const mods[NDEVS][4][2] = {
    {{"bdm", "bdm"}, {"bdmfs_fatfs", "bdmff"}, {"usbd", "USB_driver"}, {"usbmass_bd", "usbmass_bd"}},
    {{"bdm", "bdm"}, {"bdmfs_fatfs", "bdmff"}, {"mx4sio_bd", "mx4sio"}, {0, 0}},
};

static char *cat(char *d, const char *s) {
  while (*s)
    *d++ = *s++;
  *d = 0;
  return d;
}

int dev_load_saved(void) {
  char b[16] = {0};
  int fd = mx_fio_open(CONFIG, MX_FIO_RDONLY);
  if (fd < 0)
    return DEV_USB;
  int n = mx_fio_read(fd, b, sizeof b - 1);
  mx_fio_close(fd);
  return n > 0 && b[0] == 'm' ? DEV_MX4SIO : DEV_USB;
}

void dev_save(int dev) {
  int fd = mx_fio_open(CONFIG, MX_FIO_WRONLY | MX_FIO_CREAT | MX_FIO_TRUNC);
  if (fd < 0)
    return;
  mx_fio_write(fd, dev == DEV_MX4SIO ? "mx4sio\n" : "usb\n", dev == DEV_MX4SIO ? 7 : 4);
  mx_fio_close(fd);
}

static u8 irx[128 * 1024] MX_ALIGNED(64);

static int load(const char *file) {
  char path[48];
  cat(cat(cat(path, MXOS), file), ".irx");
  int fd = mx_fio_open(path, MX_FIO_RDONLY);
  if (fd < 0) {
    mx_printf("shell: no %s\n", path);
    return -1;
  }
  int n = mx_fio_read(fd, irx, sizeof irx), res = 0;
  mx_fio_close(fd);
  if (n <= 0 || n == (int)sizeof irx)
    return -1;
  int id = mx_iop_load_module_buffer(irx, (u32)n, 0, 0, &res);
  mx_printf("shell: %s: id %d, result %d\n", file, id, res);
  return id < 0 ? -1 : 0;
}

/* key=value of game.ini into the game */
static void ini_line(struct game *g, const char *k, const char *v) {
  static const struct {
    const char *key;
    u32 off, size;
  } f[] = {
      {"title", __builtin_offsetof(struct game, title), sizeof g->title},
      {"year", __builtin_offsetof(struct game, year), sizeof g->year},
      {"type", __builtin_offsetof(struct game, type), sizeof g->type},
      {"cover", __builtin_offsetof(struct game, cover), sizeof g->cover},
      {"elf", __builtin_offsetof(struct game, elf), sizeof g->elf},
      {"args", __builtin_offsetof(struct game, args), sizeof g->args},
      {"description", __builtin_offsetof(struct game, desc), sizeof g->desc},
  };
  for (u32 i = 0; i < sizeof f / sizeof *f; i++)
    if (!strcmp(k, f[i].key)) {
      char *d = (char *)g + f[i].off;
      u32 n = 0;
      for (; v[n] && n < f[i].size - 1; n++)
        d[n] = v[n];
      d[n] = 0;
    }
}

void game_file(char *out, int dev, const struct game *g, const char *file) {
  cat(cat(cat(cat(cat(out, dev_root[dev]), "/PS2MAX/"), g->dir), "/"), file);
}

/* Without a game.ini a directory with <directory>.elf is a game too. */
static int no_ini(int dev, struct game *g) {
  char path[128];
  cat(cat(g->elf, g->dir), ".elf");
  game_file(path, dev, g, g->elf);
  int fd = mx_fio_open(path, MX_FIO_RDONLY);
  if (fd < 0)
    return -1;
  mx_fio_close(fd);
  cat(g->title, g->dir);
  return 0;
}

static int read_ini(int dev, struct game *g) {
  static char b[1024];
  char path[128];
  game_file(path, dev, g, "game.ini");
  int fd = mx_fio_open(path, MX_FIO_RDONLY);
  if (fd < 0)
    return no_ini(dev, g);
  int n = mx_fio_read(fd, b, sizeof b - 1);
  mx_fio_close(fd);
  if (n <= 0)
    return -1;
  b[n] = 0;
  for (char *s = b; *s;) {
    char *e = s, *eq = 0;
    while (*e && *e != '\n') {
      if (*e == '=' && !eq)
        eq = e;
      e++;
    }
    int more = *e != 0;
    *e = 0;
    if (e > s && e[-1] == '\r')
      e[-1] = 0;
    if (eq) {
      *eq = 0;
      ini_line(g, s, eq + 1);
    }
    s = more ? e + 1 : e;
  }
  return g->title[0] && g->elf[0] ? 0 : -1;
}

static void sort_games(void) {
  for (u32 i = 1; i < ngames; i++) {
    struct game t = games[i];
    u32 j = i;
    for (; j > 0 && strcmp(games[j - 1].title, t.title) > 0; j--)
      games[j] = games[j - 1];
    games[j] = t;
  }
}

static void scan(int dev, int dir) {
  char name[64];
  u32 size;
  ngames = 0;
  while (ngames < MAX_GAMES && mx_fio_dread(dir, name, sizeof name, &size) > 0) {
    if (name[0] == '.' || strlen(name) >= sizeof games[0].dir)
      continue;
    struct game *g = &games[ngames];
    memset(g, 0, sizeof *g);
    cat(g->dir, name);
    if (read_ini(dev, g) == 0)
      ngames++;
  }
  sort_games();
  mx_printf("shell: %u games on %s\n", ngames, dev_root[dev]);
}

static u32 step, tries;

void dev_restart(void) { step = tries = 0; }

int dev_step(int dev) {
  while (step < 4 && mods[dev][step][0]) {
    const char *const *m = mods[dev][step++];
    struct mx_iop_module info;
    if (mx_iop_find_module(m[1], &info) > 0)
      continue;
    return load(m[0]) < 0 ? -1 : 0;
  }
  char path[32];
  cat(cat(path, dev_root[dev]), "/PS2MAX");
  int dir = mx_fio_dopen(path);
  if (dir < 0) {
    if (++tries < MAX_TRIES)
      return 0;
    ngames = 0;
    return 1;
  }
  scan(dev, dir);
  mx_fio_dclose(dir);
  return 1;
}

int game_launch(int dev, const struct game *g) {
  static char args[256], swap[48];
  game_file(args, dev, g, g->elf);
  char *e = args + strlen(args) + 1;
  int argc = 1;
  for (const char *s = g->args; *s;) {
    while (*s == ' ')
      s++;
    if (!*s)
      break;
    while (*s && *s != ' ')
      *e++ = *s++;
    *e++ = 0;
    argc++;
  }
  cat(cat(swap, dev_root[dev]), "/PS2MAX/swap.bin");
  int fd = mx_fio_open(swap, MX_FIO_RDONLY);
  if (fd >= 0)
    mx_fio_close(fd);
  gfx_stop();
  return mx_loader_launch(argc, args, (u32)(e - args), fd >= 0 ? swap : 0);
}
