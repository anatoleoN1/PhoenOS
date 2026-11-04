# PhoenOS - Makefile
#
# Construction :
#   make              compile le noyau (build/phoenos.bin)
#   make check        vérifie que le noyau est un binaire Multiboot valide
#   make iso          génère l'image bootable phoenos-<édition>-<version>.iso
#   make EDITION=perf64 iso   édition 64 bits (UEFI ou BIOS) ; perf32 (défaut) : 32 bits, BIOS
#   make clean
#
# Exécution (QEMU) :
#   make run          lance l'ISO ; le journal du noyau s'affiche aussi dans le terminal
#   make serial       SANS fenêtre : tout se passe dans ce terminal (clavier compris). Quitter : Ctrl+A puis X.
#                     Utile sans affichage (WSL, Chromebook) ou si la fenêtre ne reçoit pas le clavier.
#   make debug        SANS fenetre, enregistre tout dans logs/<session>/ (os.log, console.log, qemu.log, build.log)
#                     variantes : debug-window, debug-sdl, debug-gtk ; make logs liste les sessions
#   make newdisk     recree un disque vide disk.img (celui qui garde vos fichiers entre deux lancements)
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
DISK    := disk.img
DISKMB  := 32
# Option d'affichage de QEMU : vide = choix automatique. Essayez  make run QEMU_DISPLAY="-display sdl"  (ou gtk)
# si la fenetre s'affiche mais ne recoit pas le clavier (ex. Chromebook / Linux sous ChromeOS).
QEMU_DISPLAY :=
QEMU_DISK := -drive file=$(DISK),format=raw,if=ide,index=0,media=disk -nic user,model=e1000
# Editions que ce code sait construire (publier.sh la lit) ; EDITION=... choisit laquelle.
#   perf32 : Performance 32 bits (BIOS, GRUB Multiboot 1)       perf64 : Performance 64 bits (BIOS ou UEFI, GRUB Multiboot2)
#   pui64  : Puissance 64 bits (pas encore)
EDITIONS_OK := perf32 perf64
VERSION := $(shell sed -n 's/.*PHOENOS_VERSION "\(.*\)".*/\1/p' src/version.h)
EDITION ?= perf32
ISO     := phoenos-$(EDITION)-$(VERSION).iso
ifeq ($(filter $(EDITION),$(EDITIONS_OK)),)
$(error edition "$(EDITION)" pas encore disponible : editions construisibles : $(EDITIONS_OK))
endif
OBJDIR  := build/obj-$(EDITION)
KERNEL  := build/phoenos.bin

# make PANIC_THEME=violet  -> écran d'erreur violet (sinon orange)
ifeq ($(PANIC_THEME),violet)
THEME_FLAG := -DPANIC_THEME_VIOLET
endif

CFLAGS_COMMON := -std=gnu11 -O2 -g -Wall -Wextra -Werror \
           -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
           -fno-builtin -fno-omit-frame-pointer -fno-tree-loop-distribute-patterns \
           -nostdlib -mno-sse -mno-mmx $(THEME_FLAG) $(EXTRA_CFLAGS)

