# Essai de PhoenOS 0.11.0 (édition perf64, 64 bits UEFI)

## Installer les outils (une fois, WSL / Debian / Ubuntu)
    sudo apt install qemu-system-x86 ovmf grub-efi-amd64-bin grub-pc-bin xorriso mtools

## 1. L'édition 32 bits n'a pas changé
    make run                    # ou make serial

## 2. L'édition 64 bits
    make EDITION=perf64 check          # doit afficher « Multiboot2 OK »
    make EDITION=perf64 run-uefi       # démarrage UEFI (firmware OVMF), fenêtre graphique
    make EDITION=perf64 serial-uefi    # sans fenêtre : tout dans le terminal
    make EDITION=perf64 run-bios       # même noyau, démarrage BIOS classique
Si OVMF n'est pas trouvé : `make EDITION=perf64 run-uefi OVMF=/chemin/OVMF_CODE.fd`.

## Ce qu'on doit voir
- l'écran de démarrage de PhoenOS en graphique (80x25 agrandi, centré) ; au prompt : `ver` (édition perf64), `mem`, `ls`, `net`, `maj etat` ;
- dans `mem` : la pagination est indiquée comme faite au démarrage ;
- lancer un programme (`hello`) répond « non implemente » : normal, c'est la 0.11.1.

## Si ça ne démarre pas
Envoyez : la dernière image de l'écran, `logs/latest/` (`make EDITION=perf64 debug` ne gère pas encore OVMF : utilisez `serial-uefi` et copiez le texte du terminal),
et dites si c'est UEFI ou BIOS, et sous quel QEMU/Windows.
Écran noir en UEFI mais journal série correct = problème de console graphique (essayez `run-bios`).
