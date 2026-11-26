# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Compose the diagnostic local runtime through the S31 application slot.
.DEFAULT_GOAL := all
SURFACE_ROOT := $(patsubst %/product/,%,$(dir $(lastword $(MAKEFILE_LIST))))
ifeq ($(filter s31-local s31-connected,$(S31_PROFILE)),)
$(error TikuSurface requires S31_PROFILE=s31-local or s31-connected)
endif
SURFACE_NATIVE_DESKTOP ?= 0
ifeq ($(TIKU_APPL_GUI),1)
ifneq ($(SURFACE_NATIVE_DESKTOP),1)
$(error The board GUI requires SURFACE_NATIVE_DESKTOP=1 with TikuSurface)
endif
endif
ifeq ($(SURFACE_NATIVE_DESKTOP),1)
include $(SURFACE_ROOT)/desktop/firmware.mk
else
S31_UI_LTO ?= 0
S31_SURFACE_LCD ?= 0
SURFACE_TRANSFERS ?= 0
ifeq ($(filter $(SURFACE_TRANSFERS),0 1),)
$(error SURFACE_TRANSFERS must be 0 or 1)
endif
SURFACE_TRANSFER_SERVICE := $(if $(filter s31-connected,$(S31_PROFILE)),$(if $(filter 11,$(SURFACE_TRANSFERS)$(S31_SURFACE_LCD)),1,0),0)
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
SURFACE_DESKTOP ?= 0
ifeq ($(filter $(SURFACE_DESKTOP),0 1),)
$(error SURFACE_DESKTOP must be 0 or 1)
endif
ifeq ($(SURFACE_DESKTOP),1)
ifeq ($(S31_SURFACE_LCD),1)
ifeq ($(filter $(S31_CPU_MHZ),160 320),)
$(error The core LCD desktop requires S31_CPU_MHZ=160 or 320)
endif
endif
ifneq ($(SURFACE_DIRECTORIES)$(SURFACE_FILE_INFO),11)
$(error SURFACE_DESKTOP requires SURFACE_DIRECTORIES=1 and SURFACE_FILE_INFO=1)
endif
SDK_REQUIRED_CAPABILITIES += window-workspace deskbar-paint
endif
SURFACE_DIRECTORIES ?= 0
ifeq ($(filter $(SURFACE_DIRECTORIES),0 1),)
$(error SURFACE_DIRECTORIES must be 0 or 1)
endif
ifeq ($(SURFACE_DIRECTORIES),1)
ifneq ($(SURFACE_CONTENT_JOBS),1)
$(error SURFACE_DIRECTORIES requires SURFACE_CONTENT_JOBS=1)
endif
SDK_REQUIRED_CAPABILITIES += paged-storage-directories paged-storage-subtrees paged-storage-volume-features paged-storage-tree-copy
endif
SURFACE_PREFERENCES ?= 0
ifeq ($(filter $(SURFACE_PREFERENCES),0 1),)
$(error SURFACE_PREFERENCES must be 0 or 1)
endif
ifeq ($(SURFACE_PREFERENCES),1)
ifneq ($(SURFACE_DOCUMENTS),1)
$(error SURFACE_PREFERENCES requires SURFACE_DOCUMENTS=1)
endif
endif
SURFACE_DOCUMENTS ?= 0
ifeq ($(filter $(SURFACE_DOCUMENTS),0 1),)
$(error SURFACE_DOCUMENTS must be 0 or 1)
endif
ifeq ($(SURFACE_DOCUMENTS),1)
ifneq ($(SURFACE_HEX),1)
$(error SURFACE_DOCUMENTS requires SURFACE_HEX=1)
endif
SDK_REQUIRED_CAPABILITIES += document-handler-relation
endif
SURFACE_EDITOR_LINES ?= 0
ifeq ($(filter $(SURFACE_EDITOR_LINES),0 1),)
$(error SURFACE_EDITOR_LINES must be 0 or 1)
endif
ifeq ($(SURFACE_EDITOR_LINES),1)
ifneq ($(SURFACE_EDITOR),1)
$(error SURFACE_EDITOR_LINES requires SURFACE_EDITOR=1)
endif
SDK_REQUIRED_CAPABILITIES += editor-line-transactions editor-line-indentation
endif
SURFACE_EDITOR_DOCUMENTS ?= 0
ifeq ($(filter $(SURFACE_EDITOR_DOCUMENTS),0 1),)
$(error SURFACE_EDITOR_DOCUMENTS must be 0 or 1)
endif
ifeq ($(SURFACE_EDITOR_DOCUMENTS),1)
ifneq ($(SURFACE_NOTES),1)
$(error SURFACE_EDITOR_DOCUMENTS requires SURFACE_NOTES=1)
endif
endif
SURFACE_IMAGE ?= 0
ifeq ($(filter $(SURFACE_IMAGE),0 1),)
$(error SURFACE_IMAGE must be 0 or 1)
endif
ifeq ($(SURFACE_IMAGE),1)
ifneq ($(SURFACE_NOTES)$(SURFACE_DOCUMENTS),11)
$(error SURFACE_IMAGE requires SURFACE_NOTES=1 and SURFACE_DOCUMENTS=1)
endif
SDK_REQUIRED_CAPABILITIES += bitmap-progress png-progress image-view-model image-pointer-model
endif
SURFACE_NOTES ?= 0
ifeq ($(filter $(SURFACE_NOTES),0 1),)
$(error SURFACE_NOTES must be 0 or 1)
endif
ifeq ($(SURFACE_NOTES),1)
ifneq ($(SURFACE_DIFF)$(SURFACE_DIRECTORIES),11)
$(error SURFACE_NOTES requires SURFACE_DIFF=1 and SURFACE_DIRECTORIES=1)
endif
SDK_REQUIRED_CAPABILITIES += note-file-format
endif
SURFACE_DIFF ?= 0
ifeq ($(filter $(SURFACE_DIFF),0 1),)
$(error SURFACE_DIFF must be 0 or 1)
endif
ifeq ($(SURFACE_DIFF),1)
ifneq ($(SURFACE_HEX),1)
$(error SURFACE_DIFF requires SURFACE_HEX=1)
endif
SDK_REQUIRED_CAPABILITIES += diff-lines-progress diff-unified-progress
endif
SURFACE_HEX ?= 0
ifeq ($(filter $(SURFACE_HEX),0 1),)
$(error SURFACE_HEX must be 0 or 1)
endif
ifeq ($(SURFACE_HEX),1)
ifneq ($(SURFACE_READER)$(SURFACE_FILE_INFO),11)
$(error SURFACE_HEX requires SURFACE_READER=1 and SURFACE_FILE_INFO=1)
endif
SDK_REQUIRED_CAPABILITIES += hex-byte-model hex-key-input
endif
SURFACE_READER ?= 0
ifeq ($(filter $(SURFACE_READER),0 1),)
$(error SURFACE_READER must be 0 or 1)
endif
ifeq ($(SURFACE_READER),1)
ifneq ($(SURFACE_VFS),1)
$(error SURFACE_READER requires SURFACE_VFS=1)
endif
SDK_REQUIRED_CAPABILITIES += document-layout document-parser-steps document-paint markdown-subset document-text-geometry literal-search-progress
endif
SURFACE_VFS ?= 0
ifeq ($(filter $(SURFACE_VFS),0 1),)
$(error SURFACE_VFS must be 0 or 1)
endif
ifeq ($(SURFACE_VFS),1)
ifneq ($(SURFACE_DEVICE),1)
$(error SURFACE_VFS requires SURFACE_DEVICE=1)
endif
endif
SURFACE_DEVICE ?= 0
ifeq ($(filter $(SURFACE_DEVICE),0 1),)
$(error SURFACE_DEVICE must be 0 or 1)
endif
ifeq ($(SURFACE_DEVICE),1)
ifneq ($(SURFACE_BASIC),1)
$(error SURFACE_DEVICE requires SURFACE_BASIC=1)
endif
endif
SURFACE_BASIC ?= 0
ifeq ($(filter $(SURFACE_BASIC),0 1),)
$(error SURFACE_BASIC must be 0 or 1)
endif
ifeq ($(SURFACE_BASIC),1)
ifneq ($(SURFACE_TERMINAL),1)
$(error SURFACE_BASIC requires SURFACE_TERMINAL=1)
endif
endif
SURFACE_FILE_INFO ?= 0
ifeq ($(filter $(SURFACE_FILE_INFO),0 1),)
$(error SURFACE_FILE_INFO must be 0 or 1)
endif
ifeq ($(SURFACE_FILE_INFO),1)
SDK_REQUIRED_CAPABILITIES += paged-storage-info
ifneq ($(SURFACE_FILE_JOBS),1)
$(error SURFACE_FILE_INFO requires SURFACE_FILE_JOBS=1)
endif
endif
SURFACE_TERMINAL ?= 0
ifeq ($(filter $(SURFACE_TERMINAL),0 1),)
$(error SURFACE_TERMINAL must be 0 or 1)
endif
ifeq ($(SURFACE_TERMINAL),1)
SDK_REQUIRED_CAPABILITIES += terminal-grid
ifneq ($(SURFACE_EDITOR),1)
$(error SURFACE_TERMINAL requires SURFACE_EDITOR=1)
endif
endif
SURFACE_EDITOR ?= 0
ifeq ($(filter $(SURFACE_EDITOR),0 1),)
$(error SURFACE_EDITOR must be 0 or 1)
endif
ifeq ($(SURFACE_EDITOR),1)
SDK_REQUIRED_CAPABILITIES += editor-document editor-control editor-literal-search bounded-pool
ifneq ($(SURFACE_CONTENT_JOBS)$(SURFACE_ARITHMETIC),11)
$(error SURFACE_EDITOR requires SURFACE_CONTENT_JOBS=1 and SURFACE_ARITHMETIC=1)
endif
endif
SURFACE_CONTENT_JOBS ?= 0
ifeq ($(filter $(SURFACE_CONTENT_JOBS),0 1),)
$(error SURFACE_CONTENT_JOBS must be 0 or 1)
endif
ifeq ($(SURFACE_CONTENT_JOBS),1)
SDK_REQUIRED_CAPABILITIES += paged-storage-replacement
ifneq ($(SURFACE_FILE_JOBS),1)
$(error SURFACE_CONTENT_JOBS requires SURFACE_FILE_JOBS=1)
endif
endif
SURFACE_ARITHMETIC ?= 0
ifeq ($(filter $(SURFACE_ARITHMETIC),0 1),)
$(error SURFACE_ARITHMETIC must be 0 or 1)
endif
ifeq ($(SURFACE_ARITHMETIC),1)
SDK_REQUIRED_CAPABILITIES += arithmetic-calculator
ifneq ($(SURFACE_TEXT_INPUT),1)
$(error SURFACE_ARITHMETIC requires SURFACE_TEXT_INPUT=1)
endif
endif
SURFACE_FILE_JOBS ?= 0
ifeq ($(filter $(SURFACE_FILE_JOBS),0 1),)
$(error SURFACE_FILE_JOBS must be 0 or 1)
endif
ifeq ($(SURFACE_FILE_JOBS),1)
SDK_REQUIRED_CAPABILITIES += paged-storage-mutation
ifneq ($(SURFACE_TEXT_INPUT),1)
$(error SURFACE_FILE_JOBS requires SURFACE_TEXT_INPUT=1)
endif
endif
SURFACE_TEXT_INPUT ?= 0
ifeq ($(filter $(SURFACE_TEXT_INPUT),0 1),)
$(error SURFACE_TEXT_INPUT must be 0 or 1)
endif
ifeq ($(SURFACE_TEXT_INPUT),1)
SDK_REQUIRED_CAPABILITIES += text-field-events word-checksum-input
endif
include $(SDK_ROOT)/sdk.mk
SURFACE_UI_SRCS := $(if $(filter 1,$(SURFACE_DESKTOP)),$(SURFACE_ROOT)/shell/desktop.c,) $(addprefix $(SURFACE_ROOT)/,shell/settings.c shell/touch.c shell/surface.c shell/paint.c apps/apps.c apps/calculator.c platform/tikuos/runtime.c platform/tikuos/presentation.c)
ifeq ($(SURFACE_TEXT_INPUT),1)
SURFACE_UI_SRCS += $(SURFACE_ROOT)/shell/text_entry.c
endif
ifeq ($(SURFACE_ARITHMETIC),1)
SURFACE_UI_SRCS += $(SURFACE_ROOT)/apps/arithmetic.c
endif
ifeq ($(SURFACE_EDITOR),1)
SURFACE_UI_SRCS += $(if $(filter 1,$(SURFACE_IMAGE)),$(SURFACE_ROOT)/apps/image.c,) $(if $(filter 1,$(SURFACE_EDITOR_DOCUMENTS)),$(SURFACE_ROOT)/apps/editor_documents.c,) $(SURFACE_ROOT)/apps/editor.c $(SURFACE_ROOT)/shell/editor_context.c
endif
ifeq ($(SURFACE_TERMINAL),1)
SURFACE_UI_SRCS += $(SURFACE_ROOT)/apps/terminal.c
endif
ifeq ($(SURFACE_DEVICE),1)
SURFACE_UI_SRCS += $(SURFACE_ROOT)/apps/device.c
endif
ifeq ($(SURFACE_VFS),1)
SURFACE_UI_SRCS += $(SURFACE_ROOT)/apps/inspector.c
endif
ifeq ($(SURFACE_READER),1)
SURFACE_UI_SRCS += $(SURFACE_ROOT)/apps/reader.c
ifeq ($(SURFACE_NOTES),1)
SURFACE_UI_SRCS += $(SURFACE_ROOT)/apps/notes.c
endif
ifeq ($(SURFACE_DIFF),1)
SURFACE_UI_SRCS += $(SURFACE_ROOT)/apps/diff.c
endif
ifeq ($(SURFACE_HEX),1)
SURFACE_UI_SRCS += $(SURFACE_ROOT)/apps/hex.c
ifeq ($(SURFACE_DOCUMENTS),1)
SURFACE_UI_SRCS += $(SURFACE_ROOT)/shell/document.c
endif
endif
endif
ifeq ($(SURFACE_PREFERENCES),1)
SURFACE_UI_SRCS += $(SURFACE_ROOT)/shell/preferences.c
endif
ifeq ($(SURFACE_TRANSFERS),1)
SURFACE_UI_SRCS += $(SURFACE_ROOT)/shell/transfers.c
endif
SURFACE_UI_OBJS := $(addprefix $(S31_DIR)/,$(SURFACE_UI_SRCS:.c=.o))
SURFACE_ARCHIVES := $(addprefix $(SDK_ROOT)/lib/,libtiku_render.a libtiku_models.a libtiku_font_subset.a)
SURFACE_COMPILE := -DSURFACE_DESKTOP=$(SURFACE_DESKTOP) -DSURFACE_IMAGE=$(SURFACE_IMAGE) -DSURFACE_EDITOR_LINES=$(SURFACE_EDITOR_LINES) -DSURFACE_EDITOR_DOCUMENTS=$(SURFACE_EDITOR_DOCUMENTS) -DSURFACE_NOTES=$(SURFACE_NOTES) -DSURFACE_DIFF=$(SURFACE_DIFF) -DSURFACE_DIRECTORIES=$(SURFACE_DIRECTORIES) -DSURFACE_PREFERENCES=$(SURFACE_PREFERENCES) -DSURFACE_DOCUMENTS=$(SURFACE_DOCUMENTS) -DSURFACE_HEX=$(SURFACE_HEX) -DSURFACE_READER=$(SURFACE_READER) -DSURFACE_VFS=$(SURFACE_VFS) -DSURFACE_DEVICE=$(SURFACE_DEVICE) -DSURFACE_BASIC=$(SURFACE_BASIC) -DSURFACE_FILE_INFO=$(SURFACE_FILE_INFO) -DSURFACE_TERMINAL=$(SURFACE_TERMINAL) -DSURFACE_EDITOR=$(SURFACE_EDITOR) -DSURFACE_CONTENT_JOBS=$(SURFACE_CONTENT_JOBS) -DSURFACE_ARITHMETIC=$(SURFACE_ARITHMETIC) -DSURFACE_FILE_JOBS=$(SURFACE_FILE_JOBS) -DSURFACE_TEXT_INPUT=$(SURFACE_TEXT_INPUT) -include $(S31_DIR)/surface-identity.h $(SDK_CFLAGS) -I$(SURFACE_ROOT)/shell -I$(SURFACE_ROOT)/platform/tikuos -fstack-usage $(if $(filter 1,$(S31_UI_LTO)),-flto,)
APPL_SRCS := $(addprefix $(SURFACE_ROOT)/platform/tikuos/,bindings.c diagnostics.c)
APPL_CFLAGS := -DTIKU_APPLICATION_HEADER='"$(SURFACE_ROOT)/platform/tikuos/boot.h"'
APPL_CFLAGS += -DSURFACE_DESKTOP=$(SURFACE_DESKTOP) $(if $(filter 1,$(SURFACE_BASIC)),-DTIKU_BASIC_JOB_OWNER_ENABLE=1,) -DSURFACE_IMAGE=$(SURFACE_IMAGE) -DSURFACE_EDITOR_LINES=$(SURFACE_EDITOR_LINES) -DSURFACE_EDITOR_DOCUMENTS=$(SURFACE_EDITOR_DOCUMENTS) -DSURFACE_NOTES=$(SURFACE_NOTES) -DSURFACE_DIFF=$(SURFACE_DIFF) -DSURFACE_DIRECTORIES=$(SURFACE_DIRECTORIES) -DSURFACE_PREFERENCES=$(SURFACE_PREFERENCES) -DSURFACE_DOCUMENTS=$(SURFACE_DOCUMENTS) -DSURFACE_HEX=$(SURFACE_HEX) -DSURFACE_READER=$(SURFACE_READER) -DSURFACE_VFS=$(SURFACE_VFS) -DSURFACE_DEVICE=$(SURFACE_DEVICE) -DSURFACE_BASIC=$(SURFACE_BASIC) -DSURFACE_FILE_INFO=$(SURFACE_FILE_INFO) -DSURFACE_TERMINAL=$(SURFACE_TERMINAL) -DSURFACE_EDITOR=$(SURFACE_EDITOR) -DSURFACE_CONTENT_JOBS=$(SURFACE_CONTENT_JOBS) -DSURFACE_ARITHMETIC=$(SURFACE_ARITHMETIC) -DSURFACE_FILE_JOBS=$(SURFACE_FILE_JOBS) -DSURFACE_TEXT_INPUT=$(SURFACE_TEXT_INPUT)
APPL_CFLAGS += -DSURFACE_LCD_ENABLE=$(S31_SURFACE_LCD)
APPL_CFLAGS += -DTIKU_APPLICATION_PROJECT='"tikusurface_s31_$(if $(filter 1,$(S31_SURFACE_LCD)),lcd,diag)"'
APPL_CFLAGS += -DSURFACE_TRANSFERS=$(SURFACE_TRANSFERS)
SURFACE_COMPILE += -DSURFACE_TRANSFERS=$(SURFACE_TRANSFERS)
ifeq ($(SURFACE_TRANSFER_SERVICE),1)
APPL_CFLAGS += -DTIKU_MEDIA_TRANSFER_SERVICE_ENABLE=1 -DTIKU_KITS_NET_TCP_DEFERRED_ENABLE=1
APPL_SRCS += services/storage/tiku_media_transfer_control.c services/storage/tiku_media_transfer_async.c services/storage/tiku_media_transfer_listener.c services/storage/tiku_media_transfer_tcp.c services/storage/tiku_media_transfer_service.c
endif
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
APPL_CFLAGS += -DTIKU_MEDIA_DIRECTORIES_ENABLE=$(SURFACE_DIRECTORIES) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -DTIKU_I2C_BUDGET_ENABLE=1 -DTIKU_S31_SD_ASYNC_ENABLE=1 -DTIKU_TFS_ASYNC_ENABLE=1 -DTIKU_S31_STORAGE_ASYNC_ENABLE=1
S31_LDFLAGS += -Wl,-u,tiku_media_card_mutation_size,-u,tiku_media_card_mutation_init,-u,tiku_media_card_mutation_step,-u,tiku_media_card_mutation_idle,-u,tiku_media_card_mutation_cancel,-u,tiku_media_checkpoint_step,-u,tiku_media_checkpoint_cancel
S31_LDFLAGS += -Wl,-u,tiku_s31_sd_open_begin,-u,tiku_s31_sd_read_begin,-u,tiku_s31_sd_clock_begin,-u,tiku_s31_sd_poll,-u,tiku_s31_sd_cancel,-u,tiku_s31_mfs_async_acquire,-u,tiku_s31_mfs_async_read,-u,tiku_s31_mfs_async_poll,-u,tiku_s31_mfs_async_close
APPL_CFLAGS += -DSURFACE_PPA=0
APPL_SRCS += $(SURFACE_ROOT)/platform/tikuos/display.c $(SURFACE_ROOT)/platform/tikuos/input.c $(SURFACE_ROOT)/platform/tikuos/media_io.c $(SURFACE_ROOT)/platform/tikuos/settings_io.c
APPL_SRCS += services/storage/tiku_media_async.c services/storage/tiku_media_mutation.c services/storage/tiku_media_checkpoint.c arch/esp32s31/tiku_storage_async_io.c
APPL_SRCS += interfaces/input/tiku_touch_gt1151.c drivers/input/gt1151/tiku_drv_input_gt1151.c
APPL_SRCS += interfaces/display/tiku_display.c interfaces/display/tiku_display_async.c \
    arch/esp32s31/tiku_display_arch.c arch/esp32s31/tiku_lcdcam_arch.c arch/esp32s31/tiku_dma_arch.c
