/* mxlink, the host link, as an app (mx OS step 2e-4; libs/maxsdk/docs/
   mx-os-apps-plan.md): a service beside every program in front, on the
   kernel's general interface only (kernel version 20). A text service: what
   it has to say goes to the kernel's log (mx_printf), which the shell shows
   as its console, and the host's console; no screen of its own.

   - ps2client's commands, which the kernel passes on by the host link's IOP
     channel (MX_IOP_CHANNEL_HOST): execee, dumpreg (ps2client's ping), and
     from kernel version 21 dumpmem, writemem, gsexec, startvu and stopvu;
     the kernel keeps the reset and the IOP's exception, and serves all of
     them while this service is not there (a game that took all memory, a
     ps2sdk program);
   - the kernel's events: the end of a program the host started, with the
     test end marker for the host's runner (the kernel prints none while a
     service is beside);
   - its port "mxlink": requests from other processes (the shell): "status";
   - the log, read as the host's console will be; counted for now.

   Started by the host (`execee host:mxlink.elf marker`), it prints the
   end marker itself once it is the service, so that the run ends there. */

#include <mx/debug.h>
#include <mx/hw/formats.h>
#include <mx/mem.h>
#include <mx/cpu.h>
#include <mx/hw/regs.h>
#include <mx/iop.h>
#include <mx/kernel.h>
#include <mx/loader.h>
#include <mx/log.h>
#include <mx/msg.h>
#include <mx/proc.h>
#include <mx/sif.h>
#include <mx/string.h>
#include <mx/thread.h>

MX_CORE_HEAP(128 << 10);

static int events, requests, channel;
static volatile u32 ends, last_status, host_cmds, log_pieces, log_bytes;
/* the program the host started, until its end */
static volatile int host_child;

/* ---- ps2client's commands (apps/mxlink/iop/hostlink.h) ------------------ */

#define PKO_EXECEE_CMD 0xbabe0203u
#define PKO_DUMP_MEM 0xbabe0207u
#define PKO_START_VU 0xbabe0208u
#define PKO_STOP_VU 0xbabe0209u
#define PKO_DUMP_REG 0xbabe020au
#define PKO_GSEXEC_CMD 0xbabe020bu
#define PKO_WRITE_MEM 0xbabe020cu
#define PKO_MAX_PATH 256
#define HDR 6 /* cmd (4 bytes), len (2), packed, big-endian */

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static u16 be16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }

/* the program in front: its process, 0 none; *shell when it is the shell */
static int front(int *shell) {
  struct mx_proc_info p[4];
  int n = mx_proc_list(p, 4);
  *shell = 0;
  for (int i = 0; i < n && i < 4; i++)
    if (p[i].state == MX_PROC_FRONT) {
      u32 l = strlen(p[i].name);
      *shell = l >= 11 && !strcmp(p[i].name + l - 11, "MXSHELL.ELF");
      return p[i].id;
    }
  return 0;
}

/* whether process id is the shell; 0 also when it is gone */
static int proc_shell(int id, int *found) {
  struct mx_proc_info p[4];
  int n = mx_proc_list(p, 4);
  *found = 0;
  for (int i = 0; i < n && i < 4; i++)
    if (p[i].id == id) {
      u32 l = strlen(p[i].name);
      *found = 1;
      return l >= 11 && !strcmp(p[i].name + l - 11, "MXSHELL.ELF");
    }
  return 0;
}

/* Nothing in front, for a command of the host: the shell ends (the text
   screen after it, until the next reset); another program stays and the
   command is refused. 0, or -1. */
static int front_free(const char *what) {
  int shell, id = front(&shell);
  if (!id)
    return 0;
  if (!shell) {
    mx_printf("mxlink: %s: not while a program runs\n", what);
    return -1;
  }
  int st;
  if (mx_proc_kill(id, 0) < 0 || mx_proc_wait(id, &st, 0) < 0) {
    mx_printf("mxlink: %s: the shell did not end\n", what);
    return -1;
  }
  return 0;
}

