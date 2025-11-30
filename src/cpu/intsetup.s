extern main_interrupt_handler
global isr_stub_table

call_generic_handler:
    ; 1. Push General & Segment Registers
    pushad
    push ds
    push es
    push fs
    push gs

    ; 2. Prepare arguments for C function: main_interrupt_handler(struct CPURegister *regs, uint32_t int_number)
    mov eax, esp        ; Arg1: Pointer to the saved register state
    mov ebx, [esp+48]   ; Arg2: Get int_number from the stack (pushed by macro). 4*4 gs-ds + 8*4 pushad = 48 bytes offset
    push ebx            ; Push Arg2
    push eax            ; Push Arg1
    
    ; 3. Set segment registers for C kernel execution
    push edx  ; Save edx
    mov dx, 0x10
    mov ds, dx
    mov es, dx
    pop edx   ; Restore edx

    ; 4. Call the C function
    call main_interrupt_handler

    ; 5. Cleanup arguments
    add esp, 8

    ; 6. Restore segment registers
    pop gs
    pop fs
    pop es
    pop ds

    ; 7. Restore general-purpose registers
    popad

    ; 8. Pop error code and interrupt number
    add esp, 8

    ; 9. Return from interrupt
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