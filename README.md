# mxlink

The host link of mx OS on the PS2: ps2link's protocol, so `ps2client` and the
scripts around it work as with ps2link: `execee`, `reset`, `host:` files, the
console, and `status`.

Now this repo holds its IOP module, `ps2link.irx` (`iop/`, see
`iop/README.md`), which the kernel ([mxos](https://github.com/ps2max32/mxos))
loads at the boot from `mc0:/MXOS`; the EE side of the protocol is still in the
kernel (`ee/cmd.c` there). It becomes a program of its own, a service beside
the games, in step 2e of the mx OS plan (maxsdk's `docs/mx-os-apps-plan.md`):
then it answers the host while a game runs.

## Build

```
make      # build/apps/mxlink/ps2link.irx
```

With maxsdk's IOP toolchain (`libs/maxsdk/iop/iop.mk`): the feature clang
for `mipsel-none-irx`, linked by `ld.lld --irx`, and ps2sdk's IOP headers
(`PS2MAX_IOP_SDK`).

## The app (ee/, mx OS step 2e-4)

`ee/main.c` is mxlink's EE side as an app: a service beside every program in front
(`mx_proc_service`, mx-kernel 18), on the kernel's general interface only. It is a text
service: what it says goes to the kernel's log, for the shell to show as its console, and
it has no screen of its own. So far a skeleton, built as `build/apps/mxlink/mxlink-app.elf`:

- the kernel's events: a program's end, with its exit status and the runner's end marker;
- its port `mxlink`: requests from other processes (`status`);
- its IOP channel (SIF command 18), where ps2link.irx's commands will come;
- the log, read as the host's console will be.

The EE side that serves ps2client is still the kernel's (`apps/mxos/ee/cmd.c`) until it moves
here. `ee/tests/status.c` (`mxlink-status.elf`) runs in front of it. On PCSX2:

    MXSERVICE="host:mxlink-app.elf" MXLOAD="host:mxlink-status.elf" run.sh <the emu kernel>

