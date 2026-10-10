# PhoenOS

Système d'exploitation x86 écrit en C : léger, efficace, gratuit.
Version actuelle : **0.5.3** (un espace d'adressage par programme, première brique du multitâche) — historique complet dans [CHANGELOG.md](CHANGELOG.md)
Le détail de chaque commande et de ses options est dans **[MANUEL.md](MANUEL.md)** (et dans PhoenOS : `man`).

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
make iso          # génère phoenos-<version>.iso (ex. phoenos-0.1.0.iso), bootable
make run          # lance l'ISO dans QEMU (le journal s'affiche aussi dans le terminal)
make serial       # idem sans fenêtre graphique (Windows 10 + WSL, SSH...)
make debug        # comme run + journal des exceptions CPU dans qemu.log
make run PANIC_THEME=violet   # écran d'erreur violet au lieu d'orange
make test         # tests automatiques (15 programmes de test), sans démarrer l'OS
make clean
```

Pour tester sur une vraie machine : écrire `phoenos-0.1.0.iso` sur une clé USB
(`sudo dd if=phoenos-0.1.0.iso of=/dev/sdX bs=4M status=progress`, en vérifiant bien `sdX`).

## Ce qu'on doit voir au démarrage (v0.5.3)

Des lignes `[ OK ]` pour : VGA + port série, Multiboot, GDT, IDT/PIC/timer/clavier,
exceptions (point d'arrêt traité), **mémoire physique, tas du noyau et leurs auto-tests**,
« Timer PIT fonctionnel », une ligne de journal `[0.xxx INFO ] Demarrage termine...`
et l'invite `phoenos>` du shell (clavier AZERTY, AltGr pour `~ # { [ | \ @ ] }`).

## Comprendre un plantage

Tout ce qui est affiché à l'écran est aussi envoyé sur le port série : avec `make run` ou
`make serial`, le texte apparaît dans le terminal, même si l'écran est figé.

Une erreur du noyau affiche un **écran d'erreur** orange (violet avec `PANIC_THEME=violet`) avec :

- la cause (exception CPU décodée, ou message de `PANIC`/`ASSERT` avec `fichier:ligne`) ;
- les registres ;
- une **trace d'appel** : les adresses des fonctions appelantes.

Pour transformer une adresse de la trace en nom de fonction et ligne de code :

```bash
make symbol ADDR=0x100e60
```

Tests pour voir ces écrans : `make test-div0`, `make test-gp`, `make test-panic`, `make test-assert`,
`make test-doublefree` (double `kfree`), `make test-heapcorrupt` (débordement de tampon détecté dans le tas).

Dans le code du noyau :

```c
ASSERT(ptr != NULL);                  // panique avec fichier:ligne si faux
PANIC("cas impossible : %d", valeur); // arrêt immédiat, avec trace d'appel
klog(LOG_WARN, "disque lent (%u ms)", ms);   // journal horodaté
err_t e = ma_fonction(); if (e != E_OK) ... err_str(e) ...
```

Si la machine **redémarre en boucle sans rien afficher** (triple faute), lancer `make debug` :
QEMU s'arrête au lieu de redémarrer, et `qemu.log` contient la dernière exception
(`grep -n 'v=' qemu.log | tail`).

Limites connues : un débordement de pile est détecté après coup (canari testé à chaque tick du
timer) mais peut provoquer une triple faute avant ; la protection complète demande un TSS dédié
pour la double faute (prévu plus tard).

## Nouveautés de la 0.5.3

- Disque vierge préparé automatiquement (plus de `format oui` à retenir) ; un disque déjà utilisé n'est jamais touché.
- `make run` bascule seul en mode terminal sans écran graphique ; `make run-sdl`, `make run-gtk`, `make debug` sans fenêtre.

## Nouveautés de la 0.5.0

- **Un espace d'adressage par programme** : chaque programme vit dans ses propres pages, vues à partir de 4 Mio.
  Ce que le noyau ne lui a pas donné n'existe pas pour lui (`faute vide`) ; son code est en lecture seule
  (`faute code`) ; le noyau reste inaccessible (`faute kern`). Les pages sont rendues à la fin du programme.
- Fondations `.poe` (application = programme + en-tête vérifié) : `exe`, `poeinfo`, outil `tools/mkpoe.c`.

## Nouveautés de la 0.4.0

- Appels système d'écriture : `open` avec options (`O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_TRUNC`, `O_APPEND`),
  `write` sur fichier, `unlink`. Limite actuelle : les écritures s'ajoutent toujours en fin de fichier.
- `copie <source> <destination>` et `notes <texte>` / `notes` / `notes -v` (carnet dans /notes.txt ; `save` le conserve).

## Nouveautés de la 0.3.0

- **`calc`** : première vraie application (programme utilisateur). `calc 2+3*4`, `calc sqrt(2)`, ou `calc`
  seul pour le mode interactif ; opérateurs `+ - * / % ^`, parenthèses, `pi`, `ans`, virgule fixe (pas de flottants).
- **`more`** : lecture page par page (`man general | more`, Espace = page, Entrée = ligne, `q` = quitter).
- **Défilement sans PageHaut/PageBas** (Chromebook) : Ctrl+↑/↓, Alt+↑/↓, Maj+↑/↓ (console série) ou Ctrl+B / Ctrl+F.
- Pages de manuel pour `more`, `calc`, `faute`, `hello`, `primes`, `saisie`, `compte`, `heure`.

## Pagination (nouveau en 0.2.0)

La pagination est maintenant active (`src/paging.c`, `src/paging_enable.c`) :
- toute la RAM (1 Gio au plus) est mappée « à l'identique » mais **réservée au noyau** ;
- la page 0 est absente : `crash null` montre un pointeur nul attrapé par une faute de page lisible ;
- le code et les constantes du noyau sont **en lecture seule** : `crash ro` essaie d'y écrire ;
- seule la zone des programmes (4 Mio à 8 Mio) est accessible au ring 3 : `faute kern` (écrire dans le noyau)
  et `faute nul` (lire l'adresse 0) arrêtent le programme, le shell reste intact ;
- la mémoire vidéo est mappée sans cache. `mem` affiche l'état de la pagination.
La construction des tables est testée page par page (128 Mio). L'activation matérielle (CR3/CR0) ne peut pas l'être
sans démarrer la machine : au boot, une ligne « Pagination activee » doit apparaître.
Prochaine étape : un espace d'adresses par programme, puis le multitâche.

## Programmes utilisateur (0.1.0)

PhoenOS lance maintenant de vrais programmes, en **mode utilisateur (ring 3)**, séparés du noyau :
- `ls /bin` liste les programmes ; taper `hello a b`, `primes 100`, `saisie`, `compte /etc/motd`, `heure`, `faute gp`.
  `faute gp|div|ud|io` provoque une erreur dans le programme : le noyau l'arrête, affiche où, et vous revenez au shell.
- Ils fonctionnent avec les tubes et redirections : `primes 50 > p.txt`, `echo Zoe | saisie`.
- Écrire un programme : créer `user/nom.c` (voir `user/hello.c` et `user/phoenos.h`), puis `make iso` ; il apparaît dans `/bin/nom`.
- Technique : TSS + porte `int 0x80` (appels système, numéros de Linux pour exit/read/write/open/close/time/getpid),
  chargeur ELF32 défensif (`src/elf.c`), zone de 4 Mio à 8 Mio réservée avant le tas, instructions privilégiées/ports/fautes
  interceptés (`src/user.c`, `src/syscall.c`, `src/user_enter.S`).
- Depuis la 0.2.0, la pagination isole la mémoire du noyau (voir plus haut).
- Le fichier ISO s'appelle maintenant `phoenos-<version>.iso`.

## Horloge, tubes et outils texte (0.0.9)

- `date` lit l'horloge de la machine (puce CMOS) ; `date -s` donne les secondes depuis 1970. `make run` règle QEMU sur
  l'heure de votre ordinateur. Pas de fuseaux horaires : c'est l'heure locale de la machine.
- Chaque fichier et dossier a une date de modification : `ls -l` l'affiche, elle est conservée dans la sauvegarde
  disque (champ « mtime » du tar, donc lisible aussi par `tar tvf`).
- Tubes : `ls -l / | grep docs | wc` (espaces autour du `|` obligatoires, 4 commandes au plus). Redirection en fin : `a | b > f`.
- Nouveaux outils : `head -N`, `tail -N`, `wc [-lwc]`, `grep [-inv] motif`, `sort [-ru]` ; `cat` sans fichier lit le tube.
- Limite d'arguments portée à 16. Détails : `man <commande>`.

## Stockage et défilement (0.0.8)

**Défilement** : PageHaut / PageBas remontent dans les 500 dernières lignes de l'écran (12 lignes par appui) ;
toute autre touche ramène à la saisie.

**Disque** : `make run` crée `disk.img` (32 Mio) et le branche en IDE dans QEMU (il n'est pas effacé
par `make clean` ; `make newdisk` repart de zéro). Dans PhoenOS : `disks`, puis `format oui` **une seule fois**.
Ensuite `save` / `load`, et `shutdown`, `reboot` et `halt` enregistrent seuls les modifications. Au démarrage,
la sauvegarde est relue par-dessus l'initrd (voir `man stockage`). Sous VirtualBox : ajouter un disque IDE
(contrôleur PIIX4) au VM ; sans disque, tout fonctionne comme avant (fichiers en RAM seulement).
Format : « PhoenFS », deux emplacements A/B de 8 Mio au plus, archive tar + CRC32, validation en dernier
(une coupure pendant l'écriture ne détruit jamais l'ancienne sauvegarde). Le test simule 12 coupures de courant.

## Console série : utiliser PhoenOS sans fenêtre (Chromebook, WSL...)

`make serial` lance PhoenOS **sans fenêtre** : l'écran et le clavier sont ce terminal (Ctrl+A puis X pour quitter).
Les touches tapées sont lues sur le port série COM1 (Entrée, Retour arrière, Tab, flèches, PageHaut/Bas, Ctrl+C/L/U,
lettres accentuées). Pratique quand la fenêtre de QEMU s'affiche mais ne reçoit pas le clavier (cas observé sous
Linux dans ChromeOS). Si vous préférez la fenêtre : cliquer dedans d'abord, ou essayer
`make run QEMU_DISPLAY="-display sdl"` (ou `gtk`). Avec `make run`, le terminal garde aussi le clavier série.

## Manuel (0.0.7)

`man` liste les sujets, `man ls` affiche la page de `ls`. Le texte vient de `initrd/docs/manuel.txt`
(une section par commande, commençant par `== nom - résumé`). `MANUEL.md` en est la version lisible
hors de PhoenOS. `make test` vérifie que chaque commande du shell a sa page, et inversement.
Les commandes de PhoenOS ne sont pas celles de Linux : chaque page dit ce qui diffère.
**Ajouter une commande = ajouter sa page au manuel** (le test échoue sinon).

## Fichiers et initrd (0.0.6)

Le noyau a un **système de fichiers en RAM** (dossiers, fichiers, chemins absolus ou relatifs,
`.` et `..`). Au démarrage, GRUB charge `initrd.tar` (le contenu du dossier `initrd/`, empaqueté par
`make iso`) et le noyau le déverse dans le système de fichiers. Pour ajouter un fichier au démarrage :
le mettre dans `initrd/` puis `make iso`. Tout ce qui est créé ensuite est **perdu à l'extinction**
(pas encore de disque). `/etc/motd`, s'il existe, est affiché au démarrage.

Commandes : `ls [-l]`, `cat`, `cd`, `pwd`, `mkdir [-p]`, `touch`, `rm [-r]`, `rmdir`, `cp`, `mv`,
`tree`, `df`, `hexdump`. Redirection : `echo salut > a.txt`, `help >> doc.txt` (le nom de fichier est le
dernier mot de la ligne ; `echo ">"` entre guillemets reste un simple texte). Les messages d'erreur
restent toujours à l'écran. Tab complète aussi les chemins. Limites : noms 31 caractères, fichiers 1 Mio,
4096 éléments, 32 niveaux. L'archive est lue de façon prudente (checksum, chemins `..` refusés,
tailles vérifiées) et la mémoire du module est protégée du tas.

