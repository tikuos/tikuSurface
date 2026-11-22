# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# TikuDesktop's shell compiled for TikuOS: Stomme's shell package and kits,
# TikuSurface's shell configuration and the TikuOS platform in desktop/.
#
# DESKTOP_FOUNDATION names the Stomme checkout carrying packages/shell.
# DESKTOP_ASSETS names the TikuDesktop checkout whose strings, sounds and
# icons the desktop shows.
DESKTOP_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))..)
DESKTOP_FOUNDATION ?= $(DESKTOP_ROOT)/foundation
DESKTOP_ASSETS ?=
DESKTOP_BUILD ?= build/desktop-s31
DESKTOP_CC ?= $(CROSS_COMPILE)gcc
DESKTOP_AR ?= $(CROSS_COMPILE)ar
ifeq ($(DESKTOP_ASSETS),)
$(error DESKTOP_ASSETS must name the TikuDesktop checkout that supplies strings and icons)
endif

F := $(DESKTOP_FOUNDATION)
DESKTOP_SHELL_SRCS := $(shell python3 $(DESKTOP_ROOT)/desktop/sources.py --foundation $(F) --kind shell)
DESKTOP_KIT_SRCS := $(filter-out interface/tiku_font.c,$(shell python3 $(DESKTOP_ROOT)/desktop/sources.py --foundation $(F) --kind kits))
DESKTOP_PLATFORM_SRCS := $(wildcard $(DESKTOP_ROOT)/desktop/*.c)
# The candidate's panel presentation and touch-settings codec, unchanged.
DESKTOP_SURFACE_SRCS := platform/tikuos/presentation.c shell/settings.c desktop/heap_profile.c
DESKTOP_EPOCH := $(shell git -C $(F) log -1 --format=%ct 2>/dev/null || echo 1795000000)
# The last code point the desktop's faces carry; see tools/desktop_fonts.py.
DESKTOP_FONT_LAST ?= 0x24f
DESKTOP_GEN := $(DESKTOP_BUILD)/generated

DESKTOP_INCLUDES := $(addprefix -I$(F)/kits/,support device storage interface translation application media editor) \
    -I$(DESKTOP_GEN) \
    $(addprefix -I$(F)/packages/shell/system/,shared/core shared/ui shared/ui/commands shared/ui/windows \
        shared/ui/overlays shared/ui/input deskbar shared/shell shared/shell/adapters shared/shell/session desktop) \
    -I$(DESKTOP_ROOT)/system/product -I$(DESKTOP_ASSETS)/kits/translation -I$(DESKTOP_ROOT)/desktop \
    -I$(DESKTOP_ROOT)/desktop/compat $(addprefix -I,$(wildcard $(F)/kits/*/portable))
DESKTOP_ARCH ?= -march=rv32imafc_zicsr_zifencei -mabi=ilp32f
# Bounds the shared shell sizes for a desktop computer, set for the panel's
# PSRAM: entries per folder view, the largest state file an update reads,
# the native settings table, the resource registry's providers, and the
# remote sessions the listener holds (the panel serves none), and the
# notices one batch holds (an event list is a stack local).  A desktop
# started on a fresh home holds 31 native entries and 52 providers.  The
# settings files are polled for outside edits every 5 s instead of 0.5 s.
# Navigation menus open four levels deep: on an 800-pixel screen no more
# cascading submenus fit beside one another.  Window content caches and
# folder windows' kept pixels take at most 1 MiB each; windows past that
# draw without them.
DESKTOP_LIMITS := -DTIKU_TRK_POSES_MAX=256 -DTIKU_STATE_UPDATE_BYTES=262144 \
    -DTIKU_TRK_NATIVE_ENTRIES_MAX=128 -DTIKU_TRK_RESOURCE_PROVIDERS=128 \
    -DTIKU_REMOTE_SESSIONS=1 -DTIKU_EVENTS_MAX=32 -DTIKU_SHELL_POLL_SCALE=10 \
    -DTIKU_TRK_NAV_DEPTH=4 -DTIKU_WINDOW_CACHE_BYTES=1048576 \
    -DTIKU_TRK_KEPT_PIXELS_BYTES=1048576