/* execee: argc, then the path and the arguments, each with its 0 */
static void execee(const u8 *p, u32 len) {
  int argc = (int)be32(p + HDR);
  const char *args = (const char *)p + HDR + 4;
  u32 room = len - HDR - 4, n = 0;
  for (int i = 0; i < argc; i++) {
    while (n < room && args[n])
      n++;
    if (n++ >= room) {
      mx_printf("mxlink: execee: broken arguments\n");
      return;
    }
  }
  if (argc < 1 || front_free("execee") < 0)
    return;
  int id = mx_proc_spawn(argc, args, n, 0);
  if (id < 0) {
    mx_printf("mxlink: execee %s: did not start\n", args);
    /* the run's end: a refused program (not an MXOS one) ends it as a
       program that could not be found would (127) */
    mx_printf("@@PS2TEST_END@@ status=127\n");
    return;
  }
  host_child = id;
}

/* a path of the packet; without a device it is ps2client's (host:), as
   ps2client's ping (a dumpreg to "ps2client.ping") expects */
static void path_of(char *d, const char *s) {
  int dev = 0;
  for (const char *t = s; *t; t++)
    dev |= *t == ':';
  if (dev)
    mx_snprintf(d, PKO_MAX_PATH, "%s", s);
  else
    mx_snprintf(d, PKO_MAX_PATH, "host:%s", s);
}

/* dumpreg: the register sets of ps2link */
static const u32 dma_regs[] = {
    0x1000e000, 0x1000e010, 0x1000e020, 0x1000e030, 0x1000e040, 0x1000e050, 0x1000e060,
    0x1000f520, 0x10008000, 0x10008010, 0x10008020, 0x10008030, 0x10008040, 0x10008050,
    0x10009000, 0x10009010, 0x10009020, 0x10009030, 0x10009040, 0x10009050, 0x1000a000,
    0x1000a010, 0x1000a020, 0x1000a030, 0x1000a040, 0x1000a050, 0x1000b000, 0x1000b010,
    0x1000b020, 0x1000b400, 0x1000b410, 0x1000b420, 0x1000b430, 0x1000c000, 0x1000c010,
    0x1000c020, 0x1000c400, 0x1000c410, 0x1000c420, 0x1000c430, 0x1000c800, 0x1000c810,
    0x1000c820, 0x1000d000, 0x1000d010, 0x1000d020, 0x1000d080, 0x1000d400, 0x1000d410,
    0x1000d420, 0x1000d430, 0x1000d480};
static const u32 intc_regs[] = {0x1000F000, 0x1000F010};
static const u32 timer_regs[] = {0x10000000, 0x10000010, 0x10000020, 0x10000030, 0x10000800,
                                 0x10000810, 0x10000820, 0x10000830, 0x10001000, 0x10001010,
                                 0x10001020, 0x10001800, 0x10001810, 0x10001820};
static const u32 gs_regs[] = {0x12001000, 0x12001080};
static const u32 sif_regs[] = {0x1000F230};
static const u32 fifo_regs[] = {0x10005000, 0x10007000};
static const u32 gif_regs[] = {0x10003020, 0x10003040, 0x10003050, 0x10003060,
                               0x10003070, 0x10003080, 0x10003090, 0x100030a0};
static const u32 vif0_regs[] = {0x10003800, 0x10003810, 0x10003820, 0x10003830, 0x10003840,
                                0x10003850, 0x10003860, 0x10003870, 0x10003880, 0x10003890,
                                0x100038d0, 0x10003900, 0x10003910, 0x10003920, 0x10003930,
                                0x10003940, 0x10003950, 0x10003960, 0x10003970};
static const u32 vif1_regs[] = {0x10003c00, 0x10003c10, 0x10003c20, 0x10003c30, 0x10003c40,
                                0x10003c50, 0x10003c60, 0x10003c70, 0x10003c80, 0x10003c90,
                                0x10003ca0, 0x10003cb0, 0x10003cc0, 0x10003cd0, 0x10003ce0,
                                0x10003d00, 0x10003d10, 0x10003d20, 0x10003d30, 0x10003d40,
                                0x10003d50, 0x10003d60, 0x10003d70};
