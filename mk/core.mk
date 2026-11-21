# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
# Desktop windows, Tracker storage operations and device settings.
BUILD ?= build/core
SURFACE_DESKTOP := 1
SURFACE_TEXT_INPUT := 1
SURFACE_FILE_JOBS := 1
SURFACE_CONTENT_JOBS := 1
SURFACE_FILE_INFO := 1
SURFACE_DIRECTORIES := 1
include mk/tikuos-test.mk
.DEFAULT_GOAL := core-test
.PHONY: core-test
core-test: desktop-test desktop-media-test desktop-runtime-test