# 1: a mark under the finger, a slop before the pointer moves and double taps
# landing on the first tap (desktop/gesture.h); 0: raw touch contacts.
SURFACE_TOUCH_MODE ?= 1
DESKTOP_CFLAGS := $(DESKTOP_ARCH) -Os -g -std=gnu11 -D_DEFAULT_SOURCE -D_GNU_SOURCE \
    -ffunction-sections -fdata-sections -fstack-usage -Wall -Wno-unused-parameter \
    -DTIKU_SURFACE_TIKUOS=1 -DSURFACE_NATIVE_DESKTOP=1 -DSURFACE_LCD_ENABLE=1 -DTIKU_WORK_INLINE=1 \
    -DSURFACE_TOUCH_MODE=$(SURFACE_TOUCH_MODE) \
    -DSURFACE_EPOCH=$(DESKTOP_EPOCH)LL -include $(DESKTOP_ROOT)/desktop/tikuos_libc.h \
    -include $(DESKTOP_GEN)/tiku_sdk_identity.h $(DESKTOP_LIMITS) $(DESKTOP_INCLUDES)

# An argument list that does not match its format is an error in the shell,
# which prints with the format strings of system/product/tiku_shell_product.h,
# and in the kits.  Possible truncation by snprintf stays a warning.
DESKTOP_FORMAT_CFLAGS := -Werror=format -Wno-error=format-truncation

DESKTOP_OBJS := $(addprefix $(DESKTOP_BUILD)/shell/,$(DESKTOP_SHELL_SRCS:.c=.o)) \
    $(addprefix $(DESKTOP_BUILD)/kits/,$(DESKTOP_KIT_SRCS:.c=.o)) \
    $(DESKTOP_BUILD)/assets/tiku_icons_tiku.o $(DESKTOP_BUILD)/fonts/tiku_font.o \
    $(addprefix $(DESKTOP_BUILD)/surface/,$(DESKTOP_SURFACE_SRCS:.c=.o)) \
    $(patsubst $(DESKTOP_ROOT)/desktop/%.c,$(DESKTOP_BUILD)/platform/%.o,$(DESKTOP_PLATFORM_SRCS))
DESKTOP_HEADERS := $(DESKTOP_GEN)/tiku_sdk_identity.h $(DESKTOP_GEN)/tiku_strings_default.h \
    $(DESKTOP_GEN)/tiku_scores_default.h $(DESKTOP_GEN)/tiku_saslprep_tables.h

$(DESKTOP_GEN)/tiku_sdk_identity.h:
	@mkdir -p $(@D)
	@printf '#define TIKU_SDK_IDENTITY "tikusurface-desktop"\n#define TIKU_FOUNDATION_REVISION "%s"\n#define TIKU_SDK_HOST "tikuos"\n' \
	    "$$(git -C $(F) rev-parse HEAD)" > $@
$(DESKTOP_GEN)/tiku_strings_default.h: $(DESKTOP_ASSETS)/assets/strings/en.xml
	@mkdir -p $(@D)
	python3 $(F)/kits/support/tools/genstrings.py $< > $@.tmp && mv $@.tmp $@
$(DESKTOP_GEN)/tiku_scores_default.h:
	@mkdir -p $(@D)
	python3 $(F)/kits/media/tools/genscores.py $(DESKTOP_ASSETS)/assets/sounds > $@.tmp && mv $@.tmp $@
$(DESKTOP_GEN)/tiku_saslprep_tables.h:
	@mkdir -p $(@D)
	python3 $(F)/kits/support/tools/gensaslprep.py > $@.tmp && mv $@.tmp $@