static const u32 ipu_regs[] = {0x10002000, 0x10002010, 0x10002020, 0x10002030};

#define SET(a) {a, sizeof a / sizeof a[0]}
static const struct {
  const u32 *r;
  u32 n;
} reg_sets[] = {/* in ps2link's order: REGDMA .. REGIPU, REGALL (10) is all of them */
                SET(dma_regs),  SET(intc_regs), SET(timer_regs), SET(gs_regs),  SET(sif_regs),
                SET(fifo_regs), SET(gif_regs),  SET(vif0_regs),  SET(vif1_regs), SET(ipu_regs)};

static void dump_reg(const u8 *p) {
  static u32 w[160] MX_ALIGNED(64);
  u32 which = be32(p + HDR), n = 0;
  char path[PKO_MAX_PATH];
  path_of(path, (const char *)p + HDR + 4);
  if (front_free("dumpreg") < 0)
    return;
  for (u32 s = 0; s < 10; s++)
    if (which == s || which == 10)
      for (u32 i = 0; i < reg_sets[s].n && n < sizeof w / 4; i++)
        w[n++] = MX_R32(reg_sets[s].r[i]);
  if (!n) {
    /* REGVU0, REGVU1: not yet */
    mx_printf("mxlink: dumpreg %u: not supported\n", which);
    return;
  }
  int fd = mx_fio_open(path, MX_FIO_WRONLY | MX_FIO_CREAT | MX_FIO_TRUNC);
  if (fd < 0) {
    mx_printf("mxlink: dumpreg: cannot open %s\n", path);
    return;
  }
  mx_fio_write(fd, w, n * 4);
  mx_fio_close(fd);
}

/* dumpmem and writemem: offset, size, path; the EE's physical memory, as the
   kernel sees it (kernel version 21) */
static u8 data_buf[8 << 10] MX_ALIGNED(64);

static void dump_mem(const u8 *p) {
  u32 off = be32(p + HDR), size = be32(p + HDR + 4);
  char path[PKO_MAX_PATH];
  path_of(path, (const char *)p + HDR + 8);
  if (front_free("dumpmem") < 0)
    return;
  mx_printf("mxlink: dumpmem %08x, %u bytes -> %s\n", off, size, path);
  int fd = mx_fio_open(path, MX_FIO_WRONLY | MX_FIO_CREAT | MX_FIO_TRUNC);
  if (fd < 0) {
    mx_printf("mxlink: dumpmem: cannot open %s\n", path);
    return;
  }
  for (u32 done = 0; done < size;) {
    u32 n = size - done < sizeof data_buf ? size - done : sizeof data_buf;
    if (mx_phys_read(off + done, data_buf, n) < 0 || mx_fio_write(fd, data_buf, n) < 0) {
      mx_printf("mxlink: dumpmem: failed at %08x\n", off + done);
      break;
    }
    done += n;
  }
  mx_fio_close(fd);
}

static void write_mem(const u8 *p) {
  u32 off = be32(p + HDR), size = be32(p + HDR + 4);
  char path[PKO_MAX_PATH];
  path_of(path, (const char *)p + HDR + 8);
  if (front_free("writemem") < 0)
    return;
  mx_printf("mxlink: writemem %s -> %08x, %u bytes\n", path, off, size);
  int fd = mx_fio_open(path, MX_FIO_RDONLY);
  if (fd < 0) {
    mx_printf("mxlink: writemem: cannot open %s\n", path);
    return;
  }
  for (u32 done = 0; done < size;) {
    u32 n = size - done < sizeof data_buf ? size - done : sizeof data_buf;
    int r = mx_fio_read(fd, data_buf, n);
    if (r <= 0 || mx_phys_write(off + done, data_buf, (u32)r) < 0) {
      mx_printf("mxlink: writemem: failed at %08x\n", off + done);
      break;
    }
    done += (u32)r;
  }
  mx_fio_close(fd);
}

