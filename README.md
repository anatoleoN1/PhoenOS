# PhoenOS

Système d'exploitation x86 écrit en C : léger, efficace, gratuit.
Version actuelle : **0.2.0** (GDT, IDT, exceptions, interruptions, timer).

## Prérequis (Debian / Ubuntu, machine x86_64 ou WSL sous Windows)

```bash
sudo apt install build-essential gcc-multilib grub-pc-bin grub-common xorriso mtools qemu-system-x86
```

> Sur ARM (ex. Raspberry Pi 5), `gcc -m32` ne suffit pas :
> il faut un cross-compilateur `i686-elf-gcc`, et adapter `CC`/`LD` dans le Makefile.

## Utilisation

```bash
make              # compile le noyau -> build/phoenos.bin
make check        # vérifie que le noyau est un binaire Multiboot valide
make iso          # génère PhoenOS.iso (bootable)
make run          # lance l'ISO dans QEMU
make test-fault   # démo de l'écran de panique (division par zéro volontaire)
make clean
```

Pour tester sur une vraie machine : écrire `PhoenOS.iso` sur une clé USB
(`sudo dd if=PhoenOS.iso of=/dev/sdX bs=4M status=progress`, en vérifiant bien `sdX`).

## Ce qu'on doit voir au démarrage (v0.2.0)

Des lignes `[ OK ]` pour : VGA, Multiboot, mémoire, GDT, IDT/PIC/timer,
exceptions (point d'arrêt traité), puis un compteur « 1s 2s 3s » et « Timer PIT fonctionnel ».
Avec `make test-fault`, un écran rouge de panique affiche l'exception #0 et les registres.

## Structure

| Fichier              | Rôle                                                    |
|----------------------|---------------------------------------------------------|
| `src/boot.S`         | En-tête Multiboot, pile, appel du noyau C               |
| `src/kernel.c`       | `kernel_main`, séquence de démarrage et auto-tests      |
| `src/vga.c`          | Pilote texte VGA 80x25 (couleurs, défilement)           |
| `src/kprintf.c`      | Mini `printf` du noyau                                  |
| `src/gdt.c`, `gdt_flush.S` | Table des segments (noyau + utilisateur)          |
| `src/idt.c`          | Table des interruptions (vecteurs 0-47)                 |
| `src/isr_stubs.S`    | Stubs d'entrée des interruptions + point d'entrée commun|
| `src/isr.c`          | Exceptions CPU (écran de panique) et distribution IRQ   |
| `src/pic.c`          | Contrôleur 8259 (remap, masque, EOI, fausses IRQ)       |
| `src/pit.c`          | Timer 100 Hz, `pit_ticks`, `pit_sleep_ms`               |
| `src/io.h`           | `inb`/`outb`, `cli`/`sti`/`hlt`                         |
| `linker.ld`          | Placement du noyau en mémoire (1 Mio)                   |
| `iso/boot/grub/`     | Configuration GRUB de l'ISO                             |

## Feuille de route

Chaque étape produit une ISO fonctionnelle.

1. [x] Boot Multiboot + affichage texte + `make iso`
2. [x] GDT et IDT (gestion des exceptions)
3. [x] PIC, timer (PIT) et interruptions
4. [ ] Clavier PS/2
5. [ ] Gestion de la mémoire physique puis paginée, `kmalloc`
6. [ ] Mini-shell
7. [ ] Système de fichiers (initrd, puis disque)
8. [ ] Mode graphique (framebuffer) et interface soignée
9. [ ] Passage en x86_64
