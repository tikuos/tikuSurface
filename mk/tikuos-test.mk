# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
TIKUOS_ROOT ?= ..
BUILD ?= build/tikuos-test
include mk/host.mk
.DEFAULT_GOAL := diagnostics-test
BENCH := $(TIKUOS_ROOT)/TikuBench/tests/host/kernel
KERNEL_FLAGS := -std=c11 -Wall -Wextra -Werror -I$(BENCH)/include -I$(TIKUOS_ROOT) $(CFLAGS)
$(BUILD)/vfs.o: $(TIKUOS_ROOT)/kernel/vfs/tiku_vfs.c mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP -c $< -o $@
$(BUILD)/vfs-hooks.o: $(BENCH)/vfs_host_hooks.c mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP -c $< -o $@
$(BUILD)/test-diagnostics: tests/test_diagnostics.c tests/heap_trap.c platform/tikuos/diagnostics.c platform/tikuos/runtime.c platform/tikuos/runtime.h platform/tikuos/boot.h $(PRODUCT) $(HEADERS) $(LIBS) $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk
	$(CC) $(FLAGS) -Iplatform/tikuos -I$(TIKUOS_ROOT) tests/test_diagnostics.c tests/heap_trap.c platform/tikuos/diagnostics.c platform/tikuos/runtime.c $(PRODUCT) $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
.PHONY: diagnostics-test
diagnostics-test: $(BUILD)/test-diagnostics
	$(BUILD)/test-diagnostics
-include $(BUILD)/vfs.d $(BUILD)/vfs-hooks.d

$(BUILD)/test-display-async: tests/tikuos/test_display_async.c $(TIKUOS_ROOT)/interfaces/display/tiku_display.c $(TIKUOS_ROOT)/interfaces/display/tiku_display_async.c $(TIKUOS_ROOT)/interfaces/display/tiku_display.h $(TIKUOS_ROOT)/interfaces/display/tiku_display_async.h | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror $(CFLAGS) -I$(TIKUOS_ROOT) $(filter %.c,$^) -o $@
.PHONY: display-test
display-test: $(BUILD)/test-display-async $(BUILD)/test-display-s31 $(BUILD)/test-display-binding $(BUILD)/test-owner-schedule $(BUILD)/test-owner-schedule-threads
	$(BUILD)/test-display-async
	$(BUILD)/test-display-s31
	$(BUILD)/test-display-binding
	$(BUILD)/test-owner-schedule
	$(BUILD)/test-owner-schedule-threads

$(BUILD)/test-owner-schedule $(BUILD)/test-owner-schedule-threads: tests/tikuos/test_owner_schedule.c platform/tikuos/bindings.c platform/tikuos/runtime.h platform/tikuos/boot.h $(TIKUOS_ROOT)/kernel/process/tiku_process.c $(TIKUOS_ROOT)/kernel/timers/tiku_timer.c mk/tikuos-test.mk | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror $(CFLAGS) -DSURFACE_LCD_ENABLE=1 -DTIKU_CLOCK_ARCH_SECOND=128 -DTIKU_THREADS_ENABLE=$(if $(filter %-threads,$@),1,0) -include $(BENCH)/include/tiku.h -Iplatform/tikuos -I$(TIKUOS_ROOT) -ffunction-sections -fdata-sections $(filter %.c,$^) -Wl,--gc-sections -o $@

$(BUILD)/test-display-binding: tests/tikuos/test_display_binding.c platform/tikuos/display.c platform/tikuos/display.h platform/tikuos/runtime.h $(TIKUOS_ROOT)/interfaces/display/tiku_display.h mk/tikuos-test.mk | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror $(CFLAGS) -Iplatform/tikuos -I$(TIKUOS_ROOT) tests/tikuos/test_display_binding.c platform/tikuos/display.c -o $@

$(BUILD)/test-display-s31: tests/tikuos/test_display_s31.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_display_arch.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_display_arch.h $(TIKUOS_ROOT)/arch/esp32s31/tiku_lcdcam_arch.c $(TIKUOS_ROOT)/interfaces/display/tiku_display.c $(TIKUOS_ROOT)/interfaces/display/tiku_display_async.c $(TIKUOS_ROOT)/interfaces/display/tiku_display.h $(TIKUOS_ROOT)/interfaces/display/tiku_display_async.h mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -ffunction-sections -fdata-sections tests/tikuos/test_display_s31.c $(TIKUOS_ROOT)/interfaces/display/tiku_display.c $(TIKUOS_ROOT)/interfaces/display/tiku_display_async.c -Wl,--gc-sections -o $@

$(BUILD)/test-input-binding: tests/tikuos/test_input_binding.c platform/tikuos/input.c $(TIKUOS_ROOT)/interfaces/input/tiku_touch_gt1151.c $(TIKUOS_ROOT)/drivers/input/gt1151/tiku_drv_input_gt1151.c $(HEADERS) $(TIKUOS_ROOT)/interfaces/input/tiku_touch.h | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -Iplatform/tikuos $(filter %.c,$^) -o $@
.PHONY: input-test
input-test: $(BUILD)/test-i2c-budget $(BUILD)/test-input-binding
	$(BUILD)/test-input-binding
	$(BUILD)/test-i2c-budget

