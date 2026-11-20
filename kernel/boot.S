/*
 * TangPingOS Multiboot2 (GRUB) bootstrap.
 *
 * GRUB loads ELF64 Multiboot2 kernels below the 4 GiB border and enters in
 * 32-bit protected mode, so the real kernel (linked at 0xffffffff80000000)
 * cannot be loaded directly.  This low-address trampoline builds page tables,
 * enters long mode and jumps to the higher-half entry point (kmain).
 */

.set MB2_HEADER_MAGIC, 0xE85250D6
.set MB2_ARCH, 0
.set MB2_TAG_FRAMEBUFFER, 5
.set MB2_TAG_END, 0
.set KERNEL_PHYS, 0x200000

/* Layout of the low page-table arena (offsets from boot_pagetables). */
.set PT_PML4,     0x0000
.set PT_PDPT_LOW, 0x1000
.set PT_PD_LOW,   0x2000
.set PT_PDPT_H,   0x6000
.set PT_PD_H,     0x7000
.set PT_PDPT_K,   0x17000
.set PT_PD_K,     0x18000
.set PT_SIZE,     0x19000

.section .multiboot2, "a"
.balign 8
mb2_header:
    .long MB2_HEADER_MAGIC
    .long MB2_ARCH
    .long mb2_header_end - mb2_header
    .long -(MB2_HEADER_MAGIC + MB2_ARCH + (mb2_header_end - mb2_header))
    /* Framebuffer request tag (type 5): prefer 1024x768x32, optional. */
    .short MB2_TAG_FRAMEBUFFER
    .short 1
    .long 20
    .long 1024
    .long 768
    .long 32
    /* Tags are 8-byte aligned; the framebuffer tag is only 20 bytes. */
    .long 0
    /* End tag. */
    .short MB2_TAG_END
    .short 0
    .long 8
mb2_header_end:

.section .bootstrap, "ax"
.code32
.globl _start
_start:
    cli
    cld
    movl %eax, boot_magic
    movl %ebx, boot_mbi

    movl $boot_stack_top, %esp

    /* Zero the page-table arena (GRUB also zeroes the segment tail). */
    movl $boot_pagetables, %edi
    xorl %eax, %eax
    movl $(PT_SIZE / 4), %ecx
    rep stosl

    movl $boot_pagetables, %ebx

    /* PML4: identity at [0], higher-half direct map at [256], kernel at [511]. */
    leal PT_PDPT_LOW(%ebx), %eax
    orl $3, %eax
    movl %eax, (PT_PML4 + 0 * 8)(%ebx)
    leal PT_PDPT_H(%ebx), %eax
    orl $3, %eax
    movl %eax, (PT_PML4 + 256 * 8)(%ebx)
    leal PT_PDPT_K(%ebx), %eax
    orl $3, %eax
    movl %eax, (PT_PML4 + 511 * 8)(%ebx)

    /* Identity PML4[0]: PDPT_LOW -> 4 PDs covering 4 GiB. */
    leal PT_PDPT_LOW(%ebx), %edi
    xorl %ecx, %ecx
1:
    movl %ecx, %eax
    shll $12, %eax
    leal PT_PD_LOW(%ebx, %eax, 1), %edx
    orl $3, %edx
    movl %edx, (%edi, %ecx, 8)
    incl %ecx
    cmpl $4, %ecx
    jl 1b

    /* Fill identity PDs with 2 MiB pages. */
    xorl %ebp, %ebp
2:
    movl %ebp, %edi
    shll $12, %edi
    leal PT_PD_LOW(%ebx, %edi, 1), %edi
    xorl %ecx, %ecx
3:
    movl %ebp, %eax
    shll $30, %eax
    movl %ecx, %edx
    shll $21, %edx
    addl %edx, %eax
    orl $0x83, %eax
    movl %eax, (%edi, %ecx, 8)
    incl %ecx
    cmpl $512, %ecx
    jl 3b
    incl %ebp
    cmpl $4, %ebp
    jl 2b

    /* HHDM PML4[256]: PDPT_H -> 16 PDs covering 16 GiB. */
    leal PT_PDPT_H(%ebx), %edi
    xorl %ecx, %ecx
4:
    movl %ecx, %eax
    shll $12, %eax
    leal PT_PD_H(%ebx, %eax, 1), %edx
    orl $3, %edx
    movl %edx, (%edi, %ecx, 8)
    incl %ecx
    cmpl $16, %ecx
    jl 4b

    xorl %ebp, %ebp
5:
    movl %ebp, %edi
    shll $12, %edi
    leal PT_PD_H(%ebx, %edi, 1), %edi
    xorl %ecx, %ecx
6:
    movl %ebp, %eax
    shll $30, %eax
    movl %ecx, %edx
    shll $21, %edx
    addl %edx, %eax
    orl $0x83, %eax
    movl %eax, (%edi, %ecx, 8)
    incl %ecx
    cmpl $512, %ecx
    jl 6b
    incl %ebp
    cmpl $16, %ebp
    jl 5b

    /* Kernel PML4[511] -> PDPT_K[510] -> PD_K covering 128 MiB. */
    leal PT_PD_K(%ebx), %eax
    orl $3, %eax
    movl %eax, (PT_PDPT_K + 510 * 8)(%ebx)

    leal PT_PD_K(%ebx), %edi
    xorl %ecx, %ecx
7:
    movl %ecx, %eax
    shll $21, %eax
    addl $KERNEL_PHYS, %eax
    orl $0x83, %eax
    movl %eax, (%edi, %ecx, 8)
    incl %ecx
    cmpl $64, %ecx
    jl 7b

    /* Enter long mode. */
    leal PT_PML4(%ebx), %eax
    movl %eax, %cr3

    movl %cr4, %eax
    orl $(1 << 5), %eax
    movl %eax, %cr4

    movl $0xC0000080, %ecx
    rdmsr
    orl $(1 << 8), %eax
    wrmsr

    movl %cr0, %eax
    orl $0x80000001, %eax
    movl %eax, %cr0

    lgdt gdt64_ptr

    ljmp $0x08, $long_mode_entry

.code64
long_mode_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    /*
     * Run the C kernel on a stack that lives in the kernel image itself, not
     * in the low identity-mapped bootstrap area.  User address spaces only
     * share PML4[256..511], so any stack switch performed while dispatching a
     * task would fault if the stack were only reachable through the identity
     * map (as it was with Limine, whose kernel stack was already higher half).
     */
    movabsq $boot_kstack_top, %rsp

    movl boot_magic(%rip), %edi
    movl boot_mbi(%rip), %esi
    movabsq $kmain, %rax
    jmp *%rax
.section .bootstrap.rodata, "a"
.balign 8
gdt64:
    .quad 0x0000000000000000
    .quad 0x00af9a000000ffff
    .quad 0x00cf92000000ffff
gdt64_end:
gdt64_ptr:
    .short gdt64_end - gdt64 - 1
    .quad gdt64

.section .bootstrap.bss, "aw", @nobits
.balign 4096
.globl boot_pagetables
boot_pagetables:
    .skip PT_SIZE
.balign 16
boot_magic:
    .skip 4
boot_mbi:
    .skip 4
.balign 16
boot_stack:
    .skip 0x4000
boot_stack_top:

/*
 * Higher-half stack used once long mode is active.  Placed in the kernel
 * image (linked at KERNEL_VMA), so it stays mapped after CR3 switches to a
 * user address space.
 */
.section .bss.bootstack, "aw", @nobits
.balign 16
boot_kstack:
    .skip 0x4000
boot_kstack_top:
