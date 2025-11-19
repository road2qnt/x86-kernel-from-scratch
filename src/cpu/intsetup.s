
call_generic_handler:

    push gs
    push fs
    push es
    push ds


    pushad

    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    pop eax

    call main_interrupt_handler

    popad

    pop ds
    pop es
    pop fs
    pop gs

    add esp, 8

    sti
    iret