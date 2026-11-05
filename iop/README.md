# mxlink's IOP module (ps2link.irx)

ps2link's IOP side: the network file server (`host:`), the command port that
passes ps2client's commands to the EE, the tty and the IOP exception report.
Copied from our ps2link fork (rickgaiser/ps2link, branch `fio-write-stream`,
commit ff4c2f9, `iop/` and three headers of `include/`), the build of the
ps2link that the PS2 ran. mxlink owns this copy and changes it; the fork is
not updated from here. Changed so far: the status record and
`PKO_STATUS_CMD` (`cmdHandler.c`, `hostlink.h`, `nprintf.c`).

Licence: the PS2Link licence (Academic Free License 2.0), `LICENSE`.

Built by `../Makefile` with ps2dev's IOP gcc and ps2sdk's IOP headers, IOP
link script and `iopfixup` (`PS2MAX_IOP_BIN`, `PS2MAX_IOP_SDK` in
`ps2max/envsetup.sh`).

`chime.c` is mxlink's own module, not ps2link's: it resets the SPU2 and plays
the boot sound, then unloads. The sound is `chime_blip.h`, made by
`chime_blip.py`.
