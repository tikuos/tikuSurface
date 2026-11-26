# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
CC ?= cc
TIKUOS_ROOT ?= ..
MEDIA_INCLUDES := -idirafter $(TIKUOS_ROOT) -I$(TIKUOS_ROOT)/compat/posix
MEDIA_CLIENT := $(TIKUOS_ROOT)/compat/posix/tiku_posix_media.c
MEDIA_HEADERS := $(TIKUOS_ROOT)/compat/posix/tiku_posix_media.h $(TIKUOS_ROOT)/compat/posix/tiku_posix_media_io.h
BUILD ?= build/host
SDK_ROOT ?= $(CURDIR)/foundation/build/surface-host/sdk
FOUNDATION_TARGET := host-test
SURFACE_TRANSFERS ?= 0
ifeq ($(filter $(SURFACE_TRANSFERS),0 1),)
$(error SURFACE_TRANSFERS must be 0 or 1)
endif
SDK_REQUIRED_CAPABILITIES := local-rendering font-subset allocator-contexts bounded-input monotonic-time static-applications paged-storage-read word-calculator
SURFACE_DESKTOP ?= 0
ifeq ($(filter $(SURFACE_DESKTOP),0 1),)
$(error SURFACE_DESKTOP must be 0 or 1)
endif
ifeq ($(SURFACE_DESKTOP),1)
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
CFLAGS ?= -O2 -g
FLAGS := $(MEDIA_INCLUDES) -DSURFACE_DESKTOP=$(SURFACE_DESKTOP) -DSURFACE_IMAGE=$(SURFACE_IMAGE) -DSURFACE_EDITOR_LINES=$(SURFACE_EDITOR_LINES) -DSURFACE_EDITOR_DOCUMENTS=$(SURFACE_EDITOR_DOCUMENTS) -DSURFACE_NOTES=$(SURFACE_NOTES) -DSURFACE_DIFF=$(SURFACE_DIFF) -DSURFACE_DIRECTORIES=$(SURFACE_DIRECTORIES) -DSURFACE_PREFERENCES=$(SURFACE_PREFERENCES) -DSURFACE_DOCUMENTS=$(SURFACE_DOCUMENTS) -DSURFACE_HEX=$(SURFACE_HEX) -DSURFACE_READER=$(SURFACE_READER) -DSURFACE_VFS=$(SURFACE_VFS) -DSURFACE_DEVICE=$(SURFACE_DEVICE) -DSURFACE_BASIC=$(SURFACE_BASIC) -DSURFACE_FILE_INFO=$(SURFACE_FILE_INFO) -DSURFACE_TERMINAL=$(SURFACE_TERMINAL) -DSURFACE_EDITOR=$(SURFACE_EDITOR) -DSURFACE_CONTENT_JOBS=$(SURFACE_CONTENT_JOBS) -DSURFACE_ARITHMETIC=$(SURFACE_ARITHMETIC) -DSURFACE_FILE_JOBS=$(SURFACE_FILE_JOBS) -DSURFACE_TEXT_INPUT=$(SURFACE_TEXT_INPUT) $(SDK_CFLAGS) -Ishell -Iplatform/host -Wall -Wextra -Werror -Wno-unused-parameter $(CFLAGS)
ifeq ($(SURFACE_DEVICE),1)
DEVICE_APP := apps/device.c
endif
ifeq ($(SURFACE_VFS),1)
DEVICE_APP += apps/inspector.c
endif
ifeq ($(SURFACE_READER),1)
DEVICE_APP += apps/reader.c
ifeq ($(SURFACE_NOTES),1)
DEVICE_APP += apps/notes.c
endif
ifeq ($(SURFACE_DIFF),1)
DEVICE_APP += apps/diff.c
endif
ifeq ($(SURFACE_HEX),1)
DEVICE_APP += apps/hex.c
ifeq ($(SURFACE_DOCUMENTS),1)
DEVICE_APP += shell/document.c
endif
endif
endif
PRODUCT := $(if $(filter 1,$(SURFACE_DESKTOP)),shell/desktop.c,) $(if $(filter 1,$(SURFACE_IMAGE)),apps/image.c,) $(if $(filter 1,$(SURFACE_EDITOR_DOCUMENTS)),apps/editor_documents.c,) $(if $(filter 1,$(SURFACE_TRANSFERS)),shell/transfers.c,) $(if $(filter 1,$(SURFACE_PREFERENCES)),shell/preferences.c,) $(DEVICE_APP) shell/settings.c shell/touch.c shell/surface.c shell/paint.c apps/apps.c apps/calculator.c
FLAGS += -DSURFACE_TRANSFERS=$(SURFACE_TRANSFERS)
ifeq ($(SURFACE_TEXT_INPUT),1)
PRODUCT += shell/text_entry.c
endif
ifeq ($(SURFACE_ARITHMETIC),1)
PRODUCT += apps/arithmetic.c
endif
ifeq ($(SURFACE_EDITOR),1)
PRODUCT += apps/editor.c shell/editor_context.c
endif
ifeq ($(SURFACE_TERMINAL),1)
PRODUCT += apps/terminal.c
endif
HOST := platform/host/host.c
FLAGS += -include $(BUILD)/identity.h
HEADERS := $(BUILD)/identity.h $(wildcard shell/*.h platform/tikuos/*.h) platform/host/host.h
LIBS := $(wildcard $(SDK_ROOT)/lib/*.a)
.PHONY: test preview capture identity-force
$(BUILD)/identity.h: identity-force
	python3 tools/build_identity.py --product $(CURDIR) --output $@
$(BUILD):
	mkdir -p $@
$(BUILD)/test-surface: tests/test_surface.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) tests/test_surface.c tests/heap_trap.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
$(BUILD)/test-runtime: tests/test_runtime.c tests/heap_trap.c $(PRODUCT) platform/tikuos/runtime.c platform/tikuos/runtime.h $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) -Iplatform/tikuos tests/test_runtime.c tests/heap_trap.c $(PRODUCT) platform/tikuos/runtime.c $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
$(BUILD)/test-presentation: tests/test_presentation.c tests/heap_trap.c platform/tikuos/presentation.c platform/tikuos/presentation.h $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) -Iplatform/tikuos tests/test_presentation.c tests/heap_trap.c platform/tikuos/presentation.c $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
LCD_TEST_RUNTIME := $(if $(filter 1,$(SURFACE_IMAGE)),tests/runtime_image.c,$(if $(filter 1,$(SURFACE_EDITOR_DOCUMENTS)),tests/runtime_documents.c,platform/tikuos/runtime.c))

$(BUILD)/test-runtime-lcd: platform/tikuos/media.c platform/tikuos/media.h tests/test_runtime_lcd.c tests/heap_trap.c $(PRODUCT) $(LCD_TEST_RUNTIME) platform/tikuos/runtime.c platform/tikuos/runtime.h platform/tikuos/presentation.c platform/tikuos/presentation.h platform/tikuos/display.h $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) -DSURFACE_LCD_ENABLE=1 -Iplatform/tikuos tests/test_runtime_lcd.c tests/heap_trap.c $(PRODUCT) $(LCD_TEST_RUNTIME) platform/tikuos/presentation.c platform/tikuos/media.c $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
test: $(BUILD)/test-settings $(BUILD)/test-touch $(BUILD)/test-gesture $(BUILD)/test-posix-layer $(BUILD)/test-board $(BUILD)/test-sys $(BUILD)/test-camera $(BUILD)/test-posix-threads-tsan $(BUILD)/test-posix-threads-asan $(BUILD)/test-surface $(BUILD)/test-runtime $(BUILD)/test-presentation $(BUILD)/test-runtime-lcd
	$(BUILD)/test-product-acceptance theme
	$(BUILD)/test-product-acceptance reset
	$(BUILD)/test-product-acceptance states
	$(BUILD)/test-settings
	$(BUILD)/test-touch
	$(BUILD)/test-gesture
	$(BUILD)/test-posix-layer
	$(BUILD)/test-board
	$(BUILD)/test-sys
	$(BUILD)/test-camera
	setarch $(HOST_MACHINE) -R $(BUILD)/test-posix-threads-tsan
	$(BUILD)/test-posix-threads-asan
	$(BUILD)/test-surface
	$(BUILD)/test-runtime
	$(BUILD)/test-presentation
	$(BUILD)/test-runtime-lcd
	$(BUILD)/test-runtime-connected
$(BUILD)/surface-preview: platform/host/keymap.c platform/host/keymap.h platform/host/main.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) -D_POSIX_C_SOURCE=200809L platform/host/keymap.c platform/host/main.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -lX11 -o $@
preview: $(BUILD)/surface-preview
	$(BUILD)/surface-preview
capture: $(BUILD)/surface-preview
	$(BUILD)/surface-preview --capture $(BUILD)

$(BUILD)/test-touch: $(BUILD)/identity.h tests/test_touch.c shell/touch.c shell/touch.h shell/touch_report.h | $(BUILD)
	$(CC) $(FLAGS) tests/test_touch.c shell/touch.c $(SDK_LIBRARIES) -o $@

$(BUILD)/test-gesture: tests/test_gesture.c desktop/gesture.c desktop/gesture.h mk/host.mk | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror $(CFLAGS) -Idesktop tests/test_gesture.c desktop/gesture.c -o $@

# The native desktop's file layer on the host: tests/posix_host/ stands in for
# newlib's headers, and the sanitizers report what the layer does to memory.
# The layer is built from the pinned foundation's kits, as desktop/desktop.mk
# builds it, not from the exported SDK.  The host C library declares write()
# as must-use and the mkstemp() family's templates as non-null; newlib does
# neither, so those two warnings are off here.
# The host compiler fortifies at -O1, and a fortified call such as pread()
# reaches the C library by its symbol, past rename.h; fortification is off.
POSIX_LAYER_BASE := $(MEDIA_INCLUDES) -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -Wno-unused-result \
    -Wno-nonnull-compare -O1 -g -U_FORTIFY_SOURCE -fno-omit-frame-pointer \
    -include tests/posix_host/rename.h \
    -Itests/posix_host -Idesktop -Idesktop/compat -I$(SDK_ROOT)/include -Ifoundation/kits/support
POSIX_LAYER_FLAGS := $(POSIX_LAYER_BASE) -fsanitize=address,undefined
$(BUILD)/test-posix-layer: tests/test_posix_layer.c desktop/posix.c $(MEDIA_CLIENT) $(MEDIA_HEADERS) desktop/bridge.h desktop/bridge_os.h \
    $(wildcard tests/posix_host/*.h) mk/host.mk | $(BUILD)
	$(CC) $(POSIX_LAYER_FLAGS) tests/test_posix_layer.c desktop/posix.c $(MEDIA_CLIENT) -o $@
# The board's namespace as a Tracker device over a fake kernel namespace;
# Stomme's model header comes from the pinned foundation.
$(BUILD)/test-board: tests/test_board.c desktop/board.c desktop/board.h tests/posix_host/tiku_posix.h \
    mk/host.mk | $(BUILD)
	$(CC) $(MEDIA_INCLUDES) -std=gnu11 -Wall -Wextra -Werror -O1 -g -pthread -fsanitize=address,undefined \
	    -Itests/posix_host -Idesktop -Ifoundation/kits/storage tests/test_board.c desktop/board.c -o $@
# Camera's window over a fake kernel bridge and job runner, drawn with the
# SDK's interface kit; the kits it uses beyond the SDK come from the pinned
# foundation.
$(BUILD)/test-camera: tests/test_camera.c desktop/camera.c desktop/camera.h desktop/bridge.h \
    platform/tikuos/camera.h $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(MEDIA_INCLUDES) $(SDK_CFLAGS) -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
	    -Idesktop $(addprefix -Ifoundation/kits/,support storage interface) \
	    tests/test_camera.c $(SDK_LIBRARIES) -lm -o $@
# The system facts the desktop's kits ask of TikuOS, over fake kernel nodes;
# the kits' headers come from the pinned foundation.
$(BUILD)/test-sys: tests/test_sys.c desktop/sys.c desktop/bridge.h desktop/bridge_os.h \
    tests/posix_host/tiku_posix.h mk/host.mk | $(BUILD)
	$(CC) $(MEDIA_INCLUDES) -std=gnu11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
	    -Itests/posix_host -Idesktop $(addprefix -Ifoundation/kits/,support interface device storage) \
	    tests/test_sys.c desktop/sys.c -o $@
# The layer from several threads: once under the thread sanitizer, run
# without address randomisation, which its memory map does not accept on
# newer kernels; once under the address sanitizer, whose leak check covers
# the kept card bytes.
HOST_MACHINE := $(shell uname -m)
POSIX_THREADS_FLAGS := $(POSIX_LAYER_BASE) -pthread
$(BUILD)/test-posix-threads-tsan $(BUILD)/test-posix-threads-asan: tests/test_posix_threads.c desktop/posix.c $(MEDIA_CLIENT) $(MEDIA_HEADERS) \
    desktop/bridge.h desktop/bridge_os.h $(wildcard tests/posix_host/*.h) mk/host.mk | $(BUILD)
$(BUILD)/test-posix-threads-tsan:
	$(CC) $(POSIX_THREADS_FLAGS) -fsanitize=thread tests/test_posix_threads.c desktop/posix.c $(MEDIA_CLIENT) -o $@
$(BUILD)/test-posix-threads-asan:
	$(CC) $(POSIX_THREADS_FLAGS) -fsanitize=address,undefined tests/test_posix_threads.c desktop/posix.c $(MEDIA_CLIENT) -o $@

$(BUILD)/test-settings: tests/test_settings.c $(PRODUCT) $(HOST) $(HEADERS) shell/settings.h | $(BUILD)
	$(CC) $(FLAGS) tests/test_settings.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -o $@


$(BUILD)/test-runtime-connected: platform/tikuos/media.c platform/tikuos/media.h tests/test_runtime_lcd.c tests/heap_trap.c $(PRODUCT) $(LCD_TEST_RUNTIME) platform/tikuos/runtime.c platform/tikuos/runtime.h platform/tikuos/presentation.c platform/tikuos/presentation.h platform/tikuos/display.h platform/tikuos/services.h $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) -DSURFACE_LCD_ENABLE=1 -DSURFACE_CONNECTED_ENABLE=1 -Iplatform/tikuos tests/test_runtime_lcd.c tests/heap_trap.c $(PRODUCT) $(LCD_TEST_RUNTIME) platform/tikuos/presentation.c platform/tikuos/media.c $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
test: $(BUILD)/test-runtime-connected

$(BUILD)/test-product-acceptance: tests/test_product_acceptance.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) tests/test_product_acceptance.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -o $@

test: $(BUILD)/test-product-acceptance

$(BUILD)/test-sessions: tests/test_sessions.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) tests/test_sessions.c tests/heap_trap.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@

test: test-sessions
.PHONY: test-sessions
test-sessions: $(BUILD)/test-sessions
	$(BUILD)/test-sessions

$(BUILD)/test-word-controls: tests/test_word_controls.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) tests/test_word_controls.c tests/heap_trap.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@

test: test-word-controls
.PHONY: test-word-controls
test-word-controls: $(BUILD)/test-word-controls
	$(BUILD)/test-word-controls

ifeq ($(SURFACE_TEXT_INPUT),1)
$(BUILD)/test-text-entry: tests/test_text_entry.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) tests/test_text_entry.c tests/heap_trap.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
test: test-text-entry
.PHONY: test-text-entry
test-text-entry: $(BUILD)/test-text-entry
	$(BUILD)/test-text-entry
endif

ifeq ($(SURFACE_TEXT_INPUT),1)
$(BUILD)/test-word-ranges: tests/test_word_ranges.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) tests/test_word_ranges.c tests/heap_trap.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
test: test-word-ranges
.PHONY: test-word-ranges
test-word-ranges: $(BUILD)/test-word-ranges
	$(BUILD)/test-word-ranges
endif

ifeq ($(SURFACE_ARITHMETIC),1)
$(BUILD)/test-arithmetic: tests/test_arithmetic.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) tests/test_arithmetic.c tests/heap_trap.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
test: test-arithmetic
.PHONY: test-arithmetic
test-arithmetic: $(BUILD)/test-arithmetic
	$(BUILD)/test-arithmetic
endif

ifeq ($(SURFACE_TERMINAL),1)
$(BUILD)/test-terminal: tests/test_terminal.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) tests/test_terminal.c tests/heap_trap.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
.PHONY: test-terminal
test-terminal: $(BUILD)/test-terminal
	$(BUILD)/test-terminal
test: test-terminal
endif

ifeq ($(SURFACE_BASIC),1)
$(BUILD)/test-basic-workflow: tests/test_basic_workflow.c tests/test_terminal.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) $(filter-out tests/test_terminal.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
.PHONY: test-basic-workflow
test-basic-workflow: $(BUILD)/test-basic-workflow
	$(BUILD)/test-basic-workflow
test: test-basic-workflow
endif

ifeq ($(SURFACE_DEVICE),1)
$(BUILD)/test-device: tests/test_device.c tests/test_terminal.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) $(filter-out tests/test_terminal.c,$(filter %.c,$^)) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
.PHONY: test-device
test-device: $(BUILD)/test-device
	$(BUILD)/test-device
test: test-device
endif

ifeq ($(SURFACE_VFS),1)
$(BUILD)/test-inspector: tests/test_inspector.c tests/test_terminal.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) tests/test_inspector.c tests/heap_trap.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
.PHONY: test-inspector
test-inspector: $(BUILD)/test-inspector
	$(BUILD)/test-inspector
test: test-inspector
endif

ifeq ($(SURFACE_DEVICE),1)
$(BUILD)/test-process-monitor: tests/test_process_monitor.c tests/test_terminal.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) tests/test_process_monitor.c tests/heap_trap.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
.PHONY: test-process-monitor
test-process-monitor: $(BUILD)/test-process-monitor
	$(BUILD)/test-process-monitor
test: test-process-monitor
endif

ifeq ($(SURFACE_READER),1)
$(BUILD)/test-reader: tests/test_reader.c tests/test_terminal.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) tests/test_reader.c tests/heap_trap.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
.PHONY: test-reader
test-reader: $(BUILD)/test-reader
	$(BUILD)/test-reader
test: test-reader
endif

$(BUILD)/test-navigation: tests/test_navigation.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) $(filter %.c,$^) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
.PHONY: navigation-test
navigation-test: $(BUILD)/test-navigation
	$(BUILD)/test-navigation

$(BUILD)/test-keymap: tests/test_keymap.c platform/host/keymap.c platform/host/keymap.h $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) $(filter %.c,$^) $(SDK_LIBRARIES) -o $@
.PHONY: keymap-test
keymap-test: $(BUILD)/test-keymap
	$(BUILD)/test-keymap
test: navigation-test keymap-test

ifeq ($(SURFACE_TEXT_INPUT),1)
$(BUILD)/test-word-checksums: tests/test_word_checksums.c tests/test_text_entry.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) tests/test_word_checksums.c tests/heap_trap.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
.PHONY: word-checksums-test
word-checksums-test: $(BUILD)/test-word-checksums
	$(BUILD)/test-word-checksums
test: word-checksums-test
endif

ifeq ($(SURFACE_PREFERENCES),1)
$(BUILD)/test-device-preferences: tests/test_device_preferences.c tests/test_terminal.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) tests/test_device_preferences.c tests/heap_trap.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
.PHONY: device-preferences-test
device-preferences-test: $(BUILD)/test-device-preferences
	$(BUILD)/test-device-preferences
test: device-preferences-test
endif

ifeq ($(SURFACE_TRANSFERS),1)
$(BUILD)/test-transfers: tests/test_transfers.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) tests/test_transfers.c tests/heap_trap.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
.PHONY: transfers-test
transfers-test: $(BUILD)/test-transfers
	$(BUILD)/test-transfers
test: transfers-test
endif

ifeq ($(SURFACE_EDITOR),1)
$(BUILD)/test-editor-context: tests/test_editor_context.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) $(filter %.c,$^) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
.PHONY: editor-context-test
editor-context-test: $(BUILD)/test-editor-context
	$(BUILD)/test-editor-context
test: editor-context-test
endif

$(BUILD)/test-host-pointer: tests/test_host_pointer.c platform/host/main.c platform/host/keymap.c $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) -D_POSIX_C_SOURCE=200809L -ffunction-sections -fdata-sections tests/test_host_pointer.c platform/host/keymap.c $(SDK_LIBRARIES) -Wl,--gc-sections -lX11 -o $@
.PHONY: host-pointer-test
host-pointer-test: $(BUILD)/test-host-pointer
	$(BUILD)/test-host-pointer

ifeq ($(SURFACE_DESKTOP),1)
$(BUILD)/test-desktop: tests/test_desktop.c tests/heap_trap.c $(PRODUCT) $(HOST) $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) tests/test_desktop.c tests/heap_trap.c $(PRODUCT) $(HOST) $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
.PHONY: desktop-test
desktop-test: $(BUILD)/test-desktop
	$(BUILD)/test-desktop
endif

ifeq ($(SURFACE_DESKTOP),1)
$(BUILD)/test-desktop-runtime $(BUILD)/test-desktop-connected: tests/test_desktop_runtime.c tests/test_runtime_lcd.c tests/heap_trap.c $(PRODUCT) platform/tikuos/runtime.c platform/tikuos/media.c platform/tikuos/presentation.c $(HEADERS) $(LIBS) mk/host.mk | $(BUILD)
	$(CC) $(FLAGS) -DSURFACE_LCD_ENABLE=1 -DSURFACE_CONNECTED_ENABLE=$(if $(filter %-connected,$@),1,0) -Iplatform/tikuos tests/test_desktop_runtime.c tests/heap_trap.c $(PRODUCT) platform/tikuos/runtime.c platform/tikuos/media.c platform/tikuos/presentation.c $(SDK_LIBRARIES) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -o $@
.PHONY: desktop-runtime-test
desktop-runtime-test: $(BUILD)/test-desktop-runtime $(BUILD)/test-desktop-connected
	$(BUILD)/test-desktop-runtime
	$(BUILD)/test-desktop-connected
endif