/* gsexec: size, path: up to 128 bytes of GIF packet sent by PATH3 */
static void gs_exec(const u8 *p) {
  static u8 gif[128] MX_ALIGNED(64);
  u32 size = be16(p + HDR);
  char path[PKO_MAX_PATH];
  path_of(path, (const char *)p + HDR + 2);
  if (front_free("gsexec") < 0)
    return;
  int fd = mx_fio_open(path, MX_FIO_RDONLY);
  if (fd < 0) {
    mx_printf("mxlink: gsexec: cannot open %s\n", path);
    return;
  }
  int n = mx_fio_read(fd, gif, sizeof gif);
  mx_fio_close(fd);
  if (n < 0)
    return;
  if (size > sizeof gif)
    size = sizeof gif;
  mx_printf("mxlink: gsexec %s, %u bytes\n", path, size);
  mx_dcache_writeback(gif, sizeof gif);
  MX_R32(MX_DMA_GIF_QWC) = size / 16;
  MX_R32(MX_DMA_GIF_MADR) = mx_paddr_of(gif).v;
  MX_R32(MX_DMA_GIF_CHCR) = 0x101;
  for (int i = 0; i < 1000 && (MX_R32(MX_DMA_GIF_CHCR) & 0x100); i++)
    mx_sleep_us(100);
}

/* ctc2 $8, vi28 (FBRST) as a word: the assembler takes it only with -mvu0 */
static void fbrst(u32 v) {
  __asm__ volatile("move $8, %0\n\t.word 0x48c8e000\n\tsync.p" : : "r"(v) : "$8", "memory");
}

/* startvu, stopvu: the VU's reset or force break, and its VIF's */
static void vu(const u8 *p, int start) {
  u32 vpu = be32(p + HDR);
  if (vpu > 1)
    return;
  mx_printf("mxlink: %s VU%u\n", start ? "startvu" : "stopvu", vpu);
  fbrst((start ? 2u : 1u) << (vpu * 8));
  MX_R32(vpu ? 0x10003C10u : 0x10003810u) = start ? 8 : 2;
}

/* the kernel's packets of the host link, one at a time */
static void on_host(void *arg) {
  (void)arg;
  static u8 buf[MX_MSG_MAX + 1];
  for (;;) {
    struct mx_msg m;
    if (mx_msg_receive(channel, buf, MX_MSG_MAX, &m) < 0)
      return;
    u32 len = m.len < MX_MSG_MAX ? m.len : MX_MSG_MAX;
    buf[len] = 0;
    if (m.from != 0 || len < HDR + 3)
      continue;
    host_cmds++;
    switch (be32(buf)) {
    case PKO_EXECEE_CMD:
      execee(buf, len);
      break;
    case PKO_DUMP_REG:
      dump_reg(buf);
      break;
    case PKO_DUMP_MEM:
      dump_mem(buf);
      break;
    case PKO_WRITE_MEM:
      write_mem(buf);
      break;
    case PKO_GSEXEC_CMD:
      gs_exec(buf);
      break;
    case PKO_START_VU:
    case PKO_STOP_VU:
      vu(buf, be32(buf) == PKO_START_VU);
      break;
    }
  }
}

/* ---- the rest ----------------------------------------------------------- */

/* the kernel's events: a program in front ended */
static void on_events(void) {
  for (;;) {
    struct mx_event e;
    struct mx_msg m;
    if (mx_msg_receive(events, &e, sizeof e, &m) < 0)
      return;
    if (m.from != 0 || e.type != MX_EVENT_END)
      continue;
    ends++;
    last_status = (u32)e.status;
    if (e.proc == host_child) {
      host_child = 0;
      mx_printf("@@PS2TEST_END@@ status=%d\n", e.status);
    }
  }
}

