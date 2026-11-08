# Essai de PhoenOS 0.12.0 (édition perf64, 64 bits UEFI)

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
- (0.11.3) `run hello` lance maintenant la version **64 bits** (vrai mode long en ring 3) et `run /bin32/hello` la version 32 bits : les deux doivent afficher la même chose, avec « arguments : N » identique ;

## 0.11.3 : programmes 64 bits et 32 bits (à essayer dans l'ordre ; la 1re étape est la plus risquée)
    run hello a "b c"            # 64 bits (/bin/hello) : doit afficher 3 arguments
    run /bin32/hello a "b c"     # 32 bits : même affichage
    ls /bin /bin32 /apps /apps32 /lib /lib32
    psl                          # maths et texte en 64 bits, puis [32 bits] pour /lib32
    run primes 100 ; run /bin32/primes 100 ; run tas 1000 ; run /bin32/tas 1000
    exe utilib                   # 64 bits : maths et texte de /lib ;   exe /apps32/utilib : celles de /lib32
    run lance hello ; run essaim 3 ; run faute   # spawn avec argv 64 bits ; un 64 bits qui plante doit etre arrete proprement
`dmesg` doit montrer « lance (64 bits) » / « lance (32 bits) » pour chaque programme. Si `run hello` (64 bits) plante tout de suite
(redémarrage, écran figé, « exception 13 » ou « exception 14 » au démarrage du programme) : envoyez le texte ; `run /bin32/hello` doit alors encore marcher.

## Si ça ne démarre pas
Envoyez : la dernière image de l'écran, `logs/latest/` (`make EDITION=perf64 debug` ne gère pas encore OVMF : utilisez `serial-uefi` et copiez le texte du terminal),
et dites si c'est UEFI ou BIOS, et sous quel QEMU/Windows.
Écran noir en UEFI mais journal série correct = problème de console graphique (essayez `run-bios`).

## Mises à jour sous perf64 (même procédure que `docs/TEST_MAJ.md`, avec l'édition perf64)
1. `tools/phoenos_sign.py release --edition perf64 ...` (ou `publier.sh --edition perf64`) pour publier ; `maj verifier`, `maj installer`, `reboot` dans PhoenOS ;
2. au redémarrage, la ligne « lancement de la version … » doit apparaître puis l'écran graphique revient (la relance repasse du mode 64 bits au mode 32 bits puis relance le noyau) ;
3. si l'écran reste noir ou si ça redémarre en boucle : envoyez le journal série (`serial-uefi`) ; `maj retour` depuis le CD ramène à la version du CD.

## 0.12.0 : graphique et souris (QEMU, carte VGA standard : c'est le défaut)
    make EDITION=perf64 run-uefi
**Cliquez d'abord dans la fenêtre QEMU** : la souris est alors capturée (Ctrl+Alt+G pour la rendre à Windows).
    mode                         # résolution actuelle
    mode liste                   # résolutions proposées (carte VGA standard)
    mode 1280x720                # change sans redémarrer ; l'écran se redessine, le shell reste là
    souris                       # position et boutons en direct ; Echap pour finir
    bureau                       # fenêtres déplaçables (barre de titre), carré orange = fermer, barre du bas = rouvrir ; Echap = retour
    exe dessin                   # clic gauche : tracer, clic droit : effacer, touches 1 à 6 : couleur
    exe plasma ; exe mandel      # mandel : clic gauche = zoom, clic droit = dézoom
Après un programme graphique, la console texte doit revenir intacte. Essayez aussi `mode 1920x1080` puis `bureau`.
Menu GRUB des résolutions : maintenir Maj ou Echap au démarrage de GRUB. Rien de tout cela n'est testable sans QEMU : si l'écran reste noir
ou si la souris ne bouge pas, envoyez `dmesg` (lignes « video » et « mouse ») ou lancez `make EDITION=perf64 debug`.
