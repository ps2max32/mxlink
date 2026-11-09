# mxlink

The host link of mx OS on the PS2: ps2link's protocol, so `ps2client` and the
scripts around it work as with ps2link: `execee`, `reset`, `host:` files, the
console, and `status`.

Now this repo holds its IOP module, `ps2link.irx` (`iop/`, see
`iop/README.md`), which the kernel ([mxos](https://github.com/ps2max32/mxos))
loads at the boot from `mc0:/MXOS`, and its EE side as an app, a service beside
the games (`ee/`, step 2e of the mx OS plan, maxsdk's `docs/mx-os-apps-plan.md`).
The app serves ps2client's commands; the kernel keeps the reset and the IOP's
exception (`ee/cmd.c` there), and serves everything while the app is not running.
```
make      # build/apps/mxlink/ps2link.irx
```

With maxsdk's IOP toolchain (`libs/maxsdk/iop/iop.mk`): the feature clang
for `mipsel-none-irx`, linked by `ld.lld --irx`, and ps2sdk's IOP headers
(`PS2MAX_IOP_SDK`).

## The app (ee/, mx OS step 2e-4)

`ee/main.c` is mxlink's EE side as an app: a service beside every program in front
(`mx_proc_service`), on the kernel's general interface only (mx-kernel 20). It is a text
service: what it says goes to the kernel's log, for the shell to show as its console, and
it has no screen of its own. Built as `build/apps/mxlink/mxlink.elf`; on the card it is
`mc0:/MXOS/MXLINK.ELF`, which `mc0:/MXOS/SERVICES.CFG` names, so the kernel starts it at
the boot and the shell after it (`make -C apps/mxos shell` puts both into the card's files).

- ps2client's commands, which the kernel passes on to the port of the host link's IOP
  channel (`MX_IOP_CHANNEL_HOST`): `execee`, `dumpreg` (ps2client's ping), and from mx-kernel
  21 `dumpmem` and `writemem` (physical memory, `mx_phys_read`, `mx_phys_write`), `gsexec`,
  `startvu` and `stopvu`. For them the shell in front ends (the text screen after it until a
  reset); another program in front refuses them. ps2link.irx still sends them to the kernel by
  SIF DMA;
- the kernel's events: the end of the host's program, with the runner's end marker;
- its port `mxlink`: requests from other processes (`status`);
- the log, read as the host's console will be.

A program that takes all memory (no core heap of its own) ends the app, and one on the BIOS's
threads restarts the kernel: the kernel serves the host until the app is back after it.
Started by the host (`execee host:mxlink.elf marker`), the app prints the end marker once it
is the service. With an older kernel (18, 19) it becomes a service that does nothing, so that
the shell comes. `ee/tests/status.c` (`mxlink-status.elf`) runs in front of it. On PCSX2 (no
host link there; `test-stop [program]` ends the shell in front after 3 s, as for a command,
and starts the program then):

    MXSERVICE="host:mxlink.elf" MXLOAD="host:mxlink-status.elf" run.sh <the emu kernel>
    MXSERVICE="host:mxlink.elf test-stop" MXLOAD="host:MXSHELL.ELF console" run.sh <the emu kernel>

