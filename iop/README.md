# mxlink's IOP module

## ps2link.irx

ps2link's IOP side: the network file server (`host:`), the command port that
passes ps2client's commands to the EE, the tty and the IOP exception report.
Copied from our ps2link fork (rickgaiser/ps2link, branch `fio-write-stream`,
commit ff4c2f9, `iop/` and three headers of `include/`). mxlink owns this
copy and changes it; the fork is not updated from here. Changed so far: the status record and
`PKO_STATUS_CMD` with the PS2's model and ROM version (`cmdHandler.c`,
`hostlink.h`, `nprintf.c`, `ps2link.c`).

Licence: the PS2Link licence (Academic Free License 2.0), `LICENSE`.

## Building

It is built by `../Makefile` with maxsdk's IOP toolchain
(`libs/maxsdk/iop/iop.mk`): the feature clang for the IOP target
(`--target=mipsel-none-irx`), which links with `ld.lld --irx` into the IRX and
makes the import tables for what the module calls, from the ILB files of
maxsdk's modules and of ps2sdk's IOP headers (`PS2MAX_IOP_SDK` in
`ps2max/envsetup.sh`). `irx_imports.h` (from ps2link) only includes headers.
