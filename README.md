# Rendang OS - IF2130 Sistem Operasi 2025/2026

![Mascot Rendang OS](link_gambar_mascot_anime_rendang.jpg)

> *"Sistem Operasi dengan Cita Rasa Bumbu Rempah Teknologi"*

## Pendahuluan

**Rendang OS** adalah sistem operasi 32-bit *freestanding* di arsitektur x86 berbasis *protected mode*. Proyek ini dibuat dari nol (*bare metal*) sebagai Tugas Besar IF2130 Sistem Operasi – Institut Teknologi Bandung.

Kernel dirancang modular untuk mendukung manajemen memori, interrupt, EXT2 file system, hingga multitasking dengan *preemptive scheduler*.

---

## Daftar Isi

1. [Prerequisites & Toolchain](#prerequisites--toolchain)  
2. [Cara Kompilasi & Menjalankan](#cara-kompilasi--menjalankan)  
3. [Arsitektur & Fitur](#arsitektur--fitur)  
4. [Maskot Kelompok](#maskot-kelompok)  
5. [Anggota Kelompok](#anggota-kelompok)

---

## Prerequisites & Toolchain

Pastikan lingkungan Linux/WSL2 memiliki tools berikut:

- **Compiler:** `gcc`  
- **Assembler:** `nasm`  
- **Linker:** `ld`  
- **Emulator:** `qemu-system-i386`  
- **Build Tool:** `make`  
- **ISO Tool:** `genisoimage`  
- **Debugger:** `gdb`  

**Instalasi pada Ubuntu/WSL2:**

```bash
sudo apt update
sudo apt install -y nasm gcc qemu-system-x86 make genisoimage gdb
```

---

## Cara Kompilasi & Menjalankan

### 1. Build Kernel & ISO

```bash
make build
# atau
make iso
```

### 2. Menjalankan OS (QEMU)

```bash
make run
```

### 3. Debugging Kernel

```bash
make run-debug
```

### 4. Membersihkan Build

```bash
make clean
```

---

## Arsitektur & Fitur

### 1. Kernel & Booting (Chapter 0)

- **Multiboot compliant (GRUB Legacy)**
- **Protected Mode 32-bit**
- **GDT (Global Descriptor Table)**

### 2. Interrupt & Driver (Chapter 1)

- **IDT (Interrupt Descriptor Table)**
- **PIC Remapping**
- **Keyboard Driver (IRQ1)**
- **Framebuffer Driver (80x25 text mode)**

### 3. File System (Chapter 2)

- **Driver ATA PIO (LBA)**
- **EXT2 – versi disederhanakan untuk IF2130**
- **Operasi CRUD lengkap (file & directory)**

### 4. Memory Management & Paging (Chapter 3)

- **Paging**
- **Higher Half Kernel (`0xC0000000` ke atas)**
- **Page Frame Allocator**

### 5. Multitasking & User Mode (Chapter 3 & 4)

- **User Mode (Ring 3)**
- **PCB & Process Management**
- **Preemptive Round Robin Scheduler**
- **System Call `int 0x30`**

### 6. Shell (CLI)

Perintah bawaan:

- `cd`, `ls`, `cat`, `mkdir`, `cp`, `rm`
- `ps`, `kill`
- Eksekusi program via syscall `exec`

---

## Maskot Kelompok

**“Koki Rendang ( Kamisato Ayaka )”**

Maskot berupa koki anime yang menikmati rendang — melambangkan filosofi pembuatan OS: diracik dengan rempah kode yang kompleks, dimasak lama (debugging), hingga menghasilkan kernel yang lezat dan tahan lama.

*(Letakkan file gambar maskot di root repository dan sesuaikan link gambar di atas.)*

---

## Anggota Kelompok

| NIM | Nama |
| :---: | :--- |
| 13524115 | Ega Luthfi Rais |
| 13524120 | Jonathan Alverado |
| 13524126 | Ramadhian Nabil |
| 13524146 | Leonardus Brain |

---

*IF2130 Sistem Operasi – Teknik Informatika ITB*