ifeq ($(EDITION),perf32)
ARCH    := x86
CFLAGS  := -m32 $(CFLAGS_COMMON)
ASFLAGS := -m32
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib -z noexecstack
QEMU    := qemu-system-i386
GRUBCFG := iso/boot/grub/grub.cfg
SRC_C   := $(wildcard src/*.c)
SRC_S   := $(wildcard src/*.S)
else
# 64 bits : noyau ELF64 charge a 1 Mio par GRUB (Multiboot2), tout sous 4 Gio (voir docs/64BITS.md)
ARCH    := x64
CFLAGS  := -m64 -Isrc -mcmodel=small -mno-red-zone -DPHOENOS_ED_PERF64 -DPHOENOS_X64 \
           -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast $(CFLAGS_COMMON)
ASFLAGS := -m64 -Isrc -DPHOENOS_X64
LDFLAGS := -m elf_x86_64 -T linker64.ld -nostdlib -z noexecstack -z max-page-size=4096
QEMU    := qemu-system-x86_64
GRUBCFG := iso/boot/grub/grub-x64.cfg
# fichiers propres au 32 bits (remplaces par src/x64/ ou des versions reduites)
X86_ONLY := boot.S gdt.c gdt_flush.S idt.c isr_stubs.S switch.S paging.c paging_enable.c relaunch.S
SRC_C   := $(filter-out $(addprefix src/,$(X86_ONLY)),$(wildcard src/*.c)) $(wildcard src/x64/*.c)
SRC_S   := $(filter-out $(addprefix src/,$(X86_ONLY)),$(wildcard src/*.S)) $(wildcard src/x64/*.S)
endif

OBJ     := $(patsubst src/%.c,$(OBJDIR)/%.o,$(SRC_C)) \
           $(patsubst src/%.S,$(OBJDIR)/%.o,$(SRC_S))

# Programmes utilisateur (user/*.c) : un ELF par programme, charge a 4 Mio, copie dans /bin de l'initrd.
USER_CFLAGS := -m32 -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
               -fno-builtin -fno-omit-frame-pointer -mno-sse -mno-mmx -nostdlib -static
USER_PROGS  := $(patsubst user/%.c,%,$(filter-out user/crt0.c user/crt0_64.c,$(wildcard user/*.c)))
USER_ELFS   := $(addprefix build/user/,$(addsuffix .elf,$(USER_PROGS)))

# Bibliotheques partagees (lib/*.c, 0.8.0) : chacune liee a SA plage de la zone des bibliotheques (0x700000-0x7F0000),
# emballee en /lib/<nom>.psl par mkpsl. Les symboles exportes sont ceux qui commencent par <nom>_.
LIBS        := $(patsubst lib/%.c,%,$(wildcard lib/*.c))
LIB_BASE_maths := 0x700000
LIB_BASE_texte := 0x710000
LIB_ELFS    := $(addprefix build/lib/,$(addsuffix .elf,$(LIBS)))

# Edition 64 bits (0.11.3) : les memes programmes et bibliotheques sont aussi compiles en VRAIS programmes 64 bits (mode long,
# ring 3 avec segment de code 64 bits) ; ils vont dans /bin, /apps, /lib, et les versions 32 bits (mode de compatibilite)
# dans /bin32, /apps32, /lib32 (comme System32 / SysWOW64 de Windows). Sur perf32 : seulement les 32 bits, dans /bin, /apps, /lib.
USER64_CFLAGS := -m64 -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
               -fno-builtin -fno-omit-frame-pointer -mno-sse -mno-mmx -mcmodel=small -nostdlib -static -Wl,-z,max-page-size=4096
USER64_ELFS := $(addprefix build/user64/,$(addsuffix .elf,$(USER_PROGS)))
LIB64_ELFS  := $(addprefix build/lib64/,$(addsuffix .elf,$(LIBS)))

build/lib64/%.elf: lib/%.c user/psl.ld
	mkdir -p build/lib64
	$(CC) $(USER64_CFLAGS) -T user/psl.ld -Wl,--defsym=LIB_BASE=$(LIB_BASE_$*) -Wl,--build-id=none -Wl,-z,noexecstack $< -o $@

build/user64/%.elf: user/%.c user/crt0_64.c user/phoenos.h user/libc.h user/user.ld
	mkdir -p build/user64
	$(CC) $(USER64_CFLAGS) -T user/user.ld -Wl,--build-id=none -Wl,-z,noexecstack $< user/crt0_64.c -o $@

ifeq ($(ARCH),x64)
ISO_EXTRA := $(USER64_ELFS) $(LIB64_ELFS)
endif

# $(call PACK,dossier des programmes,dossier des bibliotheques,suffixe de dossier) : remplit bin<s>, apps<s>, lib<s> de l'initrd
define PACK
	mkdir -p build/initrd/bin$(3) build/initrd/apps$(3) build/initrd/lib$(3)
	for p in $(USER_PROGS); do cp $(1)/$$p.elf build/initrd/bin$(3)/$$p; done
	for p in $(USER_PROGS); do build/mkpoe -n $$p -v $(VERSION) -a PhoenOS $(1)/$$p.elf build/initrd/apps$(3)/$$p.poe >/dev/null; done
	for l in $(LIBS); do build/mkpsl -n $$l -v $(VERSION) -a PhoenOS -d "Bibliotheque d'exemple $$l" \
	    $$(nm $(2)/$$l.elf | sed -n "s/^[0-9a-f]* [TDRB] \($${l}_[A-Za-z0-9_]*\)$$/-e \1/p") $(2)/$$l.elf build/initrd/lib$(3)/$$l.psl >/dev/null; done
	build/mkpoe -n utilib -v $(VERSION) -a PhoenOS -d "Exemple : utilise maths et texte" -l maths -l texte \
	    $(1)/utilib.elf build/initrd/apps$(3)/utilib.poe >/dev/null
endef

build/lib/%.elf: lib/%.c user/psl.ld
	mkdir -p build/lib
	$(CC) $(USER_CFLAGS) -T user/psl.ld -Wl,--defsym=LIB_BASE=$(LIB_BASE_$*) -Wl,--build-id=none -Wl,-z,noexecstack $< -o $@
TESTS   := test-div0 test-gp test-panic test-assert test-doublefree test-heapcorrupt

.PHONY: all iso run-uefi serial-uefi run-bios run run-sdl run-gtk serial debug debug-window debug-sdl debug-gtk logs logs-clean check symbol test clean newdisk $(TESTS)

all: $(KERNEL)

$(KERNEL): $(OBJ) linker.ld linker64.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

$(OBJDIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: src/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

build/user/%.elf: user/%.c user/crt0.c user/phoenos.h user/user.ld
	mkdir -p build/user
	$(CC) $(USER_CFLAGS) -T user/user.ld -Wl,--build-id=none -Wl,-z,noexecstack $< user/crt0.c -o $@

iso: $(KERNEL) $(USER_ELFS) $(LIB_ELFS) $(ISO_EXTRA)
	rm -rf build/isodir
	mkdir -p build/isodir/boot/grub
	cp $(KERNEL) build/isodir/boot/phoenos.bin
	cp $(GRUBCFG) build/isodir/boot/grub/grub.cfg
	rm -rf build/initrd
	cp -r initrd build/initrd
	sh tools/build-hosttool.sh build/mkpoe tools/mkpoe.c poe.c crc32.c
	sh tools/build-hosttool.sh build/mkpsl tools/mkpsl.c psl.c poe.c elf.c crc32.c
ifeq ($(ARCH),x64)
	$(call PACK,build/user64,build/lib64,)
	$(call PACK,build/user,build/lib,32)
else
	$(call PACK,build/user,build/lib,)
endif
	tar --format=ustar --owner=0 --group=0 --numeric-owner -cf build/isodir/boot/initrd.tar -C build/initrd .
	grub-mkrescue -o $(ISO) build/isodir
	@echo ">> $(ISO) pret"

# -no-reboot : en cas de triple faute, QEMU s'arrête au lieu de redémarrer en boucle.
# Fenêtre graphique. Sur un système sans écran (SSH, certains Chromebook / WSL) la fenêtre ne peut pas
# s'ouvrir : on le détecte et on passe automatiquement en mode « serial » (tout dans le terminal).
QEMU_SERIAL := -display none -no-reboot -rtc base=localtime \
               -chardev stdio,id=con,mux=on,signal=off -serial chardev:con -mon chardev=con

run: iso $(DISK)
	@if [ -z "$$DISPLAY$$WAYLAND_DISPLAY" ]; then \
	    echo ">> Pas d'ecran graphique detecte : mode terminal (comme make serial). Ctrl+A puis X pour quitter."; \
	    $(QEMU) $(QEMU_DISK) -cdrom $(ISO) $(QEMU_SERIAL); \
	else \
	    $(QEMU) $(QEMU_DISK) $(QEMU_DISPLAY) -cdrom $(ISO) -serial stdio -no-reboot -rtc base=localtime; \
	fi

# Variantes de fenêtre si « make run » n'ouvre rien : essayez l'une puis l'autre.
run-sdl: iso $(DISK)
	$(QEMU) $(QEMU_DISK) -display sdl -cdrom $(ISO) -serial stdio -no-reboot -rtc base=localtime

run-gtk: iso $(DISK)
	$(QEMU) $(QEMU_DISK) -display gtk -cdrom $(ISO) -serial stdio -no-reboot -rtc base=localtime

serial: iso $(DISK)
	$(QEMU) $(QEMU_DISK) -cdrom $(ISO) $(QEMU_SERIAL)

# Edition 64 bits : demarrage UEFI (firmware OVMF) ou BIOS classique. Paquets : qemu-system-x86 ovmf grub-efi-amd64-bin mtools
# make EDITION=perf64 run-uefi | serial-uefi | run-bios    (OVMF=/chemin/OVMF_CODE.fd si le firmware n'est pas trouve)
OVMF ?= $(shell for f in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/ovmf/OVMF.fd /usr/share/qemu/OVMF.fd; do [ -s "$$f" ] && { echo $$f; break; }; done)
OVMF_VARS ?= $(shell for f in /usr/share/OVMF/OVMF_VARS_4M.fd /usr/share/OVMF/OVMF_VARS.fd; do [ -s "$$f" ] && { echo $$f; break; }; done)
# firmware "code seul" (OVMF_CODE*) : il faut le brancher en pflash avec une copie des variables ; un OVMF.fd complet passe par -bios
ifneq ($(findstring OVMF_CODE,$(OVMF)),)
ifneq ($(OVMF_VARS),)
QEMU_UEFI = -drive if=pflash,format=raw,readonly=on,file=$(OVMF) -drive if=pflash,format=raw,file=build/OVMF_VARS.fd
else
QEMU_UEFI = -drive if=pflash,format=raw,readonly=on,file=$(OVMF)
endif
else
QEMU_UEFI = -bios $(OVMF)
endif
OVMF_MSG = Firmware UEFI (OVMF) introuvable : sudo apt install ovmf   (ou make run-uefi OVMF=/chemin/OVMF.fd)
ovmf-vars:
	@mkdir -p build; [ -z "$(OVMF_VARS)" ] || [ -f build/OVMF_VARS.fd ] || cp "$(OVMF_VARS)" build/OVMF_VARS.fd

run-uefi: iso $(DISK) ovmf-vars
	@test -n "$(OVMF)" || { echo "$(OVMF_MSG)"; exit 1; }
	$(QEMU) -m 256 $(QEMU_UEFI) $(QEMU_DISK) $(QEMU_DISPLAY) -cdrom $(ISO) -serial stdio -no-reboot -rtc base=localtime

serial-uefi: iso $(DISK) ovmf-vars
	@test -n "$(OVMF)" || { echo "$(OVMF_MSG)"; exit 1; }
	$(QEMU) -m 256 $(QEMU_UEFI) $(QEMU_DISK) -cdrom $(ISO) $(QEMU_SERIAL)

run-bios: iso $(DISK)
	$(QEMU) -m 256 $(QEMU_DISK) $(QEMU_DISPLAY) -cdrom $(ISO) -serial stdio -no-reboot -rtc base=localtime

# Diagnostic (0.6.2) : tout est enregistre dans logs/<date-heure>/ (voir tools/debug-session.sh) : compilation,
# journal interne de PhoenOS (os.log), console serie, journal de QEMU. Rien de tout cela n'est affiche a l'ecran.
#   make debug          sans fenetre (comme serial)      make debug-window | debug-sdl | debug-gtk : avec fenetre
#   make debug QEMU_D=   desactive qemu.log (tres gros sur une longue session) ;  make logs : liste ;  make logs-clean
DEBUG_ENV := MAKE="$(MAKE)" QEMU="$(QEMU)" ISO="$(ISO)" DISK="$(DISK)" QEMU_DISK="$(QEMU_DISK)"
ifneq ($(origin QEMU_D),undefined)
DEBUG_ENV += QEMU_D="$(QEMU_D)"
endif

debug:
	@$(DEBUG_ENV) sh tools/debug-session.sh serial

debug-window:
	@$(DEBUG_ENV) sh tools/debug-session.sh window

debug-sdl:
	@$(DEBUG_ENV) sh tools/debug-session.sh sdl

debug-gtk:
	@$(DEBUG_ENV) sh tools/debug-session.sh gtk

logs:
	@ls -1dt logs/*/ 2>/dev/null || echo "Aucune session (lancez make debug)"
	@echo ">> derniere session : logs/latest  (ex. : less logs/latest/os.log)"

logs-clean:
	rm -rf logs

test:
	sh tests/run.sh

check: $(KERNEL)
ifeq ($(ARCH),x64)
	grub-file --is-x86-multiboot2 $(KERNEL) && echo "Multiboot2 OK"
else
	grub-file --is-x86-multiboot $(KERNEL) && echo "Multiboot OK"
endif

symbol: $(KERNEL)
	@test -n "$(ADDR)" || { echo "Usage : make symbol ADDR=0x100abc"; exit 1; }
	addr2line -f -p -e $(KERNEL) $(ADDR)

# Chaque test est recompilé à froid (le drapeau change le code), puis nettoyé.
$(TESTS): test-%:
	$(MAKE) clean
	-$(MAKE) run EXTRA_CFLAGS=-DTEST_$(shell echo $* | tr a-z A-Z)
	$(MAKE) clean

# Disque virtuel de PhoenOS (vide au départ) : conservé par `make clean` pour ne pas perdre vos fichiers.
$(DISK):
	truncate -s $(DISKMB)M $(DISK)
	@echo ">> $(DISK) cree ($(DISKMB) Mio) : PhoenOS le prepare tout seul (save)"

# Repart d'un disque vide (ATTENTION : efface tout ce que PhoenOS y avait enregistre).
newdisk:
	rm -f $(DISK)
	$(MAKE) $(DISK)

clean:
	rm -rf build phoenos-*.iso PhoenOS.iso qemu.log