MEDIA_CORE := $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs_copy.c $(TIKUOS_ROOT)/services/storage/tiku_media_store.c $(TIKUOS_ROOT)/services/storage/tiku_media_async.c $(TIKUOS_ROOT)/services/storage/tiku_media_mutation.c
$(BUILD)/test-media: tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.h $(TIKUOS_ROOT)/services/storage/tiku_media_store.h $(TIKUOS_ROOT)/services/storage/tiku_media_async.h $(TIKUOS_ROOT)/services/storage/tiku_media_card.h $(TIKUOS_ROOT)/kernel/fs/tiku_mfs_query.h mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=$(SURFACE_DIRECTORIES) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter %.c,$^) $(SDK_LIBRARIES) -o $@
.PHONY: media-test
media-test: $(BUILD)/test-media $(BUILD)/test-media-transfer-browser $(BUILD)/test-posix-card
	$(BUILD)/test-media
	$(BUILD)/test-media-transfer-browser
	$(BUILD)/test-posix-card

# The desktop's file layer writing to the card: desktop/posix.c built as for
# test-posix-layer, over the real bridge, media service and MFS on a RAM disk
# (tests/tikuos/card_fixture.c), both under the address sanitizer.
CARD_FIXTURE_FLAGS := -std=gnu11 -Wall -Wextra -Werror -O1 -g -fno-omit-frame-pointer \
    -fsanitize=address,undefined -DTIKU_MEDIA_DIRECTORIES_ENABLE=0 -DTIKU_MEDIA_NAMES_ENABLE=1 \
    -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT)
