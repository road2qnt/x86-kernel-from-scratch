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
global kernel_execute_user_program ; execute initial user program from kernel
kernel_execute_user_program:
    mov eax, 0x20 | 0x3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Using iret (return instruction for interrupt) technique for privilege change
    ; Stack values will be loaded into these register:
    ; [esp] -> eip, [esp+4] -> cs, [esp+8] -> eflags, [] -> user esp, [] -> user ss
    mov ecx, [esp+4] ; Save first (before pushing anything to stack) for last push
    push eax ; Stack segment selector (GDT_USER_DATA_SELECTOR), user privilege
    mov eax, ecx
    add eax, 0x400000 - 4
    push eax ; User space stack pointer (esp), move it into last 4 MiB
    pushf ; eflags register state, when jump inside user program
    mov eax, 0x18 | 0x3
    push eax ; Code segment selector (GDT_USER_CODE_SELECTOR), user privilege
    mov eax, ecx
    push eax ; eip register to jump back
    iret
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