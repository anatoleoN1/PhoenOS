# Journal des changements de PhoenOS

Numérotation **x.y.z** : x = version majeure (une par an), y = mineure (nouvelle fonction notable),
z = correctif ou petite amélioration. Chaque version est livrée testée automatiquement (`make test`) ;
les chemins de démarrage réels (GRUB, ISO, anneau 3, activation de la pagination) ne sont
vérifiables que sur une vraie machine ou sous QEMU : ils sont validés par vos essais.

## 0.4.0 — Les programmes écrivent des fichiers
- Nouveaux appels système : `open` avec options (`O_WRONLY`, `O_RDWR`, `O_CREAT`, `O_TRUNC`,
  `O_APPEND`, mêmes valeurs que Linux), `write` sur un fichier, `unlink` (suppression).
  Les écritures vont toujours en fin de fichier. Un fichier supprimé ferme ses descripteurs ouverts.
- Nouveaux programmes : `copie` (copie un fichier) et `notes` (carnet dans /notes.txt).
- Manuel : pages `copie`, `notes`, section « programmes » mise à jour.
- Tests : une trentaine de contrôles sur l'écriture/suppression (options invalides, dossier,
  fichier plein, double ouverture…), et les deux programmes exécutés pour de vrai.

## 0.3.0 — Calculatrice, `more`, défilement sans PageHaut/PageBas
- `calc` : première application utilisateur (virgule fixe, `+ - * / % ^`, parenthèses, `sqrt`, `abs`,
  `pi`, `ans`, mode interactif).
- `more` : lecture page par page (Espace, Entrée, `q`), fonctionne aussi après un tube.
- Défilement de l'écran : Ctrl+↑/↓, Alt+↑/↓, Maj+↑/↓ (console série) et Ctrl+B / Ctrl+F, pour les
  claviers sans PageHaut/PageBas (Chromebook).
- Pages de manuel pour `more`, `calc`, `faute`, `hello`, `primes`, `saisie`, `compte`, `heure`.
- Le chargeur ELF ignore les segments vides ; édition de liens des programmes en deux segments.

## 0.2.0 — Pagination
- Mémoire virtuelle activée : le code du noyau est en lecture seule, la page 0 est absente
  (déréférencement de NULL détecté), seule la zone 4–8 Mio est accessible aux programmes.
- `faute kern` et `faute nul` montrent le noyau survivant à un programme fautif ; `crash null|ro`.
- `mem` indique l'état de la pagination.

## 0.1.0 — Mode utilisateur (anneau 3)
- Appels système (`int 0x80`, numéros compatibles Linux i386), chargeur ELF, TSS, programmes en ring 3.
- Une faute d'un programme ne tue que le programme. Programmes fournis : `hello`, `primes`, `saisie`,
  `compte`, `heure`, `faute`. Commande `run`. L'ISO s'appelle désormais `phoenos-<version>.iso`.

## 0.0.9 — Horloge, tubes, outils texte
- Horloge temps réel (CMOS), `date`, date de modification des fichiers.
- Tubes `|` (jusqu'à 4 commandes) et outils `head`, `tail`, `wc`, `grep`, `sort`.

## 0.0.8 — Stockage et défilement
- Pilote disque ATA, commandes `disks`, `format`, `save`, `load` (sauvegarde PhoenFS à double
  instantané A/B protégée par somme de contrôle).
- Historique d'écran de 500 lignes (PageHaut/PageBas) ; console série (`make serial`) pour les
  environnements sans fenêtre QEMU.

## 0.0.7 — Manuel intégré
- Commande `man` : manuel complet dans PhoenOS (`initrd/docs/manuel.txt`) et dans `MANUEL.md`.

## 0.0.6 — Fichiers et initrd
- Système de fichiers en RAM (dossiers, fichiers, `ls`, `cat`, `cp`, `mv`, `rm`, `tree`, `df`, `hexdump`…).
- Disque initial (initrd, format tar) chargé par GRUB.

## 0.0.5 — Mini-shell et tests automatiques
- Shell interactif : historique, complétion par Tab, redirections `>` `>>`, guillemets.
- Banc de tests automatiques exécutant le vrai code du noyau sur la machine de développement.

## 0.0.4 — Écran d'erreur et mémoire
- Écran d'erreur orange (ou violet : `PANIC_THEME=violet`) avec trace d'appel.
- Mémoire physique (bitmap) et tas du noyau (`kmalloc`/`kfree`) avec détection de corruption.

## 0.0.3 et avant — Fondations
- Démarrage Multiboot par GRUB et image `.iso`, écran texte VGA, port série (journal copié sur COM1).
- Tables GDT/IDT, gestion des exceptions, PIC et minuterie (100 Hz), clavier PS/2 AZERTY,
  journal du noyau (`dmesg`).
