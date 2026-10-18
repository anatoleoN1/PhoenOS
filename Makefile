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
QEMU    := qemu-system-i386
DISK    := disk.img
DISKMB  := 32
# Option d'affichage de QEMU : vide = choix automatique. Essayez  make run QEMU_DISPLAY="-display sdl"  (ou gtk)
# si la fenetre s'affiche mais ne recoit pas le clavier (ex. Chromebook / Linux sous ChromeOS).
QEMU_DISPLAY :=
QEMU_DISK := -drive file=$(DISK),format=raw,if=ide,index=0,media=disk -nic user,model=e1000
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
VERSION := $(shell sed -n 's/.*PHOENOS_VERSION "\(.*\)".*/\1/p' src/version.h)
ISO     := phoenos-$(VERSION).iso

# Programmes utilisateur (user/*.c) : un ELF par programme, charge a 4 Mio, copie dans /bin de l'initrd.
USER_CFLAGS := -m32 -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
               -fno-builtin -fno-omit-frame-pointer -mno-sse -mno-mmx -nostdlib -static
USER_PROGS  := $(patsubst user/%.c,%,$(filter-out user/crt0.c,$(wildcard user/*.c)))
USER_ELFS   := $(addprefix build/user/,$(addsuffix .elf,$(USER_PROGS)))

# Bibliotheques partagees (lib/*.c, 0.8.0) : chacune liee a SA plage de la zone des bibliotheques (0x700000-0x7F0000),
# emballee en /lib/<nom>.psl par mkpsl. Les symboles exportes sont ceux qui commencent par <nom>_.
LIBS        := $(patsubst lib/%.c,%,$(wildcard lib/*.c))
LIB_BASE_maths := 0x700000
LIB_BASE_texte := 0x710000
LIB_ELFS    := $(addprefix build/lib/,$(addsuffix .elf,$(LIBS)))

build/lib/%.elf: lib/%.c user/psl.ld
	mkdir -p build/lib
	$(CC) $(USER_CFLAGS) -T user/psl.ld -Wl,--defsym=LIB_BASE=$(LIB_BASE_$*) -Wl,--build-id=none -Wl,-z,noexecstack $< -o $@
TESTS   := test-div0 test-gp test-panic test-assert test-doublefree test-heapcorrupt

.PHONY: all iso run run-sdl run-gtk serial debug debug-window debug-sdl debug-gtk logs logs-clean check symbol test clean newdisk $(TESTS)

all: $(KERNEL)

$(KERNEL): $(OBJ) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.S | build
	$(CC) $(ASFLAGS) -c $< -o $@

build:
	mkdir -p build

build/user/%.elf: user/%.c user/crt0.c user/phoenos.h user/user.ld
	mkdir -p build/user
	$(CC) $(USER_CFLAGS) -T user/user.ld -Wl,--build-id=none -Wl,-z,noexecstack $< user/crt0.c -o $@

iso: $(KERNEL) $(USER_ELFS) $(LIB_ELFS)
	rm -rf build/isodir
	mkdir -p build/isodir/boot/grub
	cp $(KERNEL) build/isodir/boot/phoenos.bin
	cp iso/boot/grub/grub.cfg build/isodir/boot/grub/grub.cfg
	rm -rf build/initrd
	cp -r initrd build/initrd
	mkdir -p build/initrd/bin
	for p in $(USER_PROGS); do cp build/user/$$p.elf build/initrd/bin/$$p; done
	mkdir -p build/initrd/apps
	sh tools/build-hosttool.sh build/mkpoe tools/mkpoe.c poe.c crc32.c
	for p in $(USER_PROGS); do build/mkpoe -n $$p -v $(VERSION) -a PhoenOS build/user/$$p.elf build/initrd/apps/$$p.poe >/dev/null; done
	sh tools/build-hosttool.sh build/mkpsl tools/mkpsl.c psl.c poe.c elf.c crc32.c
	mkdir -p build/initrd/lib
	for l in $(LIBS); do build/mkpsl -n $$l -v $(VERSION) -a PhoenOS -d "Bibliotheque d'exemple $$l" \
	    $$(nm build/lib/$$l.elf | sed -n "s/^[0-9a-f]* [TDRB] \($${l}_[A-Za-z0-9_]*\)$$/-e \1/p") build/lib/$$l.elf build/initrd/lib/$$l.psl >/dev/null; done
	build/mkpoe -n utilib -v $(VERSION) -a PhoenOS -d "Exemple : utilise maths et texte" -l maths -l texte \
	    build/user/utilib.elf build/initrd/apps/utilib.poe >/dev/null
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
	grub-file --is-x86-multiboot $(KERNEL) && echo "Multiboot OK"

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
