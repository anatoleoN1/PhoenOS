# PhoenOS - Makefile
# Cibles : make        -> compile le noyau (build/phoenos.bin)
#          make iso    -> génère l'image bootable PhoenOS.iso
#          make run    -> lance l'ISO dans QEMU
#          make check  -> vérifie que le noyau est un binaire Multiboot valide
#          make clean

CC      := gcc
LD      := ld
CFLAGS  := -m32 -std=gnu11 -O2 -Wall -Wextra -Werror \
           -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
           -fno-builtin -nostdlib -mno-sse -mno-mmx
ASFLAGS := -m32
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib -z noexecstack

SRC_C   := $(wildcard src/*.c)
SRC_S   := $(wildcard src/*.S)
OBJ     := $(patsubst src/%.c,build/%.o,$(SRC_C)) \
           $(patsubst src/%.S,build/%.o,$(SRC_S))

KERNEL  := build/phoenos.bin
ISO     := phoenos-0.0.1.iso

.PHONY: all iso run check clean

all: $(KERNEL)

$(KERNEL): $(OBJ) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.S | build
	$(CC) $(ASFLAGS) -c $< -o $@

build:
	mkdir -p build

iso: $(KERNEL)
	rm -rf build/isodir
	mkdir -p build/isodir/boot/grub
	cp $(KERNEL) build/isodir/boot/phoenos.bin
	cp iso/boot/grub/grub.cfg build/isodir/boot/grub/grub.cfg
	grub-mkrescue -o $(ISO) build/isodir
	@echo ">> $(ISO) pret"

run: iso
	qemu-system-i386 -cdrom $(ISO)

check: $(KERNEL)
	grub-file --is-x86-multiboot $(KERNEL) && echo "Multiboot OK"

clean:
	rm -rf build $(ISO)
