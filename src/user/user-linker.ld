ENTRY(_start)

SECTIONS {
    /* User program loaded at 0x400000 (4MB) */
    . = 0x00400000;

    .text ALIGN(4): {
        *crt0.o(.text)          /* Put entrypoint first */
        *(.text)                /* All other code */
    }

    .rodata ALIGN(4): {
        *(.rodata*)             /* Read-only data */
    }

    .data ALIGN(4): {
        *(.data)                /* Initialized data */
    }

    .bss ALIGN(4): {
        *(COMMON)               /* Common symbols */
        *(.bss)                 /* Uninitialized data */
    }

    _linker_user_program_end = .;
    
    /* Ensure user program doesn't exceed 1 MiB */
    ASSERT((_linker_user_program_end - 0x00400000) <= 1 * 1024 * 1024, 
           "Error: User program exceeds 1 MiB limit")
}
