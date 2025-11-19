extern main_interrupt_handler
global isr_stub_table

call_generic_handler:
    ; 1. Push General Registers (PUSHAD)
    pushad
    
    ; 2. Push Segment Registers (Sesuai order C: DS, ES, FS, GS)
    push ds
    push es
    push fs
    push gs

    ; 3. Set segment registers to kernel_code (0x10)
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    pop eax

    ; 4. Call the C function
    call main_interrupt_handler

    ; 5. Restore segment registers (Pop order reversed from push)
    pop gs
    pop fs
    pop es
    pop ds

    ; 6. Restore general-purpose & index register
    popad

    ; 7. Restore the esp (interrupt number & error code)
    add esp, 8

    ; 8. Return
    sti
    iret

; Macro and handler definitions
%macro no_error_code_interrupt_handler 1
interrupt_handler_%1:
    push    dword 0
    push    dword %1
    jmp     call_generic_handler
%endmacro

%macro error_code_interrupt_handler 1
interrupt_handler_%1:
    push    dword %1
    jmp     call_generic_handler
%endmacro

; GENERATE SEMUA 256 HANDLER
%assign i 0 
%rep    256
    ; Exception / Interrupts dengan error code
    %if i == 8 || (i >= 10 && i <= 14) || i == 17 || i == 30
        error_code_interrupt_handler i
    %else
        no_error_code_interrupt_handler i
    %endif
%assign i i+1 
%endrep

; ISR stub table, wajib 256 entry
isr_stub_table:
    %assign i 0 
    %rep    256
    dd interrupt_handler_%+i
    %assign i i+1 
    %endrep