**Changement pour développeurs** : `%x` de `kprintf` n'ajoute plus `0x` ; utiliser `%#x` (ou `%#010x`).

## Le shell (0.0.5)

Tapez `help` pour la liste. Commandes système : `help`, `clear`, `version`, `uptime`, `mem`, `memtest`,
`echo`, `sleep`, `colors`, `dmesg`, `history`, `crash`, `reboot`, `shutdown`, `halt`.

| Touche              | Effet                                                            |
|---------------------|------------------------------------------------------------------|
| Flèches haut / bas  | Parcourent l'historique (16 commandes) ; la saisie en cours est conservée |
| Tab                 | Complète le nom d'une commande ; liste les choix s'il y en a plusieurs |
| Ctrl+C / Ctrl+U     | Abandonne la ligne / l'efface                                    |
| Ctrl+L              | Efface l'écran en gardant la ligne                               |
| Retour arrière      | Efface le dernier caractère                                      |

`echo "a   b" c` : les guillemets doubles regroupent plusieurs mots en un seul argument.
`crash div0|gp|ud|panic|assert|doublefree|heap` provoque une erreur pour voir l'écran d'erreur.
`dmesg` rejoue le journal du noyau depuis le démarrage (les 8 derniers Kio).
Limite actuelle : l'édition se fait en fin de ligne (pas de déplacement avec ← →).

