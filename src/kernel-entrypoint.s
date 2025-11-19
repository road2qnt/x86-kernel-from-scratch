global loader
global loader_physical
global load_gdt              ; <--- KITA TAMBAHIN INI LAGI

extern kernel_setup

; Konstanta
KERNEL_VIRTUAL_BASE equ 0xC0000000
KERNEL_STACK_SIZE   equ 2097152  ; 2MB Stack
MAGIC_NUMBER        equ 0x1BADB002
FLAGS               equ 0x0
CHECKSUM            equ -MAGIC_NUMBER

; -----------------------------------------------------------------------------
; SEKSI MULTIBOOT
; -----------------------------------------------------------------------------
section .multiboot
align 4
    dd MAGIC_NUMBER
    dd FLAGS
    dd CHECKSUM

; -----------------------------------------------------------------------------
; SEKSI DATA PAGING (HARDCODED PAGE TABLE)
; -----------------------------------------------------------------------------
section .setup.data
align 4096
initial_page_dir:
    dd 0x00000083 ; Entry 0: Identity Map 4MB
    times (768 - 1) dd 0 
    dd 0x00000083 ; Entry 768: Higher Half Map 4MB
    times (1024 - 768 - 1) dd 0

; -----------------------------------------------------------------------------
; SEKSI SETUP CODE (Fisik)
; -----------------------------------------------------------------------------
section .setup.text
loader_physical equ (loader_entrypoint - KERNEL_VIRTUAL_BASE)

loader_entrypoint:
    ; 1. Load Page Directory
    mov eax, (initial_page_dir - KERNEL_VIRTUAL_BASE)
    mov cr3, eax

    ; 2. Enable PSE
    mov eax, cr4
    or  eax, 0x00000010
    mov cr4, eax

    ; 3. Enable Paging
    mov eax, cr0
    or  eax, 0x80000000
    mov cr0, eax

    ; 4. Jump to Higher Half
    lea eax, [loader_virtual]
    jmp eax

; -----------------------------------------------------------------------------
; SEKSI KERNEL TEXT (Virtual)
; -----------------------------------------------------------------------------
section .text
loader_virtual:
    mov esp, kernel_stack + KERNEL_STACK_SIZE
    call kernel_setup
.loop:
    jmp .loop

; --- FUNGSI LOAD GDT (INI YANG HILANG TADI) ---
load_gdt:
    mov eax, [esp+4] ; Ambil argumen pointer GDTR dari stack
    lgdt [eax]       ; Load GDT
    ret
; ----------------------------------------------

; -----------------------------------------------------------------------------
; SEKSI BSS (Stack)
; -----------------------------------------------------------------------------
section .bss
align 4
kernel_stack:
    resb KERNEL_STACK_SIZE