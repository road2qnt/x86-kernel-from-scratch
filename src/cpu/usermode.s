global enter_user_mode

; enter_user_mode - Jump from Ring 0 to Ring 3 (User Mode)
; Uses IRET to switch privilege level
;
; Stack layout for IRET (Ring 0 -> Ring 3):
;   [ESP+16] SS   - User Data Segment Selector (0x20 | 3 = 0x23)
;   [ESP+12] ESP  - User Stack Pointer
;   [ESP+8]  EFLAGS - Flags (0x202 = IF enabled)
;   [ESP+4]  CS   - User Code Segment Selector (0x18 | 3 = 0x1B)
;   [ESP+0]  EIP  - User Entry Point (0x400000)

section .text
enter_user_mode:
    ; Disable interrupts during transition
    cli
    
    ; Set data segments to User Data Selector (0x23 = 0x20 | RPL 3)
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Build IRET frame on stack
    push dword 0x23         ; SS  - User Stack Segment
    push dword 0xBFFFFC     ; ESP - User Stack (top of 0x800000 page, stack grows down)
    push dword 0x202        ; EFLAGS - Interrupt Enable Flag set
    push dword 0x1B         ; CS  - User Code Segment (0x18 | RPL 3)
    push dword 0x400000     ; EIP - User program entry point
    
    ; Jump to user mode!
    iret