**Ajouter une commande** dans `src/shell.c` : écrire `static err_t cmd_xxx(int argc, char **argv)`,
ajouter son prototype puis une ligne dans le tableau `commands`. Renvoyer `E_OK` si tout va bien,
un code d'erreur sinon (le shell affiche la cause), ou `E_REPORTED` si le message a déjà été affiché.

Sous QEMU lancé par `make run`, `reboot` quitte QEMU (option `-no-reboot` voulue : elle évite les
redémarrages en boucle lors d'un plantage) ; `shutdown` ferme la machine virtuelle proprement.

## Tests automatiques

`make test` compile des petits programmes 32 bits qui exécutent le **vrai code du noyau**
(mémoire, shell, clavier AZERTY, `kprintf`, système de fichiers, lecteur tar avec une vraie archive GNU tar) avec une fausse RAM, de faux scancodes et un écran
simulé : on vérifie ce qui serait affiché sans démarrer l'OS (quelques secondes). À lancer après
chaque modification. Ils ne remplacent pas un test dans QEMU : pilotes matériels (VGA, port série,
timer) et carte mémoire réelle de GRUB ne sont pas couverts.

Les messages de `kprintf` sont vérifiés par le compilateur : un `%d` sans argument, ou un argument
en trop, est une erreur de compilation. `kprintf` accepte la largeur et les zéros (`%2d`, `%05u`, `%08x`), `%-12s` (aligné à gauche) et `%#x` (préfixe 0x).

## Mémoire (0.0.4)

- `pmm.c` : gestionnaire de mémoire physique, un bit par frame de 4 Kio, alimenté par la carte
  mémoire de GRUB. Protège la mémoire basse (< 1 Mio) et l'image du noyau. Détecte les doubles
  libérations et les adresses invalides.
- `kheap.c` : tas du noyau (jusqu'à 8 Mio), `kmalloc` / `kzalloc` / `kfree`. Découpe les blocs,
  fusionne les voisins libres. Chaque bloc a un en-tête avec un nombre magique : un `kfree` en double,
  un pointeur invalide ou un débordement sur le bloc suivant déclenchent l'écran d'erreur avec le détail.
  `kheap_check()` vérifie tout le tas à la demande.
- `string.c` : `memset`, `memcpy`, `memmove`, `memcmp`, `strlen`, `strcmp`.

```c
char *buf = kmalloc(256);
if (!buf)                   // kmalloc renvoie NULL si le tas est plein : toujours tester
    return E_NOMEM;
...
kfree(buf);
```

La pagination (0.2.0) protège le noyau : voir plus haut.

## Structure

| Fichier              | Rôle                                                    |
|----------------------|---------------------------------------------------------|
| `src/boot.S`         | En-tête Multiboot, pile, appel du noyau C               |
| `src/kernel.c`       | `kernel_main`, séquence de démarrage et auto-tests      |
| `src/vga.c`          | Texte VGA 80x25 : couleurs, défilement, curseur         |
| `src/serial.c`       | Port série COM1 (journal de debug)                      |
| `src/kprintf.c`      | Mini `printf` du noyau (écran + série)                  |
| `src/log.c`          | `klog` : journal horodaté par niveau                    |
| `src/panic.c`        | Écran d'erreur orange/violet, `PANIC`, `ASSERT`, trace d'appel |
| `src/error.h`        | Codes d'erreur `err_t` et `err_str`                     |
| `src/gdt.c`, `gdt_flush.S` | Table des segments (noyau + utilisateur)          |
| `src/idt.c`          | Table des interruptions (vecteurs 0-47)                 |
| `src/isr_stubs.S`    | Stubs d'entrée des interruptions                        |
| `src/isr.c`          | Exceptions CPU décodées et distribution des IRQ         |
| `src/pic.c`          | Contrôleur 8259 (remap, masque, EOI, fausses IRQ)       |
| `src/pit.c`          | Timer 100 Hz, `pit_ticks`, `pit_sleep_ms`               |
| `src/keyboard.c`     | Clavier PS/2, AZERTY, AltGr, flèches, tampon circulaire |
| `src/pmm.c`          | Mémoire physique : frames de 4 Kio                      |
| `src/kheap.c`        | Tas du noyau : `kmalloc` / `kfree`                      |
| `src/string.c`       | `memset`, `memcpy`, `strlen`...                         |
| `src/multiboot.h`    | Structures d'information de GRUB (carte mémoire)        |
| `src/shell.c`        | Shell : saisie, historique, Tab, commandes              |
| `src/cmdline.c`      | Découpage de ligne, nombres, historique (testable seul) |
| `src/fs.c`           | Système de fichiers en RAM (arbre, chemins)             |
| `src/initrd.c`       | Lecteur d'archives tar → système de fichiers           |
| `src/ata.c`          | Pilote disque ATA/IDE en mode PIO (détection, lecture, écriture) |
| `src/persist.c`      | Sauvegarde PhoenFS sur disque (deux emplacements, CRC)  |
| `src/tarw.c`         | Écriture du système de fichiers au format tar           |
| `src/crc32.c`        | Somme de contrôle CRC-32                                |
| `src/power.c`        | `reboot`, `shutdown`, `halt`                            |
| `src/version.h`      | Nom et numéro de version                                |
| `tests/`             | Tests automatiques (`make test`)                        |
| `src/io.h`           | `inb`/`outb`, `cli`/`sti`/`hlt`, sections critiques     |
| `linker.ld`          | Placement du noyau en mémoire (1 Mio)                   |
| `user/`              | Programmes utilisateur (un .c = un programme de /bin)    |
| `iso/boot/grub/`     | Configuration GRUB de l'ISO                             |
| `MANUEL.md`          | Manuel des commandes (copie de `initrd/docs/manuel.txt`) |
| `initrd/`            | Fichiers présents dans PhoenOS dès le démarrage         |

## Vision : où va PhoenOS ?

Le noyau actuel est déjà plus avancé que MS-DOS (32 bits, interruptions, segments prêts pour
le mode utilisateur), mais il n'a pas encore de disque ni de programmes.
L'architecture visée est en couches, chacune posée sur la précédente :

1. noyau : mémoire, interruptions, processus ;
2. pilotes : clavier, écran, disque, plus tard réseau et USB ;
3. appels système (frontière noyau / programmes) ;
4. programmes utilisateur, dont un shell en ligne de commande ;
5. interface graphique par-dessus (framebuffer, fenêtres), sans toucher au noyau.

## Feuille de route

Chaque étape produit une ISO fonctionnelle.

1. [x] Boot Multiboot + affichage texte + `make iso`
2. [x] GDT et IDT (gestion des exceptions)
3. [x] PIC, timer (PIT) et interruptions
4. [x] Traitement d'erreurs (panique, trace d'appel, journal série) et clavier PS/2
5. [x] Mémoire physique et tas du noyau (`kmalloc`) — *la pagination viendra plus tard*
6. [x] Mini-shell, historique, complétion, tests automatiques
7. [x] Système de fichiers en RAM + initrd (0.0.6), disque ATA et sauvegarde (0.0.8)
8. [x] Horloge, dates, tubes et outils texte (0.0.9)
9. [x] Appels système, ELF et programmes utilisateur en ring 3 (0.1.0)
10. [x] Pagination : noyau protégé, programmes isolés (0.2.0)
10b. [x] Calculatrice, `more`, défilement au clavier réduit (0.3.0)
10c. [x] Programmes qui écrivent des fichiers (0.4.0)
10d. [x] Un espace d'adressage par programme (0.5.0)
11. [ ] Un espace d'adresses par programme, multitâche
12. [ ] Mode graphique (framebuffer) et interface soignée
13. [ ] Passage en x86_64
