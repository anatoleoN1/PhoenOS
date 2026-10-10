# Journal des changements de PhoenOS

Numérotation **x.y.z** : x = version majeure (une par an), y = mineure (nouvelle fonction notable),
z = correctif ou petite amélioration. Chaque version est livrée testée automatiquement (`make test`) ;
les chemins de démarrage réels (GRUB, ISO, anneau 3, activation de la pagination) ne sont
vérifiables que sur une vraie machine ou sous QEMU : ils sont validés par vos essais.

## 0.6.3 — Plusieurs commandes par ligne
- `cmdline` : `;` et `&` hors guillemets sont des mots à part entière (même collés). Le shell découpe la ligne en
  commandes : `;` séquentiel, `&` arrière-plan (`execute` → `execute_tokens`) ; 32 mots par ligne, 16 par commande.
- Affichage : `kprintf_task_hook` prévient le shell avant/après chaque écriture d'un programme ; la ligne de saisie
  est effacée puis redessinée (`kprintf_quiet` : le redessin n'entre pas dans `dmesg`).
- Tests : séparateurs (cmdline), enchaînements et saisie interrompue (shell), crochet d'écriture (appels système).

## 0.6.2 — Système de debug
- `src/dlog.c` : journal de débogage (anneau de 1024 lignes + port 0xE9), niveaux trace/debug/info/warn/error ;
  `kprintf` rendu réentrant (`kvsnprintf`) pour pouvoir journaliser depuis les interruptions.
- Journalisé : démarrage et mémoire, multiboot, commandes du shell et durée, touches, programmes (lancement,
  fin, kill, fautes), appels système (arguments, résultat, erreur), ordonnanceur, IRQ, exceptions, panique,
  battement toutes les 5 s.
- Commande `log` ; appel système `log` (199) ; `ticker` et `boucle` écrivent dans le journal.
- `make debug*` : session `logs/<date-heure>/` (session, build, os, console, qemu), résumé final,
  `make logs`, `make logs-clean` ; script `tools/debug-session.sh`.
- Tests : `test_dlog`, `SYS_LOG`, commande `log`.

## 0.6.0 — Multitâche
- Ordonnanceur à tourniquet (`src/sched.c`) : jusqu'à 8 tâches, pile noyau de 16 Kio chacune, commutation
  en assembleur (`src/switch.S`, pile par tâche), attente par prédicat (clavier, sommeil, fin d'un programme),
  tâche inactive qui dort (`hlt`), tâches terminées libérées (y compris pages et mémoire).
- Préemption : à chaque tick de la minuterie, un programme en anneau 3 cède la place au suivant ; le noyau
  lui-même n'est pas interruptible (règle volontairement simple).
- Chaque programme est une tâche avec son espace d'adresses ; le parent le charge sans changer de CR3
  (`aspace_put`, `elf_load_with`). À chaque commutation : pile noyau du CPU (`tss.esp0`) et CR3 de la tâche.
- Shell : `programme &` (arrière-plan), `ps`, `kill`, Ctrl+C sur un programme de premier plan qui calcule.
  `sleep` et `sleep_ms` laissent tourner les autres. Un programme en arrière-plan lit la fin de fichier.
- Nouveaux programmes `ticker` et `boucle` ; manuel : `man multitache`, `ps`, `kill`.
- Descripteurs de fichiers vérifiés (`fs_is_live`) : supprimer un fichier ouvert par un autre programme
  ne laisse plus de pointeur invalide.
- Tests : `test_sched` (vraies commutations de pile : tourniquet, prédicats, sommeil, kill, détachement,
  table pleine, piles isolées, 300 créations sans fuite), `aspace_put`, `elf_load_with`, shell (`&`, `ps`, `kill`).
- Chemins vérifiables seulement sur machine/QEMU : changement de pile en ring 3, préemption réelle.

## 0.5.3 — Disque automatique, lancement de QEMU
(La 0.5.2 n'a pas ete publiee : on est passe directement a la 0.5.3.)
- Disque vierge : PhoenOS le prepare tout seul, au demarrage ou au premier `save` — plus de
  `format oui` a penser. Un disque qui contient deja autre chose n'est JAMAIS touche
  (message explicite ; `format oui oui` pour l'effacer volontairement).
- `make run` detecte l'absence d'ecran graphique et passe en mode terminal ; nouveaux
  `make run-sdl` / `make run-gtk` ; `make debug` fonctionne sans fenetre (journal `qemu.log`),
  `make debug-window` garde la fenetre.
- Test de bout en bout sur Chromebook de la 0.5.x : demarrage, isolation des programmes,
  fautes `vide` / `code` / `nul` / `kern`, `calc`, `notes`, `copie`, pas de fuite de memoire
  physique (la memoire libre est identique avant et apres les programmes).

## 0.5.1 — Correctif des tests
- `make test` echouait pour un utilisateur ordinaire (pas root) : les tests de `copie` et `notes`
  ecrivaient a la racine du disque et Linux creait les fichiers sans droits. Les programmes
  passent maintenant des droits 0644 a `open`, et le test de `notes` utilise un fichier de test.
  Verifie en lancant toute la suite sous un compte non privilegie.

## 0.5.0 — Un espace d'adressage par programme
- Chaque programme a son propre repertoire de pages (`src/aspace.c`) : il ne voit que les pages
  que le noyau lui a donnees (code, donnees, 64 Kio de pile), mappees a partir de 4 Mio ; tout le
  reste (noyau, adresse 0, pages non attribuees) provoque une faute qui n'arrete que le programme.
  A la fin du programme, ses pages sont rendues au gestionnaire de memoire (test : 1000 cycles sans fuite).
- Le code d'un programme est en lecture seule (ecrire dedans = faute) ; le depassement de pile est detecte.
- Les appels systeme verifient que les pointeurs du programme visent des pages REELLEMENT mappees
  (et modifiables quand le noyau doit y ecrire), plus seulement une plage d'adresses.
- Le chargeur ELF annonce les segments avant de charger (`elf_segments`), refuse les segments qui se chevauchent.
- `faute code` et `faute vide` pour voir ces protections ; `mkpoe` et applications `.poe` (premiere
  version du format : en-tete verifie, `exe`, `poeinfo`, /apps/*.poe dans l'ISO) — pas prioritaire,
  simple fondation.
- Pas encore de multitache : un seul programme a la fois (c'est la prochaine etape).

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
