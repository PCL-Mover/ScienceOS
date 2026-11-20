OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/*
 * GRUB loads the ELF64 image below the 4 GiB border, so the higher-half
 * kernel is linked at KERNEL_VMA but loaded at the lower KERNEL_LMA.  The
 * bootstrap trampoline (kernel/boot.S) runs at BOOT_LMA, builds the page
 * tables that map KERNEL_LMA at KERNEL_VMA, and then jumps into kmain().
 */
KERNEL_VMA = 0xffffffff80000000;
KERNEL_LMA = 0x200000;
BOOT_LMA = 0x100000;

PHDRS
{
    boot    PT_LOAD FLAGS((1 << 0) | (1 << 1) | (1 << 2));
    text    PT_LOAD FLAGS((1 << 0) | (1 << 2));
    rodata  PT_LOAD FLAGS((1 << 2));
    data    PT_LOAD FLAGS((1 << 1) | (1 << 2));
}

SECTIONS
{
    . = BOOT_LMA;

    .multiboot2 : {
        KEEP(*(.multiboot2))
    } :boot

    . = ALIGN(0x1000);

    .bootstrap : {
        *(.bootstrap)
    } :boot

    .bootstrap.rodata : {
        *(.bootstrap.rodata)
    } :boot

    . = ALIGN(0x1000);

    .bootstrap.bss (NOLOAD) : {
        *(.bootstrap.bss)
    } :boot

    . = KERNEL_VMA;

    .text : AT(. - KERNEL_VMA + KERNEL_LMA) {
        *(.text .text.*)
    } :text

    . = ALIGN(0x1000);

    .rodata : AT(. - KERNEL_VMA + KERNEL_LMA) {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(0x1000);

    .data : AT(. - KERNEL_VMA + KERNEL_LMA) {
        *(.data .data.*)
    } :data

    . = ALIGN(0x1000);

    .bss (NOLOAD) : AT(. - KERNEL_VMA + KERNEL_LMA) {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    /*
     * Physical extent of everything the bootloader put in memory for us
     * (trampoline, page tables, boot stack and kernel image).  The PMM must
     * reserve this range: Multiboot2 memory maps do not exclude it.
     */
    __kernel_phys_start = BOOT_LMA;
    __kernel_phys_end = . - KERNEL_VMA + KERNEL_LMA;
}