# The launcher holds the shell's main(); the runtime calls it by this name.
$(DESKTOP_BUILD)/shell/system/shared/shell/tiku_trk_launcher.o: private DESKTOP_EXTRA := -Dmain=surface_desktop_main
$(DESKTOP_BUILD)/shell/%.o: $(F)/packages/shell/%.c $(DESKTOP_HEADERS)
	@mkdir -p $(@D)
	$(DESKTOP_CC) $(DESKTOP_CFLAGS) $(DESKTOP_FORMAT_CFLAGS) $(DESKTOP_EXTRA) -MMD -MP -c -o $@ $<
$(DESKTOP_BUILD)/kits/%.o: $(F)/kits/%.c $(DESKTOP_HEADERS)
	@mkdir -p $(@D)
	$(DESKTOP_CC) $(DESKTOP_CFLAGS) $(DESKTOP_FORMAT_CFLAGS) -MMD -MP -c -o $@ $<
$(DESKTOP_BUILD)/assets/tiku_icons_tiku.o: $(DESKTOP_ASSETS)/kits/translation/tiku_icons_tiku.c $(DESKTOP_HEADERS)
	@mkdir -p $(@D)
	$(DESKTOP_CC) $(DESKTOP_CFLAGS) -MMD -MP -c -o $@ $<
$(DESKTOP_BUILD)/platform/%.o: $(DESKTOP_ROOT)/desktop/%.c $(DESKTOP_HEADERS)
	@mkdir -p $(@D)
	$(DESKTOP_CC) $(DESKTOP_CFLAGS) -MMD -MP -c -o $@ $<

# tiku_font.c beside the selected faces: the generated data header takes
# the place of the full one, since the source includes it by name.
$(DESKTOP_GEN)/fonts/tiku_font_data.h: $(F)/kits/interface/tiku_font_data.h $(DESKTOP_ROOT)/tools/desktop_fonts.py
	python3 $(DESKTOP_ROOT)/tools/desktop_fonts.py --input $< --output $@ --last-codepoint $(DESKTOP_FONT_LAST)
$(DESKTOP_GEN)/fonts/tiku_font.c: $(F)/kits/interface/tiku_font.c $(DESKTOP_GEN)/fonts/tiku_font_data.h
	cp $(F)/kits/interface/tiku_font.c $(F)/kits/interface/tiku_font_utf8.inl $(F)/kits/interface/tiku_font_blend.inl $(@D)/
$(DESKTOP_BUILD)/fonts/tiku_font.o: $(DESKTOP_GEN)/fonts/tiku_font.c $(DESKTOP_HEADERS)
	@mkdir -p $(@D)
	$(DESKTOP_CC) $(DESKTOP_CFLAGS) -MMD -MP -c -o $@ $<
$(DESKTOP_BUILD)/surface/%.o: $(DESKTOP_ROOT)/%.c $(DESKTOP_HEADERS)
	@mkdir -p $(@D)
	$(DESKTOP_CC) $(DESKTOP_CFLAGS) -MMD -MP -c -o $@ $<

DESKTOP_ARCHIVE := $(DESKTOP_BUILD)/libsurface_desktop.a
$(DESKTOP_ARCHIVE): $(DESKTOP_OBJS)
	@rm -f $@
	$(DESKTOP_AR) rcs $@ $^

.PHONY: desktop-objects desktop-archive
desktop-objects: $(DESKTOP_OBJS)
desktop-archive: $(DESKTOP_ARCHIVE)
# Objects rebuild when the compile flags change.
DESKTOP_FLAGS_STAMP := $(DESKTOP_BUILD)/flags-$(shell printf '%s' '$(DESKTOP_CFLAGS)' | sha1sum | cut -c1-16)
$(DESKTOP_FLAGS_STAMP):
	@mkdir -p $(dir $@)
	@rm -f $(DESKTOP_BUILD)/flags-*
	@touch $@
$(DESKTOP_OBJS): $(DESKTOP_FLAGS_STAMP)

-include $(DESKTOP_OBJS:.o=.d)
