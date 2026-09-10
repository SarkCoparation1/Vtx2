ENTRY(kernel_main)

SECTIONS
{
    . = 0x00100000;

    .text : ALIGN(4K)
    {
        *(.text.head)   /* kernel_main her zaman en başta olacak */
        *(.text*)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata*)
    }

    .data : ALIGN(4K)
    {
        *(.data*)
    }

    .bss : ALIGN(4K)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }
}
