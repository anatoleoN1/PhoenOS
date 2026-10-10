# PhoenOS

Système d'exploitation x86 écrit en C : léger, efficace, gratuit.
Version actuelle : **0.0.5** (mini-shell interactif, historique, tests automatiques).

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
make run          # lance l'ISO dans QEMU (le journal s'affiche aussi dans le terminal)
make serial       # idem sans fenêtre graphique (Windows 10 + WSL, SSH...)
make debug        # comme run + journal des exceptions CPU dans qemu.log
make run PANIC_THEME=violet   # écran d'erreur violet au lieu d'orange
make test         # tests automatiques (15 programmes de test), sans démarrer l'OS
make clean
```

Pour tester sur une vraie machine : écrire `PhoenOS.iso` sur une clé USB
(`sudo dd if=PhoenOS.iso of=/dev/sdX bs=4M status=progress`, en vérifiant bien `sdX`).

## Ce qu'on doit voir au démarrage (v0.0.5)

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

## Le shell (nouveau en 0.0.5)

Tapez `help` pour la liste. Commandes : `help`, `clear`, `version`, `uptime`, `mem`, `memtest`,
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
(mémoire, shell, clavier AZERTY, `kprintf`) avec une fausse RAM, de faux scancodes et un écran
simulé : on vérifie ce qui serait affiché sans démarrer l'OS (quelques secondes). À lancer après
chaque modification. Ils ne remplacent pas un test dans QEMU : pilotes matériels (VGA, port série,
timer) et carte mémoire réelle de GRUB ne sont pas couverts.

Les messages de `kprintf` sont vérifiés par le compilateur : un `%d` sans argument, ou un argument
en trop, est une erreur de compilation. `kprintf` accepte la largeur et les zéros (`%2d`, `%05u`, `%08x`).

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

Pas encore de pagination : le noyau accède directement à la mémoire physique (prévu plus tard).

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
| `src/power.c`        | `reboot`, `shutdown`, `halt`                            |
| `src/version.h`      | Nom et numéro de version                                |
| `tests/`             | Tests automatiques (`make test`)                        |
| `src/io.h`           | `inb`/`outb`, `cli`/`sti`/`hlt`, sections critiques     |
| `linker.ld`          | Placement du noyau en mémoire (1 Mio)                   |
| `iso/boot/grub/`     | Configuration GRUB de l'ISO                             |

## Vision : où va PhoenOS ?

Le noyau actuel est déjà plus avancé que MS-DOS (32 bits, interruptions, segments prêts pour
le mode utilisateur), mais il n'a pas encore de système de fichiers ni de programmes.
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
7. [ ] Système de fichiers (initrd, puis disque) — *prochaine étape*
8. [ ] Appels système et programmes utilisateur
9. [ ] Mode graphique (framebuffer) et interface soignée
10. [ ] Passage en x86_64
