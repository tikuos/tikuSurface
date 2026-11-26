# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# The native desktop in the S31 application slot: TikuDesktop's shell from
# desktop/desktop.mk, partially linked into surface-ui.o, with the panel,
# touch and settings bindings of the LCD runtime.  Included by
# product/tikuos.mk when SURFACE_NATIVE_DESKTOP=1.
ifneq ($(S31_SURFACE_LCD),1)
$(error SURFACE_NATIVE_DESKTOP requires S31_SURFACE_LCD=1)
endif
ifeq ($(filter $(S31_CPU_MHZ),160 320),)
$(error SURFACE_NATIVE_DESKTOP requires S31_CPU_MHZ=160 or 320)
endif
comma := ,
DESKTOP_BUILD := $(S31_DIR)/desktop
DESKTOP_CC := $(S31_CC)
DESKTOP_AR := $(patsubst %gcc,%ar,$(S31_CC))
include $(SURFACE_ROOT)/desktop/desktop.mk

APPL_SRCS := $(addprefix $(SURFACE_ROOT)/platform/tikuos/,bindings.c diagnostics.c \
    desktop_bindings.c desktop_hash.c console_io.c terminal_io.c local_form.c local_programs.c basic_window.c display.c input.c settings_io.c)
APPL_SRCS += tikukits/crypto/sha256/tiku_kits_crypto_sha256.c
include applications/board/gui/sources.mk
APPL_SRCS += $(TIKU_BOARD_GUI_SRCS) applications/board/gui/tiku_draw_program.c
APPL_SRCS += services/link/tiku_link_local.c
APPL_SRCS += applications/board/samples/panel_demo.c
ifeq ($(S31_PROFILE),s31-connected)
S31_SHELL_COMMANDS += DNS NTP
APPL_SRCS += shell/commands/tiku_shell_cmd_dns.c shell/commands/tiku_shell_cmd_ntp.c tikukits/net/ipv4/tiku_kits_net_dns.c \
    tikukits/time/ntp/tiku_kits_time_ntp.c tikukits/time/tiku_kits_time.c
endif
APPL_SRCS += arch/esp32s31/tiku_storage_async_io.c
APPL_SRCS += interfaces/input/tiku_touch_gt1151.c drivers/input/gt1151/tiku_drv_input_gt1151.c
APPL_SRCS += interfaces/display/tiku_display.c interfaces/display/tiku_display_async.c \
    arch/esp32s31/tiku_display_arch.c arch/esp32s31/tiku_lcdcam_arch.c arch/esp32s31/tiku_dma_arch.c \
    arch/esp32s31/tiku_ppa_arch.c arch/esp32s31/tiku_dma2d_arch.c
# Sound Recorder: the microphones as the kernel's /dev/mic.
S31_MIC ?= 1
# Camera: the SC101IOT on the DVP port and the JPEG encoder.
APPL_SRCS += $(SURFACE_ROOT)/platform/tikuos/camera.c arch/esp32s31/tiku_camera_arch.c \
    arch/esp32s31/tiku_jpeg_arch.c
# SURFACE_PPA=1 converts the frame into the panel's format by the S31's
# Pixel Processing Accelerator, with the CPU's loop the fallback; 0 leaves
# the conversion to the CPU.
SURFACE_PPA ?= 1
ifeq ($(filter $(SURFACE_PPA),0 1),)
$(error SURFACE_PPA must be 0 or 1)
endif
APPL_CFLAGS := -DSURFACE_PPA=$(SURFACE_PPA) -DTIKU_APPL_GUI=1 -DTIKU_PROCESS_OBSERVERS_ENABLE=1 -DTIKU_BASIC_JOB_OWNER_ENABLE=1 -Iapplications/board/gui
ifeq ($(S31_PROFILE),s31-connected)
APPL_CFLAGS += -DTIKU_S31_RADIO_IMAGE_BOUNDS_ENABLE=1 -DTIKU_KITS_NET_DNS_ENABLE=1
endif
APPL_CFLAGS += -DTIKU_APPLICATION_HEADER='"$(SURFACE_ROOT)/platform/tikuos/boot.h"'
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
APPL_SRCS += $(SURFACE_ROOT)/platform/tikuos/media_io.c
APPL_CFLAGS += -DTIKU_MEDIA_DIRECTORIES_ENABLE=0
# The desktop's clock calls (clock_gettime, gettimeofday, time) and its
# reads of /Board, the kernel's namespace, come from TikuOS's compat/posix
# library.  desktop/posix.c keeps the C library's file hooks, so the
# library's are left out.
TIKU_POSIX_ENABLE := 1
TIKU_POSIX_MEDIA := 1
TIKU_POSIX_FILE_HOOKS := 0
# The C library's heap is the library's heap group over the runtime's arena,
# with its allocator lock, a short interrupt-masked section.
TIKU_POSIX_HEAP := 1
# Stomme's jobs run on one thread of the library (TIKU_WORK_WORKERS in
# desktop.mk); the desktop's mutexes and conditions, its kits' among them,
# fit the tables with room to spare, and the status line shows their use.
APPL_CFLAGS += -DTIKU_POSIX_THREADS=1 -DTIKU_POSIX_MUTEXES=32 -DTIKU_POSIX_CONDS=16
# The settings files are files of the kernel's file store through the
# library's store group (desktop/posix.c), each through a stage of 16 KiB
# in the runtime's arena: Stomme's session file is 2-4 KiB.
TIKU_POSIX_STORE := 1
APPL_CFLAGS += -DTIKU_POSIX_STAGE_MAX=16384u
APPL_EXTRA_OBJS := $(S31_DIR)/tikuSurface/surface-ui.o
APPL_LINKER := $(S31_DIR)/surface.ld
S31_LDFLAGS += -Wl,-u,tiku_application_stop,-u,surface_runtime_launch,-u,surface_runtime_fail_at
APPL_LINK_DEPS := $(SURFACE_ROOT)/product/tikuos.mk $(SURFACE_ROOT)/desktop/firmware.mk \
    $(SURFACE_ROOT)/tools/surface_linker.py
# After the link: the desktop's code and statics are where the image expects
# them, and no system call the desktop reaches comes from libnosys, whose
# definitions return ENOSYS.  A failing check deletes the image.
APPL_CHECK_ELF = python3 $(SURFACE_ROOT)/tools/desktop_placement.py --elf $@ --map $(S31_DIR)/kernel.map \
    && python3 $(SURFACE_ROOT)/tools/libc_stubs.py --elf $@ --map $(S31_DIR)/kernel.map

# The runtime's entry points and the C-library system calls the desktop
# supplies; the archive contributes only the objects these reach.  The
# shell asks for the built-in applications by a weak reference, which takes
# no object out of an archive, so desktop/builtins.c's list is a root too.
DESKTOP_ROOTS := surface_runtime_start surface_runtime_stop surface_runtime_poll \
    surface_runtime_continue surface_runtime_launch surface_runtime_calibrate \
    surface_runtime_calibration surface_runtime_preferences surface_runtime_set_preferences \
    surface_runtime_stats surface_runtime_frame_stats surface_runtime_input_stats \
    surface_runtime_fail_at surface_touch_map_valid _open tiku_trk_builtins
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
