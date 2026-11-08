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
