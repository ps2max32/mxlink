/* mxlink, the host link, as an app (mx OS step 2e-4; libs/maxsdk/docs/
   mx-os-apps-plan.md): a service beside every program in front, on the
   kernel's general interface only (kernel version 18). A text service: what
   it has to say goes to the kernel's log (mx_printf), which the shell shows
   as its console; no screen of its own.

   The skeleton, before the kernel's command thread (apps/mxos/ee/cmd.c)
   moves here:
   - the kernel's events (a program's end): its exit status, and the test
     end marker for the host's runner (the kernel prints none while a
     service is beside);
   - its port "mxlink": requests from other processes (the shell): "status";
   - its IOP channel (MXLINK_CID): what ps2link.irx will send by SIF command
     in place of its DMA to the kernel's 0xFF800; counted for now;
   - the log, read as the host's console will be; counted for now. */

#include <mx/debug.h>
#include <mx/iop.h>
#include <mx/loader.h>
#include <mx/log.h>
#include <mx/msg.h>
#include <mx/proc.h>
#include <mx/string.h>
#include <mx/thread.h>

MX_CORE_HEAP(128 << 10);

#define MXLINK_CID 18 /* ps2link.irx's SIF command to it (0-31) */

static int events, requests, channel;
static volatile u32 ends, last_status, iop_msgs, log_pieces, log_bytes;
static volatile int front;

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
    if (e.proc == front)
      front = 0;
    mx_printf("mxlink: process %d ended, exit status %d\n", e.proc, e.status);
    mx_printf("@@PS2TEST_END@@ status=%d\n", e.status);
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
    int n;
    if (!strcmp(req, "status"))
      n = mx_snprintf(rep, sizeof rep,
                      "mxlink: front %d, %u ends (last %d), %u IOP messages, log %u pieces %u bytes",
                      front, ends, (int)last_status, iop_msgs, log_pieces, log_bytes);
    else
      n = mx_snprintf(rep, sizeof rep, "mxlink: unknown request");
    mx_msg_reply(id, rep, (u32)n + 1);
  }
}

/* ps2link.irx's commands, by its IOP channel */
static void on_iop(void *arg) {
  (void)arg;
  static u8 buf[MX_MSG_MAX];
  for (;;) {
    if (mx_msg_receive(channel, buf, sizeof buf, 0) < 0)
      return;
    iop_msgs++;
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

static u8 stacks[3][8 << 10] MX_ALIGNED(16);

int main(void) {
  events = mx_port_create("mxlink.events");
  requests = mx_port_create("mxlink");
  channel = mx_port_create("mxlink.iop");
  if (events < 0 || requests < 0 || channel < 0 || mx_proc_events(events) < 0 ||
      mx_iop_channel(MXLINK_CID, channel) < 0) {
    mx_printf("mxlink: needs mx-kernel 18 (ports, events, IOP channels)\n");
    return 1;
  }
  mx_thread_start(serve, 0, stacks[0], sizeof stacks[0], 40);
  mx_thread_start(on_iop, 0, stacks[1], sizeof stacks[1], 40);
  mx_thread_start(on_log, 0, stacks[2], sizeof stacks[2], 100);
  front = mx_proc_service();
  mx_printf("mxlink: a service (process %d); in front: process %d\n", mx_proc_self(), front);
  on_events();
  return 0;
}
