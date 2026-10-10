# PhoenOS - Makefile
#
# Construction :
#   make              compile le noyau (build/phoenos.bin)
#   make check        vérifie que le noyau est un binaire Multiboot valide
#   make iso          génère l'image bootable PhoenOS.iso
#   make clean
#
# Exécution (QEMU) :
#   make run          lance l'ISO ; le journal du noyau s'affiche aussi dans le terminal
#   make serial       idem sans fenêtre graphique (utile sans affichage, ex. WSL Windows 10)
#   make debug        comme run + journal des exceptions CPU dans qemu.log
#
# Diagnostic :
#   make test         tests automatiques (mémoire, shell, clavier AZERTY) sans démarrer l'OS
#   make symbol ADDR=0x100abc   traduit une adresse de la trace d'appel en fonction/ligne
#   make test-div0 | test-gp | test-panic | test-assert | test-doublefree | test-heapcorrupt
#                                déclenche volontairement une erreur pour voir l'écran d'erreur
#
# Thème de l'écran d'erreur : orange par défaut ; violet avec
#   make run PANIC_THEME=violet

CC      := gcc
LD      := ld
QEMU    := qemu-system-i386
CFLAGS  := -m32 -std=gnu11 -O2 -g -Wall -Wextra -Werror \
           -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
           -fno-builtin -fno-omit-frame-pointer -fno-tree-loop-distribute-patterns \
           -nostdlib -mno-sse -mno-mmx $(THEME_FLAG) $(EXTRA_CFLAGS)
ASFLAGS := -m32

# make PANIC_THEME=violet  -> écran d'erreur violet (sinon orange)
ifeq ($(PANIC_THEME),violet)
THEME_FLAG := -DPANIC_THEME_VIOLET
endif
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib -z noexecstack

SRC_C   := $(wildcard src/*.c)
SRC_S   := $(wildcard src/*.S)
OBJ     := $(patsubst src/%.c,build/%.o,$(SRC_C)) \
           $(patsubst src/%.S,build/%.o,$(SRC_S))

KERNEL  := build/phoenos.bin
ISO     := phoenos-0.0.6.iso
TESTS   := test-div0 test-gp test-panic test-assert test-doublefree test-heapcorrupt

.PHONY: all iso run serial debug check symbol test clean $(TESTS)

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
	tar --format=ustar --owner=0 --group=0 --numeric-owner -cf build/isodir/boot/initrd.tar -C initrd .
	grub-mkrescue -o $(ISO) build/isodir
	@echo ">> $(ISO) pret"

# -no-reboot : en cas de triple faute, QEMU s'arrête au lieu de redémarrer en boucle.
run: iso
	$(QEMU) -cdrom $(ISO) -serial stdio -no-reboot

serial: iso
	$(QEMU) -cdrom $(ISO) -display none -serial stdio -no-reboot

debug: iso
	$(QEMU) -cdrom $(ISO) -serial stdio -no-reboot -d int,cpu_reset -D qemu.log
	@echo ">> Journal QEMU : qemu.log (chercher : grep -n 'v=0\|Triple' qemu.log)"

test:
	sh tests/run.sh

check: $(KERNEL)
	grub-file --is-x86-multiboot $(KERNEL) && echo "Multiboot OK"

symbol: $(KERNEL)
	@test -n "$(ADDR)" || { echo "Usage : make symbol ADDR=0x100abc"; exit 1; }
	addr2line -f -p -e $(KERNEL) $(ADDR)

# Chaque test est recompilé à froid (le drapeau change le code), puis nettoyé.
$(TESTS): test-%:
	$(MAKE) clean
	-$(MAKE) run EXTRA_CFLAGS=-DTEST_$(shell echo $* | tr a-z A-Z)
	$(MAKE) clean

clean:
	rm -rf build $(ISO) qemu.log
