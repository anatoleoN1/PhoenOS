# PhoenOS

Système d'exploitation x86 écrit en C : léger, efficace, gratuit.
Version actuelle : **0.1.0** (noyau minimal qui démarre via GRUB et génère une ISO).

## Prérequis (Debian / Ubuntu, machine x86_64)

```bash
sudo apt install build-essential gcc-multilib grub-pc-bin grub-common xorriso mtools qemu-system-x86
```

> Si tu développes sur ARM (ex. Raspberry Pi 5), `gcc -m32` ne suffit pas :
> il faut un cross-compilateur `i686-elf-gcc`, et il faudra alors adapter `CC`/`LD` dans le Makefile.

## Utilisation

```bash
make          # compile le noyau -> build/phoenos.bin
make check    # vérifie que le noyau est un binaire Multiboot valide
make iso      # génère PhoenOS.iso (bootable)
make run      # lance l'ISO dans QEMU
make clean
```

Pour tester sur une vraie machine : écrire `PhoenOS.iso` sur une clé USB
(`sudo dd if=PhoenOS.iso of=/dev/sdX bs=4M status=progress`, en vérifiant bien `sdX`).

## Structure

| Fichier              | Rôle                                              |
|----------------------|---------------------------------------------------|
| `src/boot.S`         | En-tête Multiboot, pile, appel du noyau C         |
| `src/kernel.c`       | `kernel_main`, démarrage et messages              |
| `src/vga.c`          | Pilote texte VGA 80x25 (couleurs, défilement)     |
| `src/kprintf.c`      | Mini `printf` du noyau                            |
| `linker.ld`          | Placement du noyau en mémoire (1 Mio)             |
| `iso/boot/grub/`     | Configuration GRUB de l'ISO                       |

## Feuille de route

Chaque étape produit une ISO fonctionnelle.

1. [x] Boot Multiboot + affichage texte + `make iso`
2. [ ] GDT et IDT (gestion des exceptions)
3. [ ] PIC, timer (PIT) et interruptions
4. [ ] Clavier PS/2
5. [ ] Gestion de la mémoire physique puis paginée, `kmalloc`
6. [ ] Mini-shell
7. [ ] Système de fichiers (initrd, puis disque)
8. [ ] Mode graphique (framebuffer) et interface soignée
9. [ ] Passage en x86_64
