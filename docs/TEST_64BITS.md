# Essai de PhoenOS 0.11.2 (édition perf64, 64 bits UEFI)

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
- l'écran de démarrage de PhoenOS en graphique (80x25 agrandi, centré) ; au prompt : `version` (édition perf64), `mem`, `ls`, `net`, `maj etat` ;
- dans `mem` : la pagination est indiquée comme faite au démarrage ;
- les programmes marchent comme en 32 bits : `run hello`, `run primes`, `exe utilib` (bibliothèques), `run lance`, `run web` ; un programme qui plante est arrêté sans arrêter le système ;

## Si ça ne démarre pas
Envoyez : la dernière image de l'écran, `logs/latest/` (`make EDITION=perf64 debug` ne gère pas encore OVMF : utilisez `serial-uefi` et copiez le texte du terminal),
et dites si c'est UEFI ou BIOS, et sous quel QEMU/Windows.
Écran noir en UEFI mais journal série correct = problème de console graphique (essayez `run-bios`).

## Mises à jour sous perf64 (même procédure que `docs/TEST_MAJ.md`, avec l'édition perf64)
1. `tools/phoenos_sign.py release --edition perf64 ...` (ou `publier.sh --edition perf64`) pour publier ; `maj verifier`, `maj installer`, `reboot` dans PhoenOS ;
2. au redémarrage, la ligne « lancement de la version … » doit apparaître puis l'écran graphique revient (la relance repasse du mode 64 bits au mode 32 bits puis relance le noyau) ;
3. si l'écran reste noir ou si ça redémarre en boucle : envoyez le journal série (`serial-uefi`) ; `maj retour` depuis le CD ramène à la version du CD.