$(BUILD)/test-posix-card: tests/test_posix_card.c desktop/posix.c desktop/bridge.h desktop/bridge_os.h \
    tests/tikuos/card_fixture.c tests/tikuos/card_fixture.h platform/tikuos/media_io.c platform/tikuos/media_io.h \
    $(MEDIA_CORE) $(wildcard tests/posix_host/*.h) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(CARD_FIXTURE_FLAGS) -c tests/tikuos/card_fixture.c -o $(BUILD)/card-fixture.o
	$(CC) $(CARD_FIXTURE_FLAGS) -c platform/tikuos/media_io.c -o $(BUILD)/card-media-io.o
	$(foreach f,$(MEDIA_CORE),$(CC) $(CARD_FIXTURE_FLAGS) -c $(f) -o $(BUILD)/card-$(notdir $(f:.c=.o)) &&) true
	$(CC) $(POSIX_LAYER_FLAGS) -Wl,--wrap=calloc -Itests tests/test_posix_card.c desktop/posix.c $(BUILD)/card-fixture.o \
	    $(BUILD)/card-media-io.o $(foreach f,$(MEDIA_CORE),$(BUILD)/card-$(notdir $(f:.c=.o))) -o $@

$(BUILD)/test-media-transfer-browser: tests/tikuos/test_media_transfer_browser.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(TIKUOS_ROOT)/services/storage/tiku_media_transfer.c $(TIKUOS_ROOT)/services/storage/tiku_media_transfer.h $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=$(SURFACE_DIRECTORIES) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@

$(BUILD)/test-i2c-budget: tests/tikuos/test_i2c_budget.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_i2c_arch.c | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -ffunction-sections -fdata-sections $< -Wl,--gc-sections -o $@

$(BUILD)/test-spi-async: tests/tikuos/test_spi_async.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_spi_arch.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_spi_arch.h | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -ffunction-sections -fdata-sections $< -Wl,--gc-sections -o $@
$(BUILD)/test-sd-async: tests/tikuos/test_sd_async.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_sdcard_arch.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_sdcard_arch.h | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -ffunction-sections -fdata-sections $< -Wl,--gc-sections -o $@
.PHONY: transport-test
transport-test: $(BUILD)/test-spi-async $(BUILD)/test-sd-async $(BUILD)/test-media-transport
	$(BUILD)/test-spi-async
	$(BUILD)/test-sd-async
	$(BUILD)/test-media-transport

$(BUILD)/test-media-transport: tests/tikuos/test_media_transport.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_mfs_block_arch.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_mfs_block_arch.h | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -ffunction-sections -fdata-sections $< -Wl,--gc-sections -o $@

SETTINGS_CORE := platform/tikuos/runtime.c platform/tikuos/presentation.c platform/tikuos/media.c platform/tikuos/settings_io.c $(TIKUOS_ROOT)/kernel/fs/tiku_tfs.c
SETTINGS_FLAGS := -DTIKU_TFS_ASYNC_ENABLE=1 -DTIKU_S31_STORAGE_ASYNC_ENABLE=1 -DTIKU_TFS_MAX_SLOTS=512 -DTIKU_TFS_MIN_SLOTS=256 -DTIKU_TFS_SLOT_DATA=4092 -DTIKU_TFS_SECT=4096
$(BUILD)/test-settings-store: tests/test_runtime_lcd.c tests/tikuos/test_settings_store.c tests/heap_trap.c $(SETTINGS_CORE) $(PRODUCT) $(HEADERS) $(LIBS) $(TIKUOS_ROOT)/kernel/fs/tiku_tfs.h $(TIKUOS_ROOT)/kernel/fs/tiku_nvm_backend.h mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) $(SETTINGS_FLAGS) -DSURFACE_LCD_ENABLE=1 -DSURFACE_SETTINGS_REAL_TFS=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter %.c,$^) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
.PHONY: settings-test
settings-test: $(BUILD)/test-settings-store
	$(BUILD)/test-settings-store

TFS_ASYNC_CORE := $(TIKUOS_ROOT)/kernel/fs/tiku_tfs.c
TFS_ASYNC_HEADERS := $(TIKUOS_ROOT)/kernel/fs/tiku_tfs_async.h $(TIKUOS_ROOT)/kernel/fs/tiku_tfs.h $(TIKUOS_ROOT)/kernel/fs/tiku_nvm_backend.h
$(BUILD)/test-tfs-async $(BUILD)/test-tfs-async-rotate: tests/tikuos/test_tfs_async.c $(TFS_ASYNC_CORE) $(TFS_ASYNC_HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror $(CFLAGS) $(SETTINGS_FLAGS) -DTIKU_TFS_ASYNC_ENABLE=1 -DTIKU_TFS_HOLD_ENABLE=1 -DTIKU_TFS_ROTATE=$(if $(filter %-rotate,$@),1,0) -I$(TIKUOS_ROOT) $(filter %.c,$^) -o $@
.PHONY: storage-write-test
storage-write-test: $(BUILD)/test-tfs-async $(BUILD)/test-tfs-async-rotate
	$(BUILD)/test-tfs-async
	$(BUILD)/test-tfs-async-rotate

$(BUILD)/test-flash-slice: tests/tikuos/test_flash_slice.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_storage_async_io.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_storage_async.h mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) $< -o $@
.PHONY: nvm-backend-test
nvm-backend-test: $(BUILD)/test-flash-slice $(BUILD)/test-nvm-backend
	$(BUILD)/test-flash-slice
	$(BUILD)/test-nvm-backend

$(BUILD)/test-nvm-backend: tests/tikuos/test_nvm_backend.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_storage_arch.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_storage_async.h mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) $< -o $@

$(BUILD)/test-settings-nvm: tests/tikuos/test_settings_nvm.c tests/tikuos/test_nvm_backend.c platform/tikuos/settings_io.c $(TFS_ASYNC_CORE) $(TFS_ASYNC_HEADERS) $(TIKUOS_ROOT)/arch/esp32s31/tiku_storage_arch.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_storage_async.h mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) $(SETTINGS_FLAGS) -Iplatform/tikuos tests/tikuos/test_settings_nvm.c platform/tikuos/settings_io.c $(TFS_ASYNC_CORE) -o $@
.PHONY: settings-nvm-test
settings-nvm-test: $(BUILD)/test-settings-nvm
	$(BUILD)/test-settings-nvm

TRANSFER_STATUS_CORE := $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs_copy.c $(TIKUOS_ROOT)/services/storage/tiku_media_store.c $(TIKUOS_ROOT)/services/storage/tiku_media_transfer.c
$(BUILD)/test-transfer-status: tests/tikuos/test_transfer_status.c $(TRANSFER_STATUS_CORE) $(TIKUOS_ROOT)/services/storage/tiku_media_transfer_server.c $(TIKUOS_ROOT)/services/storage/tiku_media_transfer_server.h mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -include $(BENCH)/include/tiku.h -DTIKU_KITS_NET_TCP_ENABLE=1 -DTIKU_MEDIA_TRANSFER_STATUS_ENABLE=1 $< $(TRANSFER_STATUS_CORE) -o $@
$(BUILD)/test-services-binding: tests/tikuos/test_services_binding.c platform/tikuos/services.c platform/tikuos/services.h platform/tikuos/runtime.h $(TIKUOS_ROOT)/services/storage/tiku_media_transfer_server.h mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -DTIKU_WIRELESS_OBSERVE_ENABLE=1 -Iplatform/tikuos $(filter %.c,$^) -o $@
.PHONY: services-test
services-test: $(BUILD)/test-transfer-status $(BUILD)/test-services-binding
	$(BUILD)/test-transfer-status
	$(BUILD)/test-services-binding
	$(BUILD)/test-radio-observation

$(BUILD)/test-radio-observation: tests/tikuos/test_radio_observation.c platform/tikuos/services.c $(TIKUOS_ROOT)/drivers/wifi/esp/tiku_drv_wifi_esp.c $(TIKUOS_ROOT)/interfaces/wireless/tiku_wireless.h mk/tikuos-test.mk | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter $(CFLAGS) -include $(BENCH)/include/tiku.h -I$(TIKUOS_ROOT) -I$(BENCH)/include -DTIKU_WIRELESS_OBSERVE_ENABLE=1 -Iplatform/tikuos -ffunction-sections -fdata-sections $< platform/tikuos/services.c -Wl,--gc-sections -o $@
services-test: $(BUILD)/test-radio-observation

$(BUILD)/test-transfer-control: tests/tikuos/test_transfer_control.c $(TRANSFER_STATUS_CORE) $(TIKUOS_ROOT)/services/storage/tiku_media_transfer_control.c $(TIKUOS_ROOT)/services/storage/tiku_media_transfer_control.h mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -include $(BENCH)/include/tiku.h $< $(TRANSFER_STATUS_CORE) -o $@
$(BUILD)/test-transfer-control-names: tests/tikuos/test_transfer_control.c $(TRANSFER_STATUS_CORE) $(TIKUOS_ROOT)/services/storage/tiku_media_transfer_control.c $(TIKUOS_ROOT)/services/storage/tiku_media_transfer_control.h mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -include $(BENCH)/include/tiku.h -DTIKU_MEDIA_NAMES_ENABLE=1 $< $(TRANSFER_STATUS_CORE) -o $@
.PHONY: transfer-control-test
transfer-control-test: $(BUILD)/test-transfer-control $(BUILD)/test-transfer-control-names
	$(BUILD)/test-transfer-control
	$(BUILD)/test-transfer-control-names
services-test: transfer-control-test

$(BUILD)/test-media-rename $(BUILD)/test-media-rename-names: tests/tikuos/test_media_rename.c $(BENCH)/test_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.h $(TIKUOS_ROOT)/services/storage/tiku_media_store.c $(TIKUOS_ROOT)/services/storage/tiku_media_store.h $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP -DTIKU_MEDIA_NAMES_ENABLE=$(if $(filter %-names,$@),1,0) $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -o $@
.PHONY: media-rename-test
media-rename-test: $(BUILD)/test-media-rename $(BUILD)/test-media-rename-names
	$(BUILD)/test-media-rename
	$(BUILD)/test-media-rename-names
media-test: media-rename-test

-include $(BUILD)/test-media-rename.d $(BUILD)/test-media-rename-names.d

$(BUILD)/test-media-checkpoint: tests/tikuos/test_media_checkpoint.c $(BENCH)/test_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs_checkpoint.h $(TIKUOS_ROOT)/services/storage/tiku_media_checkpoint.c $(TIKUOS_ROOT)/services/storage/tiku_media_checkpoint.h $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -o $@
.PHONY: media-checkpoint-test
media-checkpoint-test: $(BUILD)/test-media-checkpoint
	$(BUILD)/test-media-checkpoint
media-test: media-checkpoint-test
-include $(BUILD)/test-media-checkpoint.d

$(BUILD)/test-media-mutation: tests/tikuos/test_media_mutation.c $(BENCH)/test_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs_mutation.h $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -o $@
.PHONY: media-mutation-test
media-mutation-test: $(BUILD)/test-media-mutation
	$(BUILD)/test-media-mutation
	$(BUILD)/test-media-mutation-queue
media-test: media-mutation-test
-include $(BUILD)/test-media-mutation.d

$(BUILD)/test-media-mutation-queue: tests/tikuos/test_media_mutation_queue.c tests/tikuos/test_media_mutation.c $(BENCH)/test_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/services/storage/tiku_media_mutation.c $(TIKUOS_ROOT)/services/storage/tiku_media_mutation.h $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -o $@
media-mutation-test: $(BUILD)/test-media-mutation-queue
-include $(BUILD)/test-media-mutation-queue.d

$(BUILD)/test-media-card-mutation: tests/tikuos/test_media_card_mutation.c tests/tikuos/test_media_mutation.c $(BENCH)/test_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/services/storage/tiku_media_card.c $(TIKUOS_ROOT)/services/storage/tiku_media_mutation.c $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP -ffunction-sections -fdata-sections $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -Wl,--gc-sections -o $@
.PHONY: media-card-mutation-test
media-card-mutation-test: $(BUILD)/test-media-card-mutation
	$(BUILD)/test-media-card-mutation
media-test: media-card-mutation-test
-include $(BUILD)/test-media-card-mutation.d

$(BUILD)/test-media-replace: tests/tikuos/test_media_replace.c tests/tikuos/test_media_mutation.c $(BENCH)/test_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs_mutation.h $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -o $@
.PHONY: media-replace-test
media-replace-test: $(BUILD)/test-media-replace
	$(BUILD)/test-media-replace
media-test: media-replace-test
-include $(BUILD)/test-media-replace.d

$(BUILD)/test-media-discard: tests/tikuos/test_media_discard.c tests/tikuos/test_media_mutation.c $(BENCH)/test_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs_mutation.h $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -o $@
.PHONY: media-discard-test
media-discard-test: $(BUILD)/test-media-discard
	$(BUILD)/test-media-discard
media-test: media-discard-test
-include $(BUILD)/test-media-discard.d

$(BUILD)/test-media-discard-directories: tests/tikuos/test_media_discard.c tests/tikuos/test_media_mutation.c $(BENCH)/test_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs_mutation.h $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=1 -MMD -MP $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -o $@
.PHONY: media-discard-directories-test
media-discard-directories-test: $(BUILD)/test-media-discard-directories
	$(BUILD)/test-media-discard-directories
media-test: media-discard-directories-test
-include $(BUILD)/test-media-discard-directories.d

ifeq ($(SURFACE_EDITOR),1)
$(BUILD)/test-editor: tests/tikuos/test_editor.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=$(SURFACE_DIRECTORIES) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: editor-test
editor-test: $(BUILD)/test-editor
	$(BUILD)/test-editor
endif

$(BUILD)/test-console: tests/tikuos/test_console.c platform/tikuos/console_io.c platform/tikuos/console_io.h | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror $(CFLAGS) -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter %.c,$^) -o $@
.PHONY: console-test
console-test: $(BUILD)/test-console

$(BUILD)/test-console-shell: tests/tikuos/test_console_shell.c tests/tikuos/console_uart_fixture.h platform/tikuos/console_io.c $(TIKUOS_ROOT)/shell/tiku_shell_parser.c $(TIKUOS_ROOT)/shell/tiku_shell_io.c $(TIKUOS_ROOT)/shell/commands/tiku_shell_cmd_echo.c | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -DTIKU_SHELL_ALIASES_ENABLE=0 -include tests/tikuos/console_uart_fixture.h -Iplatform/tikuos $(filter %.c,$^) -o $@
console-test: $(BUILD)/test-console-shell
	$(BUILD)/test-console
	$(BUILD)/test-console-shell

ifeq ($(SURFACE_FILE_INFO),1)
$(BUILD)/test-file-information: tests/tikuos/test_file_information.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=$(SURFACE_DIRECTORIES) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: file-information-test
file-information-test: $(BUILD)/test-file-information
	$(BUILD)/test-file-information
endif

$(BUILD)/test-basic-owned: tests/tikuos/test_basic_owned.c $(wildcard $(TIKUOS_ROOT)/basic/*.inl $(TIKUOS_ROOT)/basic/*.h) $(TIKUOS_ROOT)/basic/tiku_basic.c | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Wno-unused-function $(CFLAGS) -DTIKU_TEST_HOST=1 -DTIKU_BASIC_JOB_OWNER_ENABLE=1 -I$(TIKUOS_ROOT)/TikuBench/tests/host -I$(TIKUOS_ROOT) $< -o $@
.PHONY: basic-owned-test
basic-owned-test: $(BUILD)/test-basic-owned
	$(BUILD)/test-basic-owned

$(BUILD)/test-console-basic: tests/tikuos/test_console_basic.c tests/tikuos/test_console.c platform/tikuos/console_io.c platform/tikuos/console_io.h $(wildcard $(TIKUOS_ROOT)/basic/*.inl $(TIKUOS_ROOT)/basic/*.h) | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Wno-unused-function $(CFLAGS) -DSURFACE_BASIC=1 -DTIKU_SHELL_ENABLE=1 -DTIKU_BASIC_JOB_OWNER_ENABLE=1 -DTIKU_TEST_HOST=1 -Iplatform/tikuos -I$(TIKUOS_ROOT)/TikuBench/tests/host -I$(TIKUOS_ROOT) tests/tikuos/test_console_basic.c platform/tikuos/console_io.c -o $@
.PHONY: console-basic-test
console-basic-test: $(BUILD)/test-console-basic
	$(BUILD)/test-console-basic

$(BUILD)/test-device-io: tests/tikuos/test_device_io.c platform/tikuos/device_io.c platform/tikuos/device_io.h $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -Ishell -Iplatform/tikuos $(filter %.c %.o,$^) -o $@
.PHONY: device-io-test
device-io-test: $(BUILD)/test-device-io
	$(BUILD)/test-device-io

$(BUILD)/test-vfs-io: tests/tikuos/test_vfs_io.c platform/tikuos/vfs_io.c platform/tikuos/vfs_io.h shell/vfs_contract.h $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -Ishell -Iplatform/tikuos $(filter %.c %.o,$^) -o $@
.PHONY: vfs-io-test
vfs-io-test: $(BUILD)/test-vfs-io
	$(BUILD)/test-vfs-io

$(BUILD)/test-process-io: tests/tikuos/test_process_io.c platform/tikuos/process_io.c platform/tikuos/device_io.h shell/process_contract.h $(TIKUOS_ROOT)/kernel/process/tiku_process.c $(TIKUOS_ROOT)/kernel/timers/tiku_timer.c mk/tikuos-test.mk | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror $(CFLAGS) -I$(TIKUOS_ROOT) -DTIKU_CLOCK_ARCH_SECOND=128 -include $(BENCH)/include/tiku.h -Ishell -Iplatform/tikuos -ffunction-sections -fdata-sections $(filter %.c,$^) -Wl,--gc-sections -o $@
.PHONY: process-io-test
process-io-test: $(BUILD)/test-process-io
	$(BUILD)/test-process-io

ifeq ($(SURFACE_READER),1)
$(BUILD)/test-reader-media: tests/tikuos/test_reader_media.c tests/tikuos/test_editor.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=$(SURFACE_DIRECTORIES) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_editor.c tests/tikuos/test_media.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: reader-media-test
reader-media-test: $(BUILD)/test-reader-media
	$(BUILD)/test-reader-media
endif

$(BUILD)/test-media-directories: tests/tikuos/test_media_directories.c tests/tikuos/test_media_mutation.c $(BENCH)/test_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs_mutation.h $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -o $@
.PHONY: media-directories-test
media-directories-test: $(BUILD)/test-media-directories
	$(BUILD)/test-media-directories
-include $(BUILD)/test-media-directories.d

ifeq ($(SURFACE_DIRECTORIES),1)
$(BUILD)/test-tracker-directories: tests/tikuos/test_tracker_directories.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=1 -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: tracker-directories-test
tracker-directories-test: $(BUILD)/test-tracker-directories
	$(BUILD)/test-tracker-directories
$(BUILD)/test-tracker-tree-copy: tests/tikuos/test_tracker_tree_copy.c tests/tikuos/test_tracker_directories.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=1 -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c tests/tikuos/test_tracker_directories.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: tracker-tree-copy-test
tracker-tree-copy-test: $(BUILD)/test-tracker-tree-copy
	$(BUILD)/test-tracker-tree-copy
endif

$(BUILD)/test-media-subtrees: tests/tikuos/test_media_subtrees.c tests/tikuos/test_media_mutation.c $(BENCH)/test_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs_mutation.h $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -o $@
.PHONY: media-subtrees-test
media-subtrees-test: $(BUILD)/test-media-subtrees
	$(BUILD)/test-media-subtrees
-include $(BUILD)/test-media-subtrees.d

$(BUILD)/test-media-tree-copy: tests/tikuos/test_media_tree_copy.c tests/tikuos/test_media_subtrees.c tests/tikuos/test_media_mutation.c $(BENCH)/test_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs_mutation.h $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -o $@
.PHONY: media-tree-copy-test
media-tree-copy-test: $(BUILD)/test-media-tree-copy
	$(BUILD)/test-media-tree-copy
-include $(BUILD)/test-media-tree-copy.d

$(BUILD)/test-media-directory-upgrade: tests/tikuos/test_media_directory_upgrade.c tests/tikuos/test_media_mutation.c $(BENCH)/test_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs_mutation.h $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -o $@
.PHONY: media-directory-upgrade-test
media-directory-upgrade-test: $(BUILD)/test-media-directory-upgrade
	$(BUILD)/test-media-directory-upgrade
-include $(BUILD)/test-media-directory-upgrade.d

ifeq ($(SURFACE_DIRECTORIES),1)
$(BUILD)/test-tracker-directory-upgrade: tests/tikuos/test_tracker_directory_upgrade.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=1 -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: tracker-directory-upgrade-test
tracker-directory-upgrade-test: $(BUILD)/test-tracker-directory-upgrade
	$(BUILD)/test-tracker-directory-upgrade
endif

$(BUILD)/test-media-card-capacity: tests/tikuos/test_media_card_mutation.c tests/tikuos/test_media_mutation.c $(BENCH)/test_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs_mutation.h $(TIKUOS_ROOT)/services/storage/tiku_media_card.c platform/tikuos/media_io.h $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=1 -MMD -MP -ffunction-sections -fdata-sections $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -Wl,--gc-sections -o $@
.PHONY: media-card-capacity-test
media-card-capacity-test: $(BUILD)/test-media-card-capacity
	$(BUILD)/test-media-card-capacity
-include $(BUILD)/test-media-card-capacity.d

ifeq ($(SURFACE_HEX),1)
$(BUILD)/test-hex-media: tests/tikuos/test_hex.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=$(SURFACE_DIRECTORIES) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: hex-media-test
hex-media-test: $(BUILD)/test-hex-media
	$(BUILD)/test-hex-media
endif

ifeq ($(SURFACE_DOCUMENTS),1)
$(BUILD)/test-documents: tests/tikuos/test_documents.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=$(SURFACE_DIRECTORIES) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: documents-test
documents-test: $(BUILD)/test-documents
	$(BUILD)/test-documents
endif

ifeq ($(SURFACE_EDITOR)$(SURFACE_BASIC),11)
$(BUILD)/test-editor-search: tests/tikuos/test_editor_search.c tests/tikuos/test_editor.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=$(SURFACE_DIRECTORIES) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c tests/tikuos/test_editor.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -Wl,--wrap=tiku_pool_allocator -o $@
.PHONY: editor-search-test
editor-search-test: $(BUILD)/test-editor-search
	$(BUILD)/test-editor-search
endif

ifeq ($(SURFACE_PREFERENCES),1)
$(BUILD)/test-preferences: tests/tikuos/test_preferences.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=$(SURFACE_DIRECTORIES) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: preferences-test
preferences-test: $(BUILD)/test-preferences
	$(BUILD)/test-preferences
endif

$(BUILD)/test-preferences-nvm: tests/tikuos/test_settings_nvm.c tests/tikuos/test_nvm_backend.c platform/tikuos/settings_io.c $(TFS_ASYNC_CORE) $(TFS_ASYNC_HEADERS) $(TIKUOS_ROOT)/arch/esp32s31/tiku_storage_arch.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_storage_async.h mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) $(SETTINGS_FLAGS) -DSURFACE_PREFERENCES_NVM=1 -Iplatform/tikuos tests/tikuos/test_settings_nvm.c platform/tikuos/settings_io.c $(TFS_ASYNC_CORE) -o $@
.PHONY: preferences-nvm-test
preferences-nvm-test: $(BUILD)/test-preferences-nvm
	$(BUILD)/test-preferences-nvm

ifeq ($(SURFACE_HEX),1)
$(BUILD)/test-hex-keyboard: tests/tikuos/test_hex_keyboard.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=$(SURFACE_DIRECTORIES) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: hex-keyboard-test
hex-keyboard-test: $(BUILD)/test-hex-keyboard
	$(BUILD)/test-hex-keyboard
endif

ifeq ($(SURFACE_DIFF),1)
$(BUILD)/test-diff: tests/tikuos/test_diff.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=$(SURFACE_DIRECTORIES) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: diff-test
diff-test: $(BUILD)/test-diff
	$(BUILD)/test-diff
endif

$(BUILD)/test-media-stream: tests/tikuos/test_media_stream.c tests/tikuos/test_media_mutation.c $(BENCH)/test_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs_mutation.h $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -o $@
.PHONY: media-stream-test
media-stream-test: $(BUILD)/test-media-stream $(BUILD)/test-media-stream-queue
	$(BUILD)/test-media-stream
	$(BUILD)/test-media-stream-queue
media-test: media-stream-test
-include $(BUILD)/test-media-stream.d

$(BUILD)/test-media-stream-queue: tests/tikuos/test_media_stream_queue.c tests/tikuos/test_media_mutation_queue.c tests/tikuos/test_media_mutation.c $(BENCH)/test_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/services/storage/tiku_media_mutation.c $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -o $@
-include $(BUILD)/test-media-stream-queue.d

$(BUILD)/test-media-transfer-async: tests/tikuos/test_media_transfer_async.c tests/tikuos/test_media_mutation_queue.c tests/tikuos/test_media_mutation.c $(BENCH)/test_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/services/storage/tiku_media_mutation.c $(TIKUOS_ROOT)/services/storage/tiku_media_store.c $(TIKUOS_ROOT)/services/storage/tiku_media_transfer_async.c $(TIKUOS_ROOT)/services/storage/tiku_media_transfer_async.h $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -o $@
.PHONY: media-transfer-async-test
media-transfer-async-test: $(BUILD)/test-media-transfer-async
	$(BUILD)/test-media-transfer-async
media-test: media-transfer-async-test
-include $(BUILD)/test-media-transfer-async.d

$(BUILD)/test-transfer-control-async: tests/tikuos/test_transfer_control.c $(TRANSFER_STATUS_CORE) $(TIKUOS_ROOT)/services/storage/tiku_media_transfer_control.c $(TIKUOS_ROOT)/services/storage/tiku_media_transfer_control.h mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -include $(BENCH)/include/tiku.h -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 $< $(TRANSFER_STATUS_CORE) -o $@
.PHONY: transfer-control-async-test
transfer-control-async-test: $(BUILD)/test-transfer-control-async
	$(BUILD)/test-transfer-control-async
services-test: transfer-control-async-test

$(BUILD)/test-tcp-deferred: tests/tikuos/test_tcp_deferred.c $(TIKUOS_ROOT)/tikukits/net/ipv4/tiku_kits_net_tcp.c $(TIKUOS_ROOT)/tikukits/net/ipv4/tiku_kits_net_tcp.h mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP -ffunction-sections -fdata-sections $< -Wl,--gc-sections -o $@
.PHONY: tcp-deferred-test
tcp-deferred-test: $(BUILD)/test-tcp-deferred
	$(BUILD)/test-tcp-deferred
-include $(BUILD)/test-tcp-deferred.d

$(BUILD)/test-transfer-listener: tests/tikuos/test_transfer_listener.c tests/tikuos/test_media_transfer_async.c $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP $< $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -o $@
.PHONY: transfer-listener-test
transfer-listener-test: $(BUILD)/test-transfer-listener
	$(BUILD)/test-transfer-listener
media-test: transfer-listener-test
-include $(BUILD)/test-transfer-listener.d

$(BUILD)/transfer-tcp-fixture.o: tests/tikuos/transfer_tcp_fixture.c tests/tikuos/transfer_tcp_fixture.h tests/tikuos/test_tcp_deferred.c mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP -ffunction-sections -fdata-sections -c $< -o $@
$(BUILD)/test-transfer-tcp: tests/tikuos/test_transfer_tcp.c tests/tikuos/transfer_tcp_fixture.h $(BUILD)/transfer-tcp-fixture.o $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP -ffunction-sections -fdata-sections $< $(BUILD)/transfer-tcp-fixture.o $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -Wl,--gc-sections -o $@
.PHONY: transfer-tcp-test
transfer-tcp-test: $(BUILD)/test-transfer-tcp
	$(BUILD)/test-transfer-tcp
media-test: transfer-tcp-test
-include $(BUILD)/test-transfer-tcp.d $(BUILD)/transfer-tcp-fixture.d

$(BUILD)/test-transfer-service: tests/tikuos/test_transfer_service.c tests/tikuos/transfer_tcp_fixture.h $(BUILD)/transfer-tcp-fixture.o $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -MMD -MP -ffunction-sections -fdata-sections $< $(BUILD)/transfer-tcp-fixture.o $(BUILD)/vfs.o $(BUILD)/vfs-hooks.o -Wl,--gc-sections -o $@
.PHONY: transfer-service-test
transfer-service-test: $(BUILD)/test-transfer-service
	$(BUILD)/test-transfer-service
media-test: transfer-service-test
-include $(BUILD)/test-transfer-service.d

$(BUILD)/test-transfer-binding: tests/tikuos/test_transfer_binding.c platform/tikuos/services.c platform/tikuos/services.h mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -DSURFACE_TRANSFERS=1 -DTIKU_WIRELESS_OBSERVE_ENABLE=1 -Iplatform/tikuos tests/tikuos/test_transfer_binding.c platform/tikuos/services.c -o $@
.PHONY: transfer-binding-test
transfer-binding-test: $(BUILD)/test-transfer-binding
	$(BUILD)/test-transfer-binding
services-test: transfer-binding-test

ifeq ($(SURFACE_DIFF)$(SURFACE_DOCUMENTS),11)
$(BUILD)/test-tracker-diff: tests/tikuos/test_tracker_diff.c tests/tikuos/test_documents.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=$(SURFACE_DIRECTORIES) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_documents.c tests/tikuos/test_media.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: tracker-diff-test
tracker-diff-test: $(BUILD)/test-tracker-diff
	$(BUILD)/test-tracker-diff
media-test: tracker-diff-test
endif

ifeq ($(SURFACE_NOTES),1)
$(BUILD)/test-note-io: tests/tikuos/test_note_io.c tests/tikuos/test_editor.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=1 -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c tests/tikuos/test_editor.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: note-io-test
note-io-test: $(BUILD)/test-note-io
	$(BUILD)/test-note-io
endif

ifeq ($(SURFACE_NOTES),1)
$(BUILD)/test-notes: tests/tikuos/test_notes.c tests/tikuos/test_editor.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=1 -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c tests/tikuos/test_editor.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: notes-test
notes-test: $(BUILD)/test-notes
	$(BUILD)/test-notes
endif

.PHONY: runtime-lcd-test
runtime-lcd-test: $(BUILD)/test-runtime-lcd
	$(BUILD)/test-runtime-lcd

ifeq ($(SURFACE_DIFF),1)
$(BUILD)/test-patch: tests/tikuos/test_patch.c tests/tikuos/test_diff.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=$(SURFACE_DIRECTORIES) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c tests/tikuos/test_diff.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: patch-test
patch-test: $(BUILD)/test-patch
	$(BUILD)/test-patch
endif

ifeq ($(SURFACE_EDITOR_DOCUMENTS),1)
$(BUILD)/test-editor-documents: tests/tikuos/test_editor_documents.c tests/tikuos/test_editor.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=1 -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c tests/tikuos/test_editor.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: editor-documents-test
editor-documents-test: $(BUILD)/test-editor-documents
	$(BUILD)/test-editor-documents
endif

ifeq ($(SURFACE_EDITOR_LINES),1)
$(BUILD)/test-editor-lines: tests/tikuos/test_editor_lines.c tests/tikuos/test_editor.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=1 -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c tests/tikuos/test_editor.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: editor-lines-test
editor-lines-test: $(BUILD)/test-editor-lines
	$(BUILD)/test-editor-lines
endif

ifeq ($(SURFACE_EDITOR_LINES),1)
$(BUILD)/test-editor-indent: tests/tikuos/test_editor_indent.c tests/tikuos/test_editor_lines.c tests/tikuos/test_editor.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=1 -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_editor_lines.c tests/tikuos/test_media.c tests/tikuos/test_editor.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: editor-indent-test
editor-indent-test: $(BUILD)/test-editor-indent
	$(BUILD)/test-editor-indent
endif

ifeq ($(SURFACE_IMAGE),1)
$(BUILD)/test-image: tests/tikuos/test_image.c tests/tikuos/test_editor.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=$(SURFACE_DIRECTORIES) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c tests/tikuos/test_editor.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -Wl,--wrap=tiku_picture -o $@
.PHONY: image-test
image-test: $(BUILD)/test-image
	$(BUILD)/test-image
endif

ifeq ($(SURFACE_DESKTOP),1)
$(BUILD)/test-desktop-media: tests/tikuos/test_desktop_media.c tests/tikuos/test_tracker_directories.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) $(LIBS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_DIRECTORIES_ENABLE=1 -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c tests/tikuos/test_tracker_directories.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@
.PHONY: desktop-media-test
desktop-media-test: $(BUILD)/test-desktop-media
	$(BUILD)/test-desktop-media
endif

.PHONY: local-programs-test
local-programs-test: $(BUILD)/test-local-programs
	$(BUILD)/test-local-programs

$(BUILD)/test-local-programs: tests/tikuos/test_local_programs.c platform/tikuos/local_programs.c $(TIKUOS_ROOT)/services/link/tiku_link_local.c $(TIKUOS_ROOT)/services/link/tiku_link.c desktop/local_programs.h | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror $(CFLAGS) -include $(BENCH)/include/tiku.h -I$(TIKUOS_ROOT)/applications/board/gui -I$(TIKUOS_ROOT) $(filter %.c,$^) -o $@

.PHONY: desktop-requests-test
desktop-requests-test: $(BUILD)/test-desktop-requests
	$<

$(BUILD)/test-desktop-requests: tests/tikuos/test_desktop_requests.c desktop/runtime.c desktop/host.c desktop/bridge.h desktop/bridge_os.h shell/settings.c | $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror $(CFLAGS) -ffunction-sections -fdata-sections \
	    -DSURFACE_NATIVE_DESKTOP=1 -DSURFACE_DESKTOP=1 -DSURFACE_LCD_ENABLE=1 \
	    -I$(SDK_ROOT)/include -Ifoundation/packages/shell/system/deskbar -Ifoundation/packages/shell/system/shared/ui \
	    $(addprefix -Ifoundation/kits/,storage support interface device) \
	    $< desktop/host.c shell/settings.c -Wl,--gc-sections -pthread -o $@
