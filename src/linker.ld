ENTRY(loader);

PHDRS {
    text_segment PT_LOAD;
    data_segment PT_LOAD;
}

SECTIONS {
    . = 0x00100000;

    .text ALIGN (0x1000) : {
        *(.multiboot)
        *(.text)
    } :text_segment

    .rodata ALIGN (0x1000) : {
        *(.rodata*)
    } :text_segment

    .data ALIGN (0x1000) : {
        *(.data)
    } :data_segment

    .bss ALIGN (0x1000) : {
        *(COMMON)
        *(.bss)
    } :data_segment
}