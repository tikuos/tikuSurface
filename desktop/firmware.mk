# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# The native desktop in the S31 application slot: TikuDesktop's shell from
# desktop/desktop.mk, partially linked into surface-ui.o, with the panel,
# touch and settings bindings of the LCD runtime.  Included by
# product/tikuos.mk when SURFACE_NATIVE_DESKTOP=1.
ifneq ($(S31_SURFACE_LCD),1)
$(error SURFACE_NATIVE_DESKTOP requires S31_SURFACE_LCD=1)
endif
ifneq ($(S31_CPU_MHZ),160)
$(error SURFACE_NATIVE_DESKTOP requires S31_CPU_MHZ=160)
endif
comma := ,
DESKTOP_BUILD := $(S31_DIR)/desktop
DESKTOP_CC := $(S31_CC)
DESKTOP_AR := $(patsubst %gcc,%ar,$(S31_CC))
include $(SURFACE_ROOT)/desktop/desktop.mk

APPL_SRCS := $(addprefix $(SURFACE_ROOT)/platform/tikuos/,bindings.c diagnostics.c \
    desktop_bindings.c display.c input.c settings_io.c)
APPL_SRCS += arch/esp32s31/tiku_storage_async_io.c
APPL_SRCS += interfaces/input/tiku_touch_gt1151.c drivers/input/gt1151/tiku_drv_input_gt1151.c
APPL_SRCS += interfaces/display/tiku_display.c interfaces/display/tiku_display_async.c \
    arch/esp32s31/tiku_display_arch.c arch/esp32s31/tiku_dma_arch.c
APPL_CFLAGS := -DTIKU_APPLICATION_HEADER='"$(SURFACE_ROOT)/platform/tikuos/boot.h"'
APPL_CFLAGS += -DSURFACE_NATIVE_DESKTOP=1 -DSURFACE_LCD_ENABLE=1 -DSURFACE_DESKTOP=1
APPL_CFLAGS += -DTIKU_APPLICATION_PROJECT='"tikusurface_s31_lcd"'
APPL_CFLAGS += -DTIKU_I2C_BUDGET_ENABLE=1 -DTIKU_TFS_ASYNC_ENABLE=1 -DTIKU_S31_STORAGE_ASYNC_ENABLE=1
# A finished card read leaves the card initialized for SURFACE_CARD_PARK_MS,
# so a folder's listing and reads do not each power-cycle and identify it
# again; 0 closes the card after every request.
SURFACE_CARD_PARK_MS ?= 2000
APPL_CFLAGS += -DTIKU_S31_SD_PARK_MS=$(SURFACE_CARD_PARK_MS)
# The SD card's native media volume, reached through the queued media
# bridge; flat names, as the card was written.
APPL_SRCS += $(SURFACE_ROOT)/platform/tikuos/media_io.c services/storage/tiku_media_async.c \
    services/storage/tiku_media_mutation.c services/storage/tiku_media_checkpoint.c
APPL_CFLAGS += -DTIKU_MEDIA_DIRECTORIES_ENABLE=0 -DTIKU_MEDIA_NAMES_ENABLE=1 \
    -DTIKU_MEDIA_ASYNC_ENABLE=1 -DTIKU_S31_SD_ASYNC_ENABLE=1
S31_LDFLAGS += -Wl,-u,tiku_media_card_mutation_size,-u,tiku_media_card_mutation_init,-u,tiku_media_card_mutation_step,-u,tiku_media_card_mutation_idle,-u,tiku_media_card_mutation_cancel,-u,tiku_media_checkpoint_step,-u,tiku_media_checkpoint_cancel
S31_LDFLAGS += -Wl,-u,tiku_s31_sd_open_begin,-u,tiku_s31_sd_read_begin,-u,tiku_s31_sd_clock_begin,-u,tiku_s31_sd_poll,-u,tiku_s31_sd_cancel,-u,tiku_s31_mfs_async_acquire,-u,tiku_s31_mfs_async_read,-u,tiku_s31_mfs_async_poll,-u,tiku_s31_mfs_async_close
APPL_EXTRA_OBJS := $(S31_DIR)/tikuSurface/surface-ui.o
APPL_LINKER := $(S31_DIR)/surface.ld
S31_LDFLAGS += -Wl,-u,tiku_application_stop,-u,surface_runtime_launch,-u,surface_runtime_fail_at
APPL_LINK_DEPS := $(SURFACE_ROOT)/product/tikuos.mk $(SURFACE_ROOT)/desktop/firmware.mk \
    $(SURFACE_ROOT)/tools/surface_linker.py
APPL_CHECK_ELF = python3 $(SURFACE_ROOT)/tools/desktop_placement.py --elf $@ --map $(S31_DIR)/kernel.map

# The runtime's entry points and the C-library system calls the desktop
# supplies; the archive contributes only the objects these reach.
DESKTOP_ROOTS := surface_runtime_start surface_runtime_stop surface_runtime_poll \
    surface_runtime_continue surface_runtime_launch surface_runtime_calibrate \
    surface_runtime_calibration surface_runtime_preferences surface_runtime_set_preferences \
    surface_runtime_stats surface_runtime_frame_stats surface_runtime_input_stats \
    surface_runtime_fail_at surface_touch_map_valid _open _sbrk_r
# SURFACE_HEAP_PROFILE=1 records the desktop's heap blocks by call site; the
# control file's "heap" command prints them.
SURFACE_HEAP_PROFILE ?= 0
ifeq ($(SURFACE_HEAP_PROFILE),1)
DESKTOP_ROOTS += __wrap_malloc __wrap_calloc __wrap_realloc __wrap_free surface_desktop_heap_report
S31_LDFLAGS += -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free
endif
S31_LIBS += -lm
$(APPL_EXTRA_OBJS): $(DESKTOP_ARCHIVE) $(SURFACE_ROOT)/desktop/firmware.mk
	@mkdir -p $(@D)
	$(S31_CC) $(S31_ARCH) -nostdlib -r -Wl,--build-id=none $(addprefix -Wl$(comma)-u$(comma),$(DESKTOP_ROOTS)) \
	    -Wl,--start-group $(DESKTOP_ARCHIVE) -Wl,--end-group -o $@
$(APPL_LINKER): arch/esp32s31/radio_flash.ld $(SURFACE_ROOT)/tools/surface_linker.py
	python3 $(SURFACE_ROOT)/tools/surface_linker.py --base $< --output $@ --native-desktop
.DELETE_ON_ERROR: $(S31_DIR)/kernel.elf
