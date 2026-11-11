# mxlink's EE side, the app (mx OS step 2e-4): a service on the kernel's
# interface, an MXOS program in core (libmxk, mxos-core).
#
#   make    build/apps/mxlink/mxlink.elf (on the card: MXOS/MXLINK.ELF)

all:

PS2MAX ?= $(abspath $(CURDIR)/../../..)
OUT    ?= $(PS2MAX)/build/apps/mxlink

# its own libmx, for size: its core stays in memory beside every program
MAXSDK_OUT = $(OUT)/maxsdk
export EXTRA_CFLAGS = -Os -ffunction-sections -fdata-sections
# an MXOS program all in core (mxos-core): it stays while another program runs,
# and the kernel has one VM, the program in front's
MX_TARGET = mxos-core
include $(PS2MAX)/libs/maxsdk/mk/maxsdk.mk

SRCS = main.c
OBJS = $(SRCS:%.c=$(OUT)/obj/ee/%.o)

all: $(OUT)/mxlink.elf $(OUT)/mxlink-status.elf

$(OUT)/obj/ee/%.o: %.c $(MX_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# libmxk: the kernel's services
LIB = $(MAXSDK_OUT)/libmxk.a
$(LIB): $(MX_LIB)
$(OUT)/mxlink.elf: $(OBJS) $(LIB) $(MX_CRT0) $(MX_LD)
	$(call link,$@,$(MX_CRT0) $(OBJS) $(LIB))

# a program to run in front of it (tests/status.c)
$(OUT)/obj/ee/tests/%.o: tests/%.c $(MX_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/mxlink-status.elf: $(OUT)/obj/ee/tests/status.o $(LIB) $(MX_CRT0) $(MX_LD)
	$(call link,$@,$(MX_CRT0) $< $(LIB))
