# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
CC ?= cc
BUILD ?= build/host
SDK_ROOT ?= $(CURDIR)/foundation/build/surface-host/sdk
FOUNDATION_TARGET := host-test
SDK_REQUIRED_CAPABILITIES := local-rendering font-subset allocator-contexts bounded-input monotonic-time static-applications paged-storage-read word-calculator
include $(SDK_ROOT)/sdk.mk
CFLAGS ?= -O2 -g
FLAGS := $(SDK_CFLAGS) -Ishell -Iplatform/host -Wall -Wextra -Werror -Wno-unused-parameter $(CFLAGS)
PRODUCT := shell/settings.c shell/touch.c shell/surface.c shell/paint.c apps/apps.c apps/calculator.c
HOST := platform/host/host.c
HEADERS := $(wildcard shell/*.h platform/tikuos/*.h) platform/host/host.h
LIBS := $(wildcard $(SDK_ROOT)/lib/*.a)
.PHONY: test preview capture
$(BUILD):
	mkdir -p $@
$(BUILD)/test-surface: tests/test_surface.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) tests/test_surface.c tests/heap_trap.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
$(BUILD)/test-runtime: tests/test_runtime.c tests/heap_trap.c $(PRODUCT) platform/tikuos/runtime.c platform/tikuos/runtime.h $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) -Iplatform/tikuos tests/test_runtime.c tests/heap_trap.c $(PRODUCT) platform/tikuos/runtime.c $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
$(BUILD)/test-presentation: tests/test_presentation.c tests/heap_trap.c platform/tikuos/presentation.c platform/tikuos/presentation.h $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) -Iplatform/tikuos tests/test_presentation.c tests/heap_trap.c platform/tikuos/presentation.c $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
$(BUILD)/test-runtime-lcd: platform/tikuos/media.c platform/tikuos/media.h tests/test_runtime_lcd.c tests/heap_trap.c $(PRODUCT) platform/tikuos/runtime.c platform/tikuos/runtime.h platform/tikuos/presentation.c platform/tikuos/presentation.h platform/tikuos/display.h $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) -DSURFACE_LCD_ENABLE=1 -Iplatform/tikuos tests/test_runtime_lcd.c tests/heap_trap.c $(PRODUCT) platform/tikuos/runtime.c platform/tikuos/presentation.c platform/tikuos/media.c $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
test: $(BUILD)/test-settings $(BUILD)/test-touch $(BUILD)/test-surface $(BUILD)/test-runtime $(BUILD)/test-presentation $(BUILD)/test-runtime-lcd
	$(BUILD)/test-settings
	$(BUILD)/test-touch
	$(BUILD)/test-surface
	$(BUILD)/test-runtime
	$(BUILD)/test-presentation
	$(BUILD)/test-runtime-lcd
	$(BUILD)/test-runtime-connected
$(BUILD)/surface-preview: platform/host/main.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) -D_POSIX_C_SOURCE=200809L platform/host/main.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -lX11 -o $@
preview: $(BUILD)/surface-preview
	$(BUILD)/surface-preview
capture: $(BUILD)/surface-preview
	$(BUILD)/surface-preview --capture $(BUILD)

$(BUILD)/test-touch: tests/test_touch.c shell/touch.c shell/touch.h shell/touch_report.h | $(BUILD)
	$(CC) $(FLAGS) tests/test_touch.c shell/touch.c $(SDK_LIBRARIES) -o $@

$(BUILD)/test-settings: tests/test_settings.c $(PRODUCT) $(HOST) $(HEADERS) shell/settings.h | $(BUILD)
	$(CC) $(FLAGS) tests/test_settings.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -o $@


$(BUILD)/test-runtime-connected: platform/tikuos/media.c platform/tikuos/media.h tests/test_runtime_lcd.c tests/heap_trap.c $(PRODUCT) platform/tikuos/runtime.c platform/tikuos/runtime.h platform/tikuos/presentation.c platform/tikuos/presentation.h platform/tikuos/display.h platform/tikuos/services.h $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) -DSURFACE_LCD_ENABLE=1 -DSURFACE_CONNECTED_ENABLE=1 -Iplatform/tikuos tests/test_runtime_lcd.c tests/heap_trap.c $(PRODUCT) platform/tikuos/runtime.c platform/tikuos/presentation.c platform/tikuos/media.c $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
test: $(BUILD)/test-runtime-connected