/* requests on "mxlink": "status" answers a line of text */
static void serve(void *arg) {
  (void)arg;
  static char req[MX_MSG_MAX], rep[MX_MSG_MAX];
  for (;;) {
    struct mx_msg m;
    int id = mx_msg_receive(requests, req, sizeof req - 1, &m);
    if (id < 0)
      return;
    req[m.len < sizeof req ? m.len : sizeof req - 1] = 0;
    int n, shell;
    if (!strcmp(req, "status"))
      n = mx_snprintf(rep, sizeof rep,
                      "mxlink: front %d, %u ends (last %d), %u host commands, log %u pieces %u bytes",
                      front(&shell), ends, (int)last_status, host_cmds, log_pieces, log_bytes);
    else
      n = mx_snprintf(rep, sizeof rep, "mxlink: unknown request");
    mx_msg_reply(id, rep, (u32)n + 1);
  }
}

/* the log, as the host's console will read it */
static void on_log(void *arg) {
  (void)arg;
  u32 pos = 0;
  static char buf[80];
  for (;;) {
    int n = mx_log_read(&pos, buf, sizeof buf, 0, 1);
    if (n < 0)
      return;
    log_pieces++;
    log_bytes += (u32)n;
  }
}

/* a test (PCSX2, no host link there): the shell in front ends after 3 s as
   for a host's command; then the program arg names starts as an execee's
   (its end marker ends the run), or without one the end marker says how */
static void test_stop(void *arg) {
  const char *prog = arg;
  mx_sleep_us(3000000);
  int shell, id = front(&shell);
  mx_printf("mxlink: test: in front %d (shell %d); ending it\n", id, shell);
  int r = front_free("test");
  mx_printf("mxlink: test: %d, in front now %d\n", r, front(&shell));
  if (prog && r == 0) {
    int c = mx_proc_spawn(1, prog, (u32)strlen(prog) + 1, 0);
    mx_printf("mxlink: test: %s: process %d\n", prog, c);
    if (c > 0) {
      host_child = c;
      return;
    }
  }
  mx_printf("@@PS2TEST_END@@ status=%d\n", r < 0 || front(&shell) ? 1 : 0);
}

static u8 stacks[4][8 << 10] MX_ALIGNED(16) MX_CORE_DATA; /* stacks: core */

int main(int argc, char **argv) {
  if (!mx__kernel || mx__kernel->version < 18)
    return 1;
  if (mx__kernel->version < 20) {
    /* the kernel serves the host itself: a service that does nothing, so
       that the shell comes (an older kernel would start it again and again) */
    mx_proc_service();
    for (;;)
      mx_sleep_us(1000000);
  }
  events = mx_port_create("mxlink.events");
  requests = mx_port_create("mxlink");
  channel = mx_port_create("mxlink.host");
  if (events < 0 || requests < 0 || channel < 0 || mx_proc_events(events) < 0 ||
      mx_iop_channel(MX_IOP_CHANNEL_HOST, channel) < 0) {
    mx_printf("mxlink: no ports, events or the host link's channel\n");
    return 1;
  }
  mx_thread_start(serve, 0, stacks[0], sizeof stacks[0], 40);
  mx_thread_start(on_host, 0, stacks[1], sizeof stacks[1], 40);
  mx_thread_start(on_log, 0, stacks[2], sizeof stacks[2], 100);
  int first = mx_proc_service();
  if (first < 0) {
    mx_printf("mxlink: cannot be a service here\n");
    return 1;
  }
  mx_printf("mxlink: the host link, a service (process %d); in front: process %d\n",
            mx_proc_self(), first);
  /* in front: the shell, or none; on PCSX2 host:mxload.txt's, the run's */
  /* by its id, in front or not: it may have started another beside it
     already (then the other is in front) */
  int found, shell = first > 0 ? proc_shell(first, &found) : 0;
  if (first > 0 && found && !shell)
    host_child = first;
  if (argc > 1 && !strcmp(argv[1], "test-stop"))
    mx_thread_start(test_stop, argc > 2 ? argv[2] : 0, stacks[3], sizeof stacks[3], 40);
  if (argc > 1 && !strcmp(argv[1], "marker"))
    mx_printf("@@PS2TEST_END@@ status=0\n");
  on_events();
  return 0;
}
