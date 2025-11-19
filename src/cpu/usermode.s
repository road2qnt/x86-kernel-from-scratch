global enter_user_mode

enter_user_mode:
    ; [esp + 16] ss  (User Data Selector 0x23)
    ; [esp + 12] esp (User Stack Pointer 0x200000 + 4MB = 0x600000)
    ; [esp + 8 ] eflags (0x202 -> Interrupt Enable)
    ; [esp + 4 ] cs  (User Code Selector 0x1B)
    ; [esp + 0 ] eip (User Entry Point 0x400000)

    cli
    mov ax, 0x23    ; User Data Segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push dword 0x23       ; SS
    push dword 0xC00000   ; ESP (Pastikan ini valid)
    push dword 0x202      ; EFLAGS
    push dword 0x1B       ; CS
    push dword 0x400000   ; EIP (Lokasi program user nanti)
    
    iret