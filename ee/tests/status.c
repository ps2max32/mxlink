/* A program in front beside mxlink (the app, ee/main.c): it asks mxlink's
   port for its state and checks the answer. mxlink prints the end marker
   with this one's exit status. On PCSX2 (the runner's MXSERVICE, MXLOAD):
     services.txt: host:mxlink-app.elf
     mxload.txt:   host:mxlink-status.elf     (status 0 expected) */
#include <mx/debug.h>
#include <mx/loader.h>
#include <mx/msg.h>
#include <mx/proc.h>
#include <mx/string.h>

/* a core run of its own: without one it takes all memory, and the service
   beside it with it */
MX_CORE_HEAP(128 << 10);

int main(void) {
  int bad = 0;
  int port = mx_port_find("mxlink");
  char rep[MX_MSG_MAX];
  int n = port > 0 ? mx_msg_send(port, "status", 7, rep, sizeof rep) : -1;
  if (n <= 0) {
    mx_printf("status: no answer from mxlink (port %d): %d\n", port, n);
    return 1;
  }
  rep[sizeof rep - 1] = 0;
  mx_printf("status: %s\n", rep);
  bad += memcmp(rep, "mxlink: front ", 14) != 0;
  /* this one is in front, mxlink beside it */
  struct mx_proc_info p[4];
  int k = mx_proc_list(p, 4), beside = 0;
  for (int i = 0; i < k && i < 4; i++)
    beside += p[i].state == MX_PROC_BESIDE && !memcmp(p[i].name + strlen(p[i].name) - 14,
                                                      "mxlink-app.elf", 14);
  bad += beside != 1;
  n = mx_msg_send(port, "nope", 5, rep, sizeof rep);
  bad += n <= 0 || memcmp(rep, "mxlink: unknown", 15) != 0;
  mx_printf("status: %s\n", bad ? "FAILED" : "passed");
  return bad;
}
