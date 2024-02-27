/* Linker script for Nordic Semiconductor nRF5 devices
 *
 * Version: Sourcery G++ 4.5-1
 * Support: https://support.codesourcery.com/GNUToolchain/
 *
 * Copyright (c) 2007, 2008, 2009, 2010 CodeSourcery, Inc.
 *
 * The authors hereby grant permission to use, copy, modify, distribute,
 * and license this software and its documentation for any purpose, provided
 * that existing copyright notices are retained in all copies and that this
 * notice is included verbatim in any distributions.  No written agreement,
 * license, or royalty fee is required for any of the authorized uses.
 * Modifications to this software may be copyrighted by their authors
 * and need not follow the licensing terms described here, provided that
 * the new terms are clearly indicated on the first page of each file where
 * they apply.
 */
OUTPUT_FORMAT ("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")

/* Linker script to place sections and symbol values. Should be used together
 * with other linker script that defines memory regions FLASH and RAM.
 * It references following symbols, which must be defined in code:
 *   Reset_Handler : Entry of reset handler
 *
 * It defines following symbols, which code can use without definition:
 *   __exidx_start
 *   __exidx_end
 *   __etext
 *   __data_start__
 *   __preinit_array_start
 *   __preinit_array_end
 *   __init_array_start
 *   __init_array_end
 *   __fini_array_start
 *   __fini_array_end
 *   __data_end__
 *   __bss_start__
 *   __bss_end__
 *   __HeapBase
 *   __HeapLimit
 *   __StackLimit
 *   __StackTop
 *   __stack
 *   __bssnz_start__
 *   __bssnz_end__
 */
ENTRY(Reset_Handler)

SECTIONS
{
    .imghdr (NOLOAD):
    {
        . = . + _imghdr_size;
    } > FLASH

    __text = .;

    .text :
    {
        __isr_vector_start = .;
        KEEP(*(.isr_vector))
        __isr_vector_end = .;
        EXCLUDE_FILE(*liblc3.a: *libm.a: *libsamplerate.a: *audio_usb.o) *(.text*)

        KEEP(*(.init))
        KEEP(*(.fini))

        . = ALIGN(4);
        /* preinit data */
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);

        . = ALIGN(4);
        /* init data */
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);

        . = ALIGN(4);
        /* finit data */
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN (__fini_array_end = .);

        /* .ctors */
        *crtbegin.o(.ctors)
        *crtbegin?.o(.ctors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors)
        *(SORT(.ctors.*))
        *(.ctors)

        /* .dtors */
        *crtbegin.o(.dtors)
        *crtbegin?.o(.dtors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors)
        *(SORT(.dtors.*))
        *(.dtors)

        EXCLUDE_FILE(*liblc3.a: *libsamplerate.a:) *(.rodata*)

        *(.eh_frame*)
        PROVIDE(mynewt_main = main);
        . = ALIGN(4);
    } > FLASH


    .net_core_img :
    {
        PROVIDE(net_core_img_start = .);
        KEEP(*(.net_core_img));
        PROVIDE(net_core_img_end = .);
    } > FLASH

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
        . = ALIGN(4);
    } > FLASH

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        . = ALIGN(4);
    } > FLASH
    __exidx_end = .;

    __etext = .;

    .vector_relocation :
    {
        . = ALIGN(4);
        __vector_tbl_reloc__ = .;
        . = . + (__isr_vector_end - __isr_vector_start);
        . = ALIGN(4);
    } > RAM

    /* Section for app-net cores IPC */
    .ipc 0x20000400 (NOLOAD):
    {
        . = ALIGN(4);
        *(.ipc)
        . = ALIGN(4);
    } > RAM

    /* This section will be zeroed by RTT package init */
    .rtt (NOLOAD):
    {
        . = ALIGN(4);
        *(.rtt)
        . = ALIGN(4);
    } > RAM

    .data :
    {
        __data_start__ = .;
        *(vtable)
        *(.data*)

        *(.jcr)
        *liblc3.a:(.text* .rodata*)
        *libsamplerate.a:(.text* .rodata*)
        *audio_usb.o(.text*)
        *libm.a:(.text*)
        . = ALIGN(4);
        /* All data end */
        __data_end__ = .;
    } > RAM AT > FLASH

    /* Non-zeroed BSS.  This section is similar to BSS, but does not get zeroed at init-time.
     */
    .bssnz :
    {
        . = ALIGN(4);
        __bssnz_start__ = .;
        *(.bss.core.nz*)
        . = ALIGN(4);
        __bssnz_end__ = .;
    } > RAM

    .bss :
    {
        . = ALIGN(4);
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    /* Heap starts after BSS */
    . = ALIGN(8);
    __HeapBase = .;

    /* .stack_dummy section doesn't contains any symbols. It is only
     * used for linker to calculate size of stack sections, and assign
     * values to stack symbols later */
    .stack_dummy (COPY):
    {
        *(.stack*)
    } > RAM

    _ram_start = ORIGIN(RAM);

    /* Set stack top to end of RAM, and stack limit move down by
     * size of stack_dummy section */
    __StackTop = ORIGIN(RAM) + LENGTH(RAM);
    __StackLimit = __StackTop - SIZEOF(.stack_dummy);
    PROVIDE(__stack = __StackTop);

    /* Top of head is the bottom of the stack */
    __HeapLimit = __StackLimit;

    /* Check if data + heap + stack exceeds RAM limit */
    ASSERT(__HeapBase <= __HeapLimit, "region RAM overflowed with stack")
}

