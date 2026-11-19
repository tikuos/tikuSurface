# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Compose the diagnostic local runtime through the S31 application slot.
.DEFAULT_GOAL := all
SURFACE_ROOT := $(patsubst %/product/,%,$(dir $(lastword $(MAKEFILE_LIST))))
ifeq ($(filter s31-local s31-connected,$(S31_PROFILE)),)
$(error TikuSurface requires S31_PROFILE=s31-local or s31-connected)
endif
ifeq ($(TIKU_APPL_GUI),1)
$(error TikuSurface and the remote GUI cannot be selected together)
endif
S31_UI_LTO ?= 0
S31_SURFACE_LCD ?= 0
ifeq ($(filter $(S31_SURFACE_LCD),0 1),)
$(error S31_SURFACE_LCD must be 0 or 1)
endif
ifeq ($(filter $(S31_UI_LTO),0 1),)
$(error S31_UI_LTO must be 0 or 1)
endif
SURFACE_SDK ?= $(SURFACE_ROOT)/foundation/build/surface-s31$(if $(filter 1,$(S31_UI_LTO)),-lto)/sdk
ifeq ($(wildcard $(SURFACE_SDK)/sdk.mk),)
$(error Build the pinned TikuSurface S31 SDK before selecting this product)
endif
SURFACE_PIN := $(shell python3 $(SURFACE_ROOT)/tools/check_foundation.py >/dev/null && echo ok)
ifneq ($(SURFACE_PIN),ok)
$(error TikuSurface foundation pin validation failed)
endif
SDK_ROOT := $(abspath $(SURFACE_SDK))
FOUNDATION_TARGET := s31
CC := $(S31_CC)
SDK_REQUIRED_CAPABILITIES := local-rendering font-subset allocator-contexts bounded-input monotonic-time static-applications paged-storage-read word-calculator
include $(SDK_ROOT)/sdk.mk
SURFACE_UI_SRCS := $(addprefix $(SURFACE_ROOT)/,shell/settings.c shell/touch.c shell/surface.c shell/paint.c apps/apps.c apps/calculator.c platform/tikuos/runtime.c platform/tikuos/presentation.c)
SURFACE_UI_OBJS := $(addprefix $(S31_DIR)/,$(SURFACE_UI_SRCS:.c=.o))
SURFACE_ARCHIVES := $(addprefix $(SDK_ROOT)/lib/,libtiku_render.a libtiku_models.a libtiku_font_subset.a)
SURFACE_COMPILE := $(SDK_CFLAGS) -I$(SURFACE_ROOT)/shell -I$(SURFACE_ROOT)/platform/tikuos -fstack-usage $(if $(filter 1,$(S31_UI_LTO)),-flto,)
APPL_SRCS := $(addprefix $(SURFACE_ROOT)/platform/tikuos/,bindings.c diagnostics.c)
APPL_CFLAGS := -DTIKU_APPLICATION_HEADER='"$(SURFACE_ROOT)/platform/tikuos/boot.h"'
APPL_CFLAGS += -DSURFACE_LCD_ENABLE=$(S31_SURFACE_LCD)
ifeq ($(S31_PROFILE),s31-connected)
APPL_CFLAGS += -DTIKU_S31_RADIO_IMAGE_BOUNDS_ENABLE=1
endif
ifeq ($(S31_SURFACE_LCD),1)
SURFACE_UI_SRCS += $(SURFACE_ROOT)/platform/tikuos/media.c
SURFACE_UI_OBJS += $(S31_DIR)/$(SURFACE_ROOT)/platform/tikuos/media.o
ifeq ($(S31_PROFILE),s31-connected)
APPL_CFLAGS += -DSURFACE_CONNECTED_ENABLE=1 -DTIKU_MEDIA_TRANSFER_STATUS_ENABLE=1 -DTIKU_WIRELESS_OBSERVE_ENABLE=1
APPL_SRCS += $(SURFACE_ROOT)/platform/tikuos/services.c
endif
APPL_CFLAGS += -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -DTIKU_I2C_BUDGET_ENABLE=1 -DTIKU_S31_SD_ASYNC_ENABLE=1 -DTIKU_TFS_ASYNC_ENABLE=1 -DTIKU_S31_STORAGE_ASYNC_ENABLE=1
S31_LDFLAGS += -Wl,-u,tiku_s31_sd_open_begin,-u,tiku_s31_sd_read_begin,-u,tiku_s31_sd_clock_begin,-u,tiku_s31_sd_poll,-u,tiku_s31_sd_cancel,-u,tiku_s31_mfs_async_acquire,-u,tiku_s31_mfs_async_read,-u,tiku_s31_mfs_async_poll,-u,tiku_s31_mfs_async_close
APPL_SRCS += $(SURFACE_ROOT)/platform/tikuos/display.c $(SURFACE_ROOT)/platform/tikuos/input.c $(SURFACE_ROOT)/platform/tikuos/media_io.c $(SURFACE_ROOT)/platform/tikuos/settings_io.c
APPL_SRCS += services/storage/tiku_media_async.c arch/esp32s31/tiku_storage_async_io.c
APPL_SRCS += interfaces/input/tiku_touch_gt1151.c drivers/input/gt1151/tiku_drv_input_gt1151.c
APPL_SRCS += interfaces/display/tiku_display.c interfaces/display/tiku_display_async.c \
    arch/esp32s31/tiku_display_arch.c arch/esp32s31/tiku_dma_arch.c
endif
APPL_EXTRA_OBJS := $(S31_DIR)/tikuSurface/surface-ui.o
APPL_LINKER := $(S31_DIR)/surface.ld
S31_LDFLAGS += -Wl,-u,tiku_application_stop,-u,surface_runtime_launch,-u,surface_runtime_fail_at
APPL_LINK_DEPS := $(SURFACE_ROOT)/product/tikuos.mk $(SURFACE_ROOT)/tools/check_placement.py
APPL_CHECK_ELF = python3 $(SURFACE_ROOT)/tools/check_placement.py --elf $@ --identity $(SDK_IDENTITY) --map $(S31_DIR)/kernel.map --unit $(APPL_EXTRA_OBJS) --binding $(addprefix $(S31_DIR)/,$(APPL_SRCS:.c=.o)) $(if $(filter 1,$(S31_SURFACE_LCD)),$(S31_DIR)/interfaces/bus/tiku_i2c_bus.o $(S31_DIR)/arch/esp32s31/tiku_spi_arch.o $(S31_DIR)/arch/esp32s31/tiku_sdcard_arch.o $(S31_DIR)/arch/esp32s31/tiku_mfs_block_arch.o,) --tier-unit $(S31_DIR)/kernel/memory/tiku_tier.o $(if $(filter 1,$(S31_SURFACE_LCD)),--display-descriptors --nvm-unit $(S31_DIR)/arch/esp32s31/tiku_storage_arch.o $(S31_DIR)/arch/esp32s31/tiku_libc_heap.o --media-unit $(S31_DIR)/kernel/fs/tiku_mfs.o $(S31_DIR)/services/storage/tiku_media_card.o --i2c-unit $(S31_DIR)/arch/esp32s31/tiku_i2c_arch.o,) --prefix $(CROSS_COMPILE) --output $(S31_DIR)/placement.json

$(S31_DIR)/.surface-flags: FORCE
	@mkdir -p $(@D)
	@printf '%s\n' '$(SURFACE_COMPILE) $(SDK_IDENTITY) $(SURFACE_ARCHIVES)' > $@.new
	@cmp -s $@.new $@ || cp $@.new $@
	@rm -f $@.new
$(SURFACE_UI_OBJS): private S31_FLAGS += $(SURFACE_COMPILE)
$(SURFACE_UI_OBJS): $(S31_DIR)/.surface-flags
$(APPL_EXTRA_OBJS): $(SURFACE_UI_OBJS) $(SURFACE_ARCHIVES) $(S31_DIR)/.surface-flags $(SURFACE_ROOT)/product/tikuos.mk
	$(S31_CC) $(S31_ARCH) -nostdlib -r -fstack-usage $(if $(filter 1,$(S31_UI_LTO)),-flto -flinker-output=nolto-rel,) -Wl,--build-id=none $(SURFACE_UI_OBJS) -Wl,--whole-archive $(SURFACE_ARCHIVES) -Wl,--no-whole-archive -o $@
$(APPL_LINKER): arch/esp32s31/radio_flash.ld $(SURFACE_ROOT)/tools/surface_linker.py $(SURFACE_ROOT)/tools/check_placement.py
	python3 $(SURFACE_ROOT)/tools/surface_linker.py --base $< --output $@
.DELETE_ON_ERROR: $(S31_DIR)/kernel.elf
-include $(SURFACE_UI_OBJS:.o=.d)
