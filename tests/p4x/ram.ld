/*
 * Tiku Operating System v0.06
 * Simple. Persistent. Intelligent.
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * ram.ld - P4X SRAM loader and PSRAM desktop for RAM qualification.
 *
 * The image links into the board-selected span of HP SRAM with a 4 KiB
 * stack at its top; the 64-byte fault record sits below the LP ROM data at
 * 0x5010fa80.  Any other .persistent or .retained section fails the link.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
ENTRY(tiku_esp32p4_reset_handler)


MEMORY {
    SRAM (rwx) : ORIGIN = 0x4ff50000, LENGTH = 256K
    ASSETS (rwx) : ORIGIN = 0x49800000, LENGTH = 4M
    ZEROS (rw) : ORIGIN = 0x49c00000, LENGTH = 1M
    FAULT (rw) : ORIGIN = 0x5010fa40, LENGTH = 64
}
SECTIONS
{
    .surface_assets : ALIGN(64) {
        __assets_start = .;
        *libsurface_desktop.a:*(.text .text.* .rodata .rodata.* .srodata .srodata.* .data .data.* .sdata .sdata.*)
        . = ALIGN(4);
        __assets_end = .;
    } > ASSETS
    .surface_zero (NOLOAD) : ALIGN(64) {
        __surface_psram_bss_start = .;
        *libsurface_desktop.a:*(.bss .bss.* .sbss .sbss.* COMMON)
        __surface_psram_bss_end = .;
    } > ZEROS
    .text : ALIGN(64) {
        KEEP(*(.text.entry))
        *(.text .text.* .p4.xip.text)
        . = ALIGN(4);
    } > SRAM
    .p4x_hash (TYPE = SHT_PROGBITS) : { __payload_digest = .; BYTE(0); . += 31; } > SRAM
    .rodata : ALIGN(4) {
        *(.rodata .rodata.* .srodata .srodata.*)
        . = ALIGN(4);
    } > SRAM
    .data : ALIGN(4) {
        *(.data .data.*)
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        . = ALIGN(4);
    } > SRAM
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > SRAM
    .fault_record (NOLOAD) : {
        KEEP(*tiku_fault_arch.o(.retained))
    } > FAULT
    ASSERT(SIZEOF(.fault_record) == 64, "P4 fault record must be 64 bytes")
    .forbidden (NOLOAD) : {
        *(.persistent .persistent.* .retained .retained.*)
    } > SRAM
    __stack = ORIGIN(SRAM) + LENGTH(SRAM);
    __stack_bottom = __stack - 4K;
    __tier_sram_start = ALIGN(__bss_end, 64);
    __tier_sram_end = __stack_bottom;
    __tier_sram_extra_start = __stack_bottom;
    __tier_sram_extra_end = __stack_bottom;
    ASSERT(__tier_sram_start <= __tier_sram_end, "internal memory tier overlaps stack")
    ASSERT(__bss_end <= __stack_bottom, "P4 diagnostic overlaps stack")
    ASSERT(SIZEOF(.forbidden) == 0, "P4 diagnostic must be storage-free")
    /DISCARD/ : { *(.comment .note .note.*) }
}
