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

$(BUILD)/test-display-s31: tests/tikuos/test_display_s31.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_display_arch.c $(TIKUOS_ROOT)/arch/esp32s31/tiku_display_arch.h $(TIKUOS_ROOT)/interfaces/display/tiku_display.c $(TIKUOS_ROOT)/interfaces/display/tiku_display_async.c $(TIKUOS_ROOT)/interfaces/display/tiku_display.h $(TIKUOS_ROOT)/interfaces/display/tiku_display_async.h mk/tikuos-test.mk | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -ffunction-sections -fdata-sections tests/tikuos/test_display_s31.c $(TIKUOS_ROOT)/interfaces/display/tiku_display.c $(TIKUOS_ROOT)/interfaces/display/tiku_display_async.c -Wl,--gc-sections -o $@

$(BUILD)/test-input-binding: tests/tikuos/test_input_binding.c platform/tikuos/input.c $(TIKUOS_ROOT)/interfaces/input/tiku_touch_gt1151.c $(TIKUOS_ROOT)/drivers/input/gt1151/tiku_drv_input_gt1151.c $(HEADERS) $(TIKUOS_ROOT)/interfaces/input/tiku_touch.h | $(BUILD)
	$(CC) $(KERNEL_FLAGS) -Iplatform/tikuos $(filter %.c,$^) -o $@
.PHONY: input-test
input-test: $(BUILD)/test-i2c-budget $(BUILD)/test-input-binding
	$(BUILD)/test-input-binding
	$(BUILD)/test-i2c-budget

MEDIA_CORE := $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.c $(TIKUOS_ROOT)/kernel/fs/tiku_mfs_copy.c $(TIKUOS_ROOT)/services/storage/tiku_media_store.c $(TIKUOS_ROOT)/services/storage/tiku_media_async.c
$(BUILD)/test-media: tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(HOST) $(PRODUCT) $(HEADERS) $(TIKUOS_ROOT)/kernel/fs/tiku_mfs.h $(TIKUOS_ROOT)/services/storage/tiku_media_store.h $(TIKUOS_ROOT)/services/storage/tiku_media_async.h $(TIKUOS_ROOT)/services/storage/tiku_media_card.h $(TIKUOS_ROOT)/kernel/fs/tiku_mfs_query.h mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter %.c,$^) $(SDK_LIBRARIES) -o $@
.PHONY: media-test
media-test: $(BUILD)/test-media $(BUILD)/test-media-transfer-browser
	$(BUILD)/test-media
	$(BUILD)/test-media-transfer-browser

$(BUILD)/test-media-transfer-browser: tests/tikuos/test_media_transfer_browser.c tests/tikuos/test_media.c platform/tikuos/media.c platform/tikuos/media_io.c $(MEDIA_CORE) $(TIKUOS_ROOT)/services/storage/tiku_media_transfer.c $(TIKUOS_ROOT)/services/storage/tiku_media_transfer.h $(HOST) $(PRODUCT) $(HEADERS) mk/tikuos-test.mk | $(BUILD)
	$(CC) $(FLAGS) -DTIKU_MEDIA_NAMES_ENABLE=1 -DTIKU_MEDIA_ASYNC_ENABLE=1 -Iplatform/tikuos -I$(TIKUOS_ROOT) $(filter-out tests/tikuos/test_media.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -o $@

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
