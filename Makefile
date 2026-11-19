# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
PYTHON ?= python3
CROSS_COMPILE ?= riscv32-esp-elf-
# Keep product command-line variables out of foundation refusal-test inputs.
FOUNDATION_MAKE = env -u MAKEFLAGS -u MFLAGS -u MAKEOVERRIDES -u CROSS_COMPILE -u FOUNDATION_TARGET $(MAKE) -C foundation
.DEFAULT_GOAL := check
.PHONY: check check-foundation lint test test-tools test-foundation sdk-host sdk-s31 sdk-s31-lto host test-host test-asan preview capture

check-foundation:
	$(PYTHON) tools/check_foundation.py

lint: check-foundation
	$(PYTHON) tools/check_style.py

test-tools: check-foundation
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py'

check: lint test-tools

test: check test-host

test-foundation: check
	$(FOUNDATION_MAKE) FOUNDATION_PROFILE=portable-ui BUILD=build/surface-host test test-tools check-sdk

sdk-host: check-foundation
	$(FOUNDATION_MAKE) FOUNDATION_PROFILE=portable-ui BUILD=build/surface-host check-sdk

sdk-s31: check-foundation
	$(FOUNDATION_MAKE) FOUNDATION_PROFILE=portable-ui FOUNDATION_TARGET=s31 BUILD=build/surface-s31 CROSS_COMPILE="$(CROSS_COMPILE)" check-sdk

sdk-s31-lto: check-foundation
	$(FOUNDATION_MAKE) FOUNDATION_PROFILE=portable-ui FOUNDATION_TARGET=s31 BUILD=build/surface-s31-lto CFLAGS='-Os -g -flto' CROSS_COMPILE="$(CROSS_COMPILE)" check-sdk

host: sdk-host
	$(MAKE) -f mk/host.mk build/host/surface-preview

test-host: sdk-host
	$(MAKE) -f mk/host.mk test

test-asan: sdk-host
	$(MAKE) -f mk/host.mk BUILD=build/asan CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie' test

preview: host
	build/host/surface-preview

capture: host
	build/host/surface-preview --capture build/host

.PHONY: test-tikuos
test-tikuos: sdk-host
	$(MAKE) -f mk/tikuos-test.mk diagnostics-test

.PHONY: test-display
test-display:
	@test -n "$(TIKUOS_ROOT)" || (echo "Set TIKUOS_ROOT to the matching firmware checkout"; exit 1)
	$(MAKE) -f mk/tikuos-test.mk display-test

.PHONY: test-flash
test-flash:
	@test -n "$(TIKUOS_ROOT)" || (echo "Set TIKUOS_ROOT to the matching firmware checkout"; exit 1)
	TIKUOS_ROOT="$(TIKUOS_ROOT)" $(PYTHON) -m unittest discover -s tests/tikuos -p 'test_flash.py'

.PHONY: test-input
test-input: sdk-host
	$(MAKE) -f mk/tikuos-test.mk input-test

.PHONY: test-media
test-media: sdk-host
	$(MAKE) -f mk/tikuos-test.mk media-test

.PHONY: test-transport
test-transport:
	$(MAKE) -f mk/tikuos-test.mk transport-test

.PHONY: test-settings
test-settings: sdk-host
	$(MAKE) -f mk/tikuos-test.mk settings-test

.PHONY: test-storage-write
test-storage-write:
	$(MAKE) -f mk/tikuos-test.mk storage-write-test

.PHONY: test-nvm-backend
test-nvm-backend:
	$(MAKE) -f mk/tikuos-test.mk nvm-backend-test settings-nvm-test
	TIKUOS_ROOT="$(TIKUOS_ROOT)" $(PYTHON) tests/tikuos/test_flash_ownership.py


.PHONY: test-services
test-services: sdk-host
	$(MAKE) -f mk/tikuos-test.mk services-test
	TIKUOS_ROOT="$(TIKUOS_ROOT)" $(PYTHON) tests/tikuos/test_radio_image.py
