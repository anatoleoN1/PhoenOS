# PhoenOS

**Un système d'exploitation x86 écrit en C, gratuit, lisible et modifiable par tous.**
Il démarre avec GRUB, tourne dans QEMU (ou sur une vraie machine), et vise un système rapide,
sobre et soigné, qui fonctionne quelle que soit la puissance du PC.

> ⚠️ **Projet en développement (0.x).** Testez-le dans une machine virtuelle et sauvegardez vos données.
> Version actuelle : **0.8.0** · [Télécharger une version](../../releases) · [Licence PhoenOS 1.0](LICENSE)

## Télécharger

Chaque version a sa **release** : le zip du code source et l'ISO prête à démarrer.
Vous voulez la 0.3.0, la 0.0.2 ou la 0.6.3 ? Ouvrez [Releases](../../releases), choisissez le numéro, téléchargez
`phoenos-X.Y.Z.iso` (et `SHA256SUMS` pour vérifier le fichier). La branche `main` contient le dernier code.

Essayer une ISO dans QEMU :

```sh
qemu-system-i386 -cdrom phoenos-0.8.0.iso -serial stdio -m 128
```

## Ce que PhoenOS sait faire (0.8.0)

- **Noyau 32 bits** : GDT/IDT, interruptions, minuterie 100 Hz, mémoire physique et tas du noyau, **pagination** avec noyau protégé.
- **Multitâche** : ordonnanceur à tourniquet, un espace d'adressage par programme, programmes en anneau 3 isolés les uns des autres.
- **Programmes complets** : un programme peut en lancer d'autres (`spawn`), les attendre (`waitpid`), les arrêter (`kill`), et dispose d'un tas (`brk`, `malloc`).
- **Applications et bibliothèques** : fichiers `.poe` complets (`poepack`, `poeunpack`, `poeinfo`) et **bibliothèques partagées `.psl`** (`psl`, `pslpack`) dont le code n'existe qu'en un exemplaire en mémoire.
- **Fichiers** : système de fichiers en RAM, initrd, disque ATA avec sauvegarde (PhoenFS, deux emplacements avec CRC).
- **Shell** : historique, complétion, tubes, redirections, plusieurs commandes par ligne (`;` et `&`), manuel intégré (`man`).
- **Programmes fournis** : calculatrice, carnet de notes, nombres premiers, copie, `ticker`, `boucle`, `lance`, `essaim`, `tas`…
- **Diagnostic** : écran d'erreur orange (ou violet), journal de débogage (`log`), `make debug` qui enregistre tout dans `logs/`.
- **Qualité** : des tests automatiques exécutent le vrai code du noyau (`make test`), y compris les commutations de tâches.

## Compiler et lancer

Sous Debian/Ubuntu/WSL :

```sh
sudo apt install build-essential gcc-multilib make grub-pc-bin xorriso mtools qemu-system-x86
make test          # tests automatiques (aucune machine virtuelle nécessaire)
make iso           # construit phoenos-X.Y.Z.iso
make serial        # lance dans ce terminal (clavier compris) ; quitter : Ctrl+A puis X
make run           # lance avec une fenêtre QEMU si votre système en a une
make debug         # comme serial, en enregistrant tout dans logs/<date-heure>/
```

Une fois dans PhoenOS : `help` liste les commandes, `man` ouvre le manuel (aussi dans [`MANUEL.md`](MANUEL.md)).
Essayez `exe utilib`, `psl`, `essaim 4 3 100`, `lance primes 50`, `tas 1000` ou `calc 2+3*4`.

## Organisation du dépôt

| Dossier | Contenu |
|---|---|
| `src/` | le noyau : démarrage, mémoire, ordonnanceur, appels système, fichiers, shell… |
| `user/` | les programmes en anneau 3 et leur mini-bibliothèque (`phoenos.h`, `libc.h`) |
| `lib/` | les bibliothèques partagées d'exemple (`maths`, `texte`) |
| `initrd/` | les fichiers embarqués dans l'ISO (manuel, message d'accueil) |
| `tests/` | les tests automatiques |
| `tools/` | outils (`mkpoe`, `mkpsl`, session de debug) |
| `docs/` | guide technique détaillé (`docs/GUIDE.md`) |

L'historique de chaque version est dans [`CHANGELOG.md`](CHANGELOG.md).

## Feuille de route

| Version | Contenu prévu |
|---|---|
| 0.8.0 | fichiers `.poe` plus complets (créer, exécuter) et bibliothèques partagées `.psl` |
| 0.9.0 | premiers pilotes officiels, connexion réseau (Wi-Fi) |
| 0.10.0 | mises à jour automatiques de PhoenOS |
| 0.11.0 | affichage plus grand : 360p, 480p, 720p, 1080p… |
| 0.11.1 | pilote graphique |
| 0.100.0 | « PhoenOS and Term UEFI » : le terminal actuel + la version graphique par-dessus |
| 1.0.0 | première version officielle, trois éditions : Performance, Professionnelle, Puissance |

Numérotation **x.y.z** : x = grande version, y = mise à jour notable, z = correctif.

## Pilotes et extensions de la communauté

Chaque version de pilote officielle est publiée avec PhoenOS. La communauté peut écrire ses propres pilotes,
bibliothèques (`.psl`) et applications (`.poe`) pour ses besoins : ils sont **non officiels et utilisés aux risques et périls**
de chacun (voir l'article 5 de la [licence](LICENSE)).

## Licence

PhoenOS est sous la **[licence PhoenOS 1.0](LICENSE)** : le code est lisible, modifiable et redistribuable gratuitement
sous la même licence. Une version modifiée doit s'appeler **« PhoenOS \<surnom\> »**, indiquer clairement qu'elle est modifiée
(version d'origine, éditeur d'origine, éditeur de la modification — modèle : [`MODIFICATIONS.exemple`](MODIFICATIONS.exemple))
et ne pas altérer l'image de PhoenOS. Ce n'est pas une licence « open source » au sens de l'OSI : elle ajoute ces conditions de nom et de mention.

## Contribuer

Les retours de test (comment vous avez lancé PhoenOS, ce qui s'est passé, le journal `log save` ou `make debug`) sont précieux.
Ouvrez une *issue* pour un bug ou une idée ; une contribution de code est acceptée sous la licence du projet.
