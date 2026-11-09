# mxlink: the host link, ps2client's protocol (ps2link's). Its IOP module,
# ps2link.irx (iop/), which the kernel (apps/mxos) loads at the boot; its EE
# side, the app in ee/ (mx OS plan, step 2e-4), a service the kernel starts;
# the kernel (apps/mxos/ee/cmd.c) still serves the commands the app does not.
#
#   make    build/apps/mxlink/ps2link.irx and mxlink-app.elf

all:

PS2MAX ?= $(abspath $(CURDIR)/../..)
OUT    ?= $(PS2MAX)/build/apps/mxlink

# maxsdk's IOP toolchain (libs/maxsdk/iop/iop.mk): clang for mipsel-none-irx,
# which links with ld.lld --irx into the IRX, the import tables from the ILB
# files of maxsdk's and ps2sdk's IOP libraries
include $(PS2MAX)/libs/maxsdk/iop/iop.mk
IOP_FLAGS = $(IOP_CFLAGS) -Os -Werror $(IOP_INCS) -Iiop
IOP_SRCS  = net_fsys.c net_fio.c ps2link.c cmdHandler.c nprintf.c excepHandler.c
IOP_OBJS  = $(IOP_SRCS:%.c=$(OUT)/obj/%.o)

all: $(OUT)/ps2link.irx app

app:
	$(MAKE) -C ee OUT=$(OUT)
.PHONY: app

$(OUT)/obj/%.o: iop/%.c $(wildcard iop/*.h) $(PS2MAX)/libs/maxsdk/iop/iop.mk
	@mkdir -p $(dir $@)
	$(IOP_CC) $(IOP_FLAGS) -c $< -o $@

$(OUT)/ps2link.irx: $(IOP_OBJS) $(MAXSDK_ILBS) $(IOP_ILB_RSP)
	$(IOP_CC) -s -o $@ $(filter %.o,$^) $(call iop_link_libs)

clean:
	rm -rf $(OUT)

.PHONY: all clean
