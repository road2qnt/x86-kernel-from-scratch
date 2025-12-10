# Compiler & linker
ASM           = nasm
LIN           = ld
CC            = gcc

# Directory
SOURCE_FOLDER = src
OUTPUT_FOLDER = bin
ISO_NAME      = OS2025
DISK_NAME     = storage
USER_DIR 	  = src/user


# Flags
WARNING_CFLAG = -Wall -Wextra -Werror
DEBUG_CFLAG   = -fshort-wchar -g
STRIP_CFLAG   = -nostdlib -fno-stack-protector -nostartfiles -nodefaultlibs -ffreestanding
CFLAGS        = $(DEBUG_CFLAG) $(WARNING_CFLAG) $(STRIP_CFLAG) -m32 -c -I$(SOURCE_FOLDER)
AFLAGS        = -f elf32 -g -F dwarf
LFLAGS        = -T $(SOURCE_FOLDER)/linker.ld -melf_i386

# Run with rebuild (first time / after code changes)
run: all
	@qemu-system-i386 -drive file=$(OUTPUT_FOLDER)/storage.bin,format=raw,if=ide,index=0,media=disk -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

# Run WITHOUT rebuild (keeps your data!)
run-only:
	@qemu-system-i386 -drive file=$(OUTPUT_FOLDER)/storage.bin,format=raw,if=ide,index=0,media=disk -cdrom $(OUTPUT_FOLDER)/$(ISO_NAME).iso

all: build insert-shell 
build: iso

clean:
	@echo Cleaning up...
	@rm -rf $(OUTPUT_FOLDER)/*

# Kernel object files 
KERNEL_OBJS = \
	$(OUTPUT_FOLDER)/kernel-entrypoint.o \
	$(OUTPUT_FOLDER)/intsetup.o \
	$(OUTPUT_FOLDER)/usermode.o \
	$(OUTPUT_FOLDER)/kernel.o \
	$(OUTPUT_FOLDER)/gdt.o \
	$(OUTPUT_FOLDER)/interrupt.o \
	$(OUTPUT_FOLDER)/idt.o \
	$(OUTPUT_FOLDER)/framebuffer.o \
	$(OUTPUT_FOLDER)/portio.o \
	$(OUTPUT_FOLDER)/string.o \
	$(OUTPUT_FOLDER)/keyboard.o \
	$(OUTPUT_FOLDER)/disk.o \
	$(OUTPUT_FOLDER)/ext2.o \
	$(OUTPUT_FOLDER)/paging.o

kernel:
	@echo Assembling files...
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/kernel-entrypoint.s -o $(OUTPUT_FOLDER)/kernel-entrypoint.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/cpu/intsetup.s -o $(OUTPUT_FOLDER)/intsetup.o
	@$(ASM) $(AFLAGS) $(SOURCE_FOLDER)/cpu/usermode.s -o $(OUTPUT_FOLDER)/usermode.o	

	@echo Compiling C files...
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/kernel.c -o $(OUTPUT_FOLDER)/kernel.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/gdt.c -o $(OUTPUT_FOLDER)/gdt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/interrupt.c -o $(OUTPUT_FOLDER)/interrupt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/idt.c -o $(OUTPUT_FOLDER)/idt.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/driver/framebuffer.c -o $(OUTPUT_FOLDER)/framebuffer.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/cpu/portio.c -o $(OUTPUT_FOLDER)/portio.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/stdlib/string.c -o $(OUTPUT_FOLDER)/string.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/driver/keyboard.c -o $(OUTPUT_FOLDER)/keyboard.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/driver/disk.c -o $(OUTPUT_FOLDER)/disk.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/filesystem/ext2.c -o $(OUTPUT_FOLDER)/ext2.o
	@$(CC) $(CFLAGS) $(SOURCE_FOLDER)/memory/paging.c -o $(OUTPUT_FOLDER)/paging.o
	@echo Linking object files...
	@$(LIN) $(LFLAGS) $(KERNEL_OBJS) -o $(OUTPUT_FOLDER)/kernel

iso: kernel
	@echo Creating ISO image...
	@mkdir -p $(OUTPUT_FOLDER)/iso/boot/grub
	@cp $(OUTPUT_FOLDER)/kernel     $(OUTPUT_FOLDER)/iso/boot/
	@cp other/grub1                 $(OUTPUT_FOLDER)/iso/boot/grub/
	@cp $(SOURCE_FOLDER)/menu.lst   $(OUTPUT_FOLDER)/iso/boot/grub/
	@genisoimage -R -b boot/grub/grub1 -no-emul-boot -boot-load-size 4 -A os -input-charset utf8 -quiet -boot-info-table -o $(OUTPUT_FOLDER)/$(ISO_NAME).iso $(OUTPUT_FOLDER)/iso
	@rm -r $(OUTPUT_FOLDER)/iso/


disk:
	@if [ ! -f $(OUTPUT_FOLDER)/$(DISK_NAME).bin ]; then \
		echo "Creating new disk image..."; \
		qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 4M; \
	else \
		echo "Using existing disk image..."; \
	fi

user-shell:
	@$(ASM) $(AFLAGS) $(USER_DIR)/crt0.s -o $(OUTPUT_FOLDER)/crt0.o
	@$(CC)  $(CFLAGS) -fno-pic -fno-pie $(USER_DIR)/shell.c -o $(OUTPUT_FOLDER)/shell.o
	@$(LIN) -T $(USER_DIR)/user-linker.ld -melf_i386 --oformat binary \
		$(OUTPUT_FOLDER)/crt0.o $(OUTPUT_FOLDER)/shell.o -o $(OUTPUT_FOLDER)/shell
	@echo "User Shell Compiled!"

inserter:
	@$(CC) -Wno-builtin-declaration-mismatch -g \
		-I$(SOURCE_FOLDER) \
		-D FS_INSERTER \
		$(SOURCE_FOLDER)/filesystem/ext2.c \
		$(SOURCE_FOLDER)/external/external-inserter.c \
		-o $(OUTPUT_FOLDER)/inserter

insert-shell: disk inserter user-shell
	@echo "Inserting shell into storage..."
	@cd $(OUTPUT_FOLDER) && ./inserter shell shell 2 $(DISK_NAME).bin

# Force recreate disk (use when you want fresh storage)
reset-disk:
	@echo "Removing old disk image..."
	@rm -f $(OUTPUT_FOLDER)/$(DISK_NAME).bin
	@echo "Creating new disk image..."
	@qemu-img create -f raw $(OUTPUT_FOLDER)/$(DISK_NAME).bin 4M