# mxlink's IOP modules

## ps2link.irx

ps2link's IOP side: the network file server (`host:`), the command port that
passes ps2client's commands to the EE, the tty and the IOP exception report.
Copied from our ps2link fork (rickgaiser/ps2link, branch `fio-write-stream`,
commit ff4c2f9, `iop/` and three headers of `include/`). mxlink owns this
copy and changes it; the fork is not updated from here. Changed so far: the status record and
`PKO_STATUS_CMD` with the PS2's model and ROM version (`cmdHandler.c`,
`hostlink.h`, `nprintf.c`, `ps2link.c`).

Licence: the PS2Link licence (Academic Free License 2.0), `LICENSE`.

## chime.irx

mxlink's own module, not ps2link's (`chime.c`, `chime_imports.lst`): it resets
the SPU2 and plays the boot sound, then unloads. The sound is `chime_blip.h`,
made by `chime_blip.py`. Its start result reports what it saw, and mxlink logs
that.

## Building

Both are built by `../Makefile` with the feature clang (`-mcpu=r3000a`) and
ld.lld with maxsdk's IOP link script (`libs/maxsdk/iop/irx.ld`, the tables
through `libs/maxsdk/iop/irx_clang.h`), then ps2sdk's `iopfixup`
(`PS2MAX_IOP_SDK` in `ps2max/envsetup.sh`).
