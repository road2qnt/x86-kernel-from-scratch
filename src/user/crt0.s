global _start
extern main

section .text
_start:
    call main
    
    mov eax, 0 
    int 0x80
.loop:
    jmp .loop