# Build the RAM desktop from the root of a TikuOS checkout.
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
MCU := esp32p4
BOARD := esp32_p4x_function_ev_board
MINIMAL := 1
P4X_CPU_MHZ ?= 400
P4X_MIC := 1
SURFACE_PPA ?= 1
BUILD_DIR := build/esp32p4/p4x/surface-ram
ifdef P4X_FLASH_BOOT
BUILD_DIR := build/esp32p4/p4x/surface-flash
endif
surface := tikuSurface
fixture := $(surface)/tests/p4x
DESKTOP_FOUNDATION ?= $(abspath $(surface)/foundation)
desktop_build := $(abspath $(BUILD_DIR)/desktop)
desktop_archive := $(desktop_build)/libsurface_desktop.a
include Makefile

P4_FLAGS += -DSURFACE_PPA=$(SURFACE_PPA) -D_DEFAULT_SOURCE -DTIKU_MEM_NVM_ENABLE=0 -DTIKU_POSIX_THREADS=1 \
    -DTIKU_POSIX_MUTEXES=32 -DTIKU_POSIX_CONDS=16 -DTIKU_POSIX_STORE=1 \
    -DTIKU_POSIX_STAGE_MAX=16384u -DTIKU_TFS_ASYNC_ENABLE=1 \
    -DTIKU_PROCESS_OBSERVERS_ENABLE=1 -DTIKU_BASIC_JOB_OWNER_ENABLE=1 \
    -DSURFACE_NATIVE_DESKTOP=1 -DSURFACE_LCD_ENABLE=1 -Wno-unused-parameter \
    -DTIKU_VFS_TRNG_HEADER='"arch/esp32p4/tiku_trng_arch.h"' \
    -Icompat/posix -I$(surface)/platform/tikuos -I$(surface)/desktop \
    -I$(surface)/system/product -I$(surface)/shell \
    $(addprefix -I,$(wildcard $(DESKTOP_FOUNDATION)/kits/*/portable)) \
    $(addprefix -I,$(wildcard $(DESKTOP_FOUNDATION)/kits/*/)) \
    -I$(DESKTOP_FOUNDATION)/packages/shell/system/shared/shell -Iapplications/board/gui

ifdef P4X_FLASH_BOOT
$(BUILD_DIR)/$(fixture)/ram_session.o: P4_FLAGS += -DP4X_FLASH_BOOT=1
endif

surface_src := $(fixture)/ram_session.c $(fixture)/recorder_check.c $(fixture)/unavailable.c \
    kernel/memory/tiku_tier.c kernel/memory/tiku_region.c kernel/memory/tiku_mem.c kernel/memory/tiku_pool.c \
    kernel/vfs/tree/tiku_vfs_tree_watch.c kernel/vfs/tree/tiku_vfs_tree_random.c \
    kernel/vfs/tiku_vfs_cache.c kernel/fs/tiku_tfs.c \
    arch/esp32p4/tiku_mem_arch.c arch/esp32p4/tiku_trng_arch.c \
    arch/esp32p4/tiku_ppa_arch.c arch/esp32p4/tiku_dma2d_arch.c \
    services/link/tiku_link_local.c tikukits/crypto/sha256/tiku_kits_crypto_sha256.c \
    $(addprefix $(surface)/platform/tikuos/,bindings.c desktop_bindings.c desktop_hash.c input.c display.c) \
    $(addprefix compat/posix/tiku_posix_,thread.c pthread.c lock.c store.c media.c heap.c sbrk.c time.c)
surface_objs := $(patsubst %.c,$(BUILD_DIR)/%.o,$(surface_src))
-include $(surface_objs:.o=.d)

.PHONY: surface-p4x-ram desktop-ram-archive
desktop-ram-archive:
	$(MAKE) -f $(surface)/desktop/desktop.mk DESKTOP_BOARD=$(BOARD) \
	    DESKTOP_BUILD=$(desktop_build) CROSS_COMPILE=$(P4_TOOL) desktop-archive
$(desktop_archive): desktop-ram-archive
	@test -s "$@"

$(BUILD_DIR)/native.elf: $(P4_OBJS) $(surface_objs) $(desktop_archive) $(fixture)/ram.ld $(fixture)/ram.mk
	$(P4_CC) $(P4_ARCH) -nostdlib -Wl,--gc-sections,-Map=$(@:.elf=.map) \
	    -T$(fixture)/ram.ld -Wl,--wrap=tiku_p4x_panel_pattern,--wrap=tiku_trk_splash_done \
	    -Wl,--wrap=surface_recorder_tick,--wrap=tiku_trk_deskbar_draw,--wrap=surface_desktop_main,-u,tiku_trk_builtins,-u,_open \
	    $(P4_OBJS) $(surface_objs) -Wl,--start-group $(desktop_archive) -lc -lm -lnosys -lgcc -Wl,--end-group -o $@
	$(P4_SIZE) $@

surface-p4x-ram: $(BUILD_DIR)/native.elf
	$(P4_OBJCOPY) --dump-section .surface_assets=$(BUILD_DIR)/assets.bin \
	    --remove-section=.surface_assets --remove-section=.surface_zero $< $(BUILD_DIR)/sram.elf
	$(PYTHON) -c 'import sys,pathlib,hashlib; p=pathlib.Path(sys.argv[1]); (p/"digest.bin").write_bytes(hashlib.sha256((p/"assets.bin").read_bytes()).digest())' $(BUILD_DIR)
	$(P4_OBJCOPY) --update-section .p4x_hash=$(BUILD_DIR)/digest.bin $(BUILD_DIR)/sram.elf $(BUILD_DIR)/worker.elf
	$(PYTHON) -m esptool --chip esp32p4 elf2image --flash-mode dio --flash-freq 40m --flash-size 16MB \
	    --min-rev-full 302 --max-rev-full 302 -o $(BUILD_DIR)/worker.bin $(BUILD_DIR)/worker.elf
.DEFAULT_GOAL := surface-p4x-ram