endif
ifeq ($(SURFACE_VFS)$(S31_SURFACE_LCD),11)
APPL_SRCS += $(SURFACE_ROOT)/platform/tikuos/vfs_io.c
APPL_CFLAGS += -I$(SURFACE_ROOT)/shell
endif
ifeq ($(SURFACE_DEVICE)$(S31_SURFACE_LCD),11)
APPL_SRCS += $(SURFACE_ROOT)/platform/tikuos/device_io.c $(SURFACE_ROOT)/platform/tikuos/process_io.c
APPL_CFLAGS += -I$(SURFACE_ROOT)/shell
endif
ifeq ($(SURFACE_TERMINAL)$(S31_SURFACE_LCD),11)
APPL_SRCS += $(SURFACE_ROOT)/platform/tikuos/console_io.c
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
$(S31_DIR)/surface-identity.h: FORCE
	python3 $(SURFACE_ROOT)/tools/build_identity.py --product $(SURFACE_ROOT) --firmware $(CURDIR) --output $@
$(SURFACE_UI_OBJS): $(S31_DIR)/.surface-flags $(S31_DIR)/surface-identity.h
$(APPL_EXTRA_OBJS): $(SURFACE_UI_OBJS) $(SURFACE_ARCHIVES) $(S31_DIR)/.surface-flags $(SURFACE_ROOT)/product/tikuos.mk
	$(S31_CC) $(S31_ARCH) -nostdlib -r -fstack-usage $(if $(filter 1,$(S31_UI_LTO)),-Os -ffunction-sections -fdata-sections -flto -flinker-output=nolto-rel,) -Wl,--build-id=none $(SURFACE_UI_OBJS) -Wl,--whole-archive $(SURFACE_ARCHIVES) -Wl,--no-whole-archive -o $@
$(APPL_LINKER): arch/esp32s31/radio_flash.ld $(SURFACE_ROOT)/tools/surface_linker.py $(SURFACE_ROOT)/tools/check_placement.py
	python3 $(SURFACE_ROOT)/tools/surface_linker.py --base $< --output $@ $(if $(filter 1,$(SURFACE_TRANSFER_SERVICE)),--transfer,)
.DELETE_ON_ERROR: $(S31_DIR)/kernel.elf
-include $(SURFACE_UI_OBJS:.o=.d)
endif
