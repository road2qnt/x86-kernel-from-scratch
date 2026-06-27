# x86-kernel-from-scratch

> A 32-bit x86 operating system kernel built from scratch — bootloader, memory paging, EXT2 filesystem, preemptive multitasking, and user-mode process execution.

**4,000+ lines of C and x86 Assembly** | **Bare-metal, no external kernel code**

---

## Overview

`x86-kernel-from-scratch` is a from-scratch implementation of a 32-bit protected-mode operating system kernel targeting the x86 architecture. Designed as a monolith with modular subsystems, it covers the full boot-to-shell lifecycle: hardware initialization, memory management, process scheduling, filesystem operations, and user-mode execution.

The project deliberately avoids existing kernel code, Linux, or BSD derivatives — every line is purpose-built to understand OS internals at the hardware level.

## Architecture

```
Boot (Multiboot)
  └─ Protected Mode Setup (GDT, IDT, TSS)
       └─ Memory Management (Paging, Page Frame Allocator)
            └─ Device Drivers (Keyboard, Framebuffer, ATA PIO)
                 └─ Filesystem (EXT2 — block-level CRUD)
                      └─ Process Management (Round-Robin Scheduler, PCB)
                           └─ User Mode (Ring 3) — Shell & CLI
```

## Key Features

| Component | Implementation |
|-----------|---------------|
| **Boot** | Multiboot-compliant, 32-bit Protected Mode, GDT with code/data segments |
| **Interrupts** | IDT setup, IRQ handling, PIT for timer ticks, keyboard ISR |
| **Memory** | Higher-half kernel mapping, paging (4KB pages), page frame allocator bitmap |
| **Drivers** | PS/2 keyboard driver, 80×25 VGA text framebuffer, ATA PIO (LBA28) disk driver |
| **Filesystem** | EXT2 implementation: superblock, block group descriptors, inode traversal, directory entry navigation, read/write/create/delete |
| **Scheduler** | Preemptive Round-Robin with fixed time slices, Process Control Blocks (PCB), process states |
| **User Mode** | Ring 3 transition via TSS, syscall interface (`int 0x80`), separate user stacks |
| **Shell** | Interactive CLI with `ls`, `cd`, `mkdir`, `cp`, `rm`, `ps`, `kill`, `cat`, `clear` |

## Build & Run

**Requirements:** Linux, `gcc`, `nasm`, `ld`, `qemu-system-i386`, `make`, `genisoimage`

```bash
make clean  && make
make run    # launches in QEMU
```

## Project Structure

```
src/
├── cpu/          # GDT, IDT, interrupts, port I/O
├── driver/       # Keyboard, framebuffer, disk (ATA PIO)
├── filesystem/   # EXT2 implementation
├── memory/       # Paging, page frame allocator
├── process/      # PCB, scheduling
├── stdlib/       # String utilities, memory ops
├── user/         # Shell, crt0, user-mode entry
└── kernel.c      # Kernel entry point
```

## Why This Matters

Building a kernel from scratch requires deep understanding of:
- Hardware-software interface (x86 instruction set, interrupt controller, DMA)
- Memory hierarchy and protection mechanisms (rings, paging, segmentation)
- Concurrency primitives and scheduling theory
- Block-level filesystem design and disk I/O

These are the same fundamentals that underpin backend infrastructure at scale — from OS-level virtualization to storage engines.
