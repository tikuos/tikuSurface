# ESP32-P4X standalone desktop, built from the TikuOS checkout.
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
P4X_FLASH_BOOT := 1
include tikuSurface/tests/p4x/ram.mk

.PHONY: install-p4x-desktop
install-p4x-desktop: surface-p4x-ram
	$(PYTHON) tikuSurface/tools/install_p4x.py --build "$(BUILD_DIR)" --port "$(PORT)" --serial "$(SERIAL)"
