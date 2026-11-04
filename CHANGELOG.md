# Journal des changements de PhoenOS

Numérotation **x.y.z** : x = version majeure (une par an), y = mineure (nouvelle fonction notable),
z = correctif ou petite amélioration. Chaque version est livrée testée automatiquement (`make test`) ;
les chemins de démarrage réels (GRUB, ISO, anneau 3, activation de la pagination) ne sont
vérifiables que sur une vraie machine ou sous QEMU : ils sont validés par vos essais.

## 0.11.3 — perf64 : vrais programmes 64 bits, /bin32 et /lib32
- **Programmes 64 bits** (perf64) : le noyau lance maintenant de vrais programmes x86-64 en ring 3 (mode long, nouveau segment de code utilisateur 64 bits `0x3B` dans la GDT, `enter_user_mode64`
  dans `x64/switch.S`) en plus des programmes 32 bits (mode de compatibilite). Meme `int 0x80`, memes numeros d'appels, memes espaces d'adressage (zone 4-8 Mio) : un programme 64 bits recoit son resultat
  dans tout `rax` (signe etendu). Pile initiale 64 bits : `argc` sur 8 octets puis `argv[]` en pointeurs de 8 octets (`user/crt0_64.c`).
- **Chargeur ELF** (`src/elf.c`) : lit les ELF 32 bits ET 64 bits (`elf_class`) ; pour un ELF64 toute adresse, taille ou decalage au-dessus de 4 Gio est refuse (aucune troncature silencieuse). Un programme 64 bits
  est refuse par perf32 (`E_NOEXEC`, rien n'est reserve).
- **/bin32, /apps32, /lib32** (comme System32 / SysWOW64) : sur perf64 les programmes, applications `.poe` et bibliotheques `.psl` natifs (64 bits) sont dans `/bin`, `/apps`, `/lib`, et les 32 bits dans
  `/bin32`, `/apps32`, `/lib32`. Sur perf32 rien ne change. La classe (32/64) est lue dans l'ELF : aucun changement des formats `.poe` / `.psl`. `run nom`, le nom seul, `exe` et `spawn` cherchent d'abord
  le natif puis le 32 bits ; `psl` liste les deux dossiers.
- **Bibliotheques par classe** : un programme n'obtient que des bibliotheques de sa largeur ; le gestionnaire (`libmgr`) garde un exemplaire PAR classe et par nom (une `maths` 32 bits et une `maths` 64 bits
  peuvent coexister) ; `mkpsl` lit la table des symboles ELF64 ; `psl` indique `[32 bits]` pour les bibliotheques non natives.
- **spawn** : un programme 64 bits passe un `argv` de pointeurs de 8 octets (les 4 octets hauts doivent etre nuls).
- **Les 16 programmes d'exemple et les 2 bibliotheques existent aussi en 64 bits** : `user/phoenos.h` et `user/libc.h` ne supposent plus des pointeurs de 32 bits (`uptr`, tas aligne sur 16 avec en-tete de 32 octets
  en 64 bits) ; `make EDITION=perf64 iso` compile et emballe les deux versions (`build/user64`, `build/lib64`).
- **Tests** (49 -> tous passent) : les programmes 64 bits sont COMPILES comme pour l'ISO et EXECUTES pour de vrai sous Linux x86-64 (`tests/crt0_t64.c` met pile et arguments sous 4 Gio, car `int 0x80` tronque
  les pointeurs) : hello, primes, saisie, compte, copie, ticker, tas, libc ; `elf` (classes, ELF64 forges et hostiles, vrais ELF64), `bibliotheques` (psl 64 bits, cache par classe, refus d'une mauvaise classe,
  aucune fuite), `appels-systeme` (argv de 8 octets), `programmes-multiples` (un ELF64 refuse par perf32).
- **Non verifie ici** : le demarrage reel du ring 3 64 bits (segment `0x3B`, `iretq`) n'a pas ete essaye sur une machine ni sous QEMU ; voir `docs/TEST_64BITS.md`.

## 0.11.2 — correctifs de la 0.11.x (aucune nouvelle fonction)
- **Lancement UEFI** (`make run-uefi`, `serial-uefi`) : les firmwares « code seul » (`OVMF_CODE_4M.fd`, Ubuntu récent) se branchent en pflash avec une copie des variables (`build/OVMF_VARS.fd`) au lieu de `-bios`, que QEMU refusait (« could not load PC BIOS ») ; un firmware vide est ignoré.
- **Test `tls-openssl`** : pause de 2 s avant l'arrêt du serveur de test (course du banc d'essai sur machines lentes, p. ex. WSL ; le code TLS n'est pas en cause).
- **`tools/phoenos_sign.py`** : empaquetage des noyaux ELF64 inclus dans cette version (perf64 publiable), `update.c`/`stubs.c` perf64 corrigés (symboles de relance), repris de la 0.11.0 corrigée.
- Les versions 0.11.0 et 0.11.1 publiées avant ces correctifs restent valides pour perf32 ; utilisez la 0.11.2 pour perf64.

## 0.11.1 — perf64 : programmes, appels système et mises à jour
- **Programmes sous perf64** : les programmes `.poe`/ELF existants (32 bits) tournent tels quels en **mode de compatibilité** du processeur (code utilisateur 32 bits sous le noyau 64 bits) : même `int 0x80`,
  mêmes appels système, mêmes bibliothèques `.psl`, même `spawn`/`waitpid`/réseau/TLS. Aucun programme à recompiler ; de vrais programmes 64 bits viendront avec la version 1.0.
- **Espaces d'adressage 64 bits** (`src/aspace.c`, `pte_t` 64 bits) : chaque programme a SON PML4, SON PDPT, SON premier répertoire et 2 tables de pages pour la zone 4-8 Mio ; le noyau reste mappé mais interdit au ring 3 ;
  les pages au-delà de 1 Gio (écran, périphériques) restent partagées. Test : `espaces-adressage-64` (parcours logiciel des 4 niveaux : isolation, droits, pages partagées, mémoire pleine, aucune fuite).
- **Mises à jour sous perf64** : l'étape 0 relance le noyau installé depuis le mode long (`src/x64/relaunch.S` : copie, `.bss` à zéro, retour en mode protégé 32 bits, saut au point d'entrée) ;
  `x64/boot.S` reconnaît ce démarrage (magic Multiboot 1) et reprend les infos d'écran (`mb1_adopt`, champs d'écran du Multiboot 1 recopiés par `upd_relaunch`).
  `tools/phoenos_sign.py` empaquette aussi les noyaux ELF64 (adresses sous 4 Gio) : `release --edition perf64` fonctionne.
- `version` et le message de démarrage indiquent l'architecture et l'édition. Tests : `espaces-adressage-64`, `multiboot2` (aller-retour des infos d'écran), `maj-stockage` (reprise de l'écran), outil de signature (ELF64). Les tests 64 bits tournent en natif (`syscall`).
- **Limites** : tout ceci n'est pas démarré ici (pas d'émulateur) : bascule en mode de compatibilité, `iretq` vers le ring 3 et relance 64 → 32 bits sont à valider chez vous. Les programmes utilisateur restent 32 bits.

## 0.11.0 — PhoenOS 64 bits (édition perf64, UEFI)
- **Nouvelle édition `perf64`** (`make EDITION=perf64 iso` → `phoenos-perf64-0.11.0.iso`) : noyau x86-64 qui démarre en **UEFI** ou en BIOS classique par GRUB (Multiboot2). L'édition `perf32` reste construite comme avant.
- **Démarrage** (`src/x64/boot.S`) : mode protégé 32 bits → pagination identité 4 Gio (pages de 2 Mio ; au-delà de 1 Gio sans cache : périphériques et écran) → mode long → noyau C.
  Les informations Multiboot2 sont converties au format Multiboot 1 (`src/x64/mb2.c`) : le reste du noyau (mémoire, initrd, mises à jour) est inchangé.
- **Noyau 64 bits** : `usize` 64 bits partout (piles, ordonnanceur, traces d'appel), GDT/IDT/TSS 64 bits, pile IST dédiée pour la double faute, commutation de tâches x86-64, `kprintf` avec `%l`, `%ll`, `%z`.
  Tout le noyau et toutes les allocations restent sous 4 Gio (pointeurs 64 bits, adresses 32 bits) ; la RAM gérée est limitée à 1 Gio comme avant.
- **Console graphique** (`src/x64/fbcon.c`, police 8x16 dans le domaine public) : l'écran 80x25 est dessiné en pixels (UEFI n'a pas de texte VGA), agrandi d'un facteur entier et centré ; mémoire vidéo en écriture combinée (PAT) ;
  l'écran d'erreur garde ses couleurs. Sans écran graphique, retour au texte VGA.
- **Pas encore en 64 bits** (0.11.1) : programmes utilisateur (anneau 3), appels système, bibliothèques `.psl`, relance de l'étape 0 des mises à jour. Le shell, le réseau, le disque, TLS, `web` et `maj verifier` marchent ; lancer un programme répond « non implemente ».
- Makefile : `EDITION=perf64`, `make check` (Multiboot2), `make run-uefi | serial-uefi | run-bios` (OVMF). Procédure d'essai : `docs/TEST_64BITS.md`.
- Tests : `console-graphique` (faux écran : échelle, centrage, glyphes, curseur, palette), `multiboot2` (conversion des informations de démarrage). Les 2 noyaux compilent sans avertissement.
- **Limite importante** : le démarrage réel (boot.S, mode long, UEFI) n'a pas pu être exécuté ici (pas d'émulateur) : il est validé par vos essais.

## 0.10.5 — Version d'essai des mises à jour
- **Aucune fonction nouvelle** : cette version ne sert qu'à vérifier, sur une vraie machine, la chaîne complète de la 0.10.4 : publier → `maj verifier` → `maj installer` → `reboot` → relance
  de la version installée par l'étape 0 → confirmation après 30 s → (au besoin) retour arrière. Seuls changent le numéro de version (build 10005), le message d'accueil et la documentation.
- **Marche à suivre pas à pas** : `docs/TEST_MAJ.md` (publier la 0.10.4, la démarrer depuis son ISO, publier la 0.10.5, l'installer depuis PhoenOS, quoi regarder, quels logs envoyer, essais de retour arrière).
- Tests : les vecteurs signés des tests de mise à jour supposent un noyau 0.10.4 : `version.h` a un mode `PHOENOS_TEST_PIN_OLD` (tests seulement) pour que les tests ne dépendent plus du numéro courant.
- Feuille de route : après les essais, perf32 / perf64 / pui64 puis passage en 64 bits UEFI (C + Rust).

## 0.10.4 — Mises à jour automatiques signées
- **`maj`** (`etat`, `verifier`, `installer`, `retour`, `preparer`, `auto`) : PhoenOS cherche les nouvelles versions dans les *releases* GitHub du dépôt (`anatoleoN1/PhoenOS`), en HTTPS.
- **Signature Ed25519** : chaque édition publie un manifeste (`manifest-<édition>.txt`, version, build, tailles, SHA-256 du noyau et de l'initrd) signé avec VOTRE clé ;
  la clé publique est intégrée à l'image (`src/updkey.h`). Rien n'est installé ni lancé sans signature valide, bonne édition, bonnes empreintes et en-tête d'image correct.
  Une version plus ancienne que la dernière confirmée est refusée (anti-retour-arrière).
- **Deux emplacements A/B** sur le disque (secteurs 32768 à 65535, après PhoenFS, 16 Mio) : on écrit dans l'emplacement inactif, la bascule est la TOUTE dernière écriture
  (test : coupure de courant simulée à chaque secteur écrit, jamais d'emplacement actif invalide).
- **Étape 0** (début de `kernel_main`) : si un emplacement est actif, le noyau du CD vérifie (signature + SHA-256 en mémoire) puis relance la version installée
  (« kexec » : `src/relaunch.S`). Lancée « à l'essai » ; confirmée après 30 s de fonctionnement ; après 2 essais ratés, retour automatique à la version précédente ou à celle du CD.
- **Mode automatique** (`maj auto verifier|installer`, défaut : vérifier) : tâche de fond, première vérification 90 s après le démarrage puis toutes les 6 h ; un avis jaune `[maj]` s'affiche au prompt.
- **Outil** `tools/phoenos_sign.py` (Python seul, sans dépendance) : `keygen`, `pubkey`, `release`, `verify`. **Éditions** : l'ISO s'appelle `phoenos-<édition>-<version>.iso` (pour l'instant `perf32`).
- Client HTTP du noyau (`src/httpc.c`) : redirections HTTPS (GitHub redirige vers son CDN), Content-Length, « chunked ».
- Tests : `maj-manifeste`, `maj-http`, `maj-stockage` (choix de démarrage, retour arrière, relance simulée avec fausse RAM), `maj-complet` (commande `maj` de bout en bout contre un faux GitHub et un faux disque,
  manifestes/noyaux/initrds altérés, autre clé, autre édition, plus ancien, coupures de courant), `outil-signature` (vecteurs RFC 8032, recoupement `cryptography`, release/verify). Contrôles par mutation.
- **Limites** : le vrai démarrage de l'étape 0 (relance du noyau sur une machine) n'est vérifiable que chez vous ; disque de 32 Mio au moins et `maj preparer` une fois ; pas de révocation de clé
  (clé perdue = nouvelle ISO avec une nouvelle clé) ; l'anti-retour-arrière repose sur l'état du disque ; une seule session TLS dans le système (`maj` et `web` ne peuvent pas télécharger en même temps) ;
  l'horloge doit être exacte (certificats).
- Feuille de route : après les mises à jour, PhoenOS passe en **64 bits UEFI** et devient **mi-C mi-Rust** ; éditions Performance (32 et 64 bits) et Puissance (64 bits seulement).

## 0.10.3 — Correctif : téléchargements fluides
- **Bug corrigé (TCP)** : en lisant, `tcp_recv` ne prévenait jamais le serveur que la place avait été libérée. Dès que le tampon de 8 Kio était plein
  (page lourde, comme GitHub), le serveur attendait ses sondes lentes et la page arrivait « bout par bout » sans jamais finir. Maintenant une mise à jour de
  fenêtre part dès qu'il y a de nouveau deux segments de place. Test `tcp` : un serveur qui respecte la fenêtre envoie 30 000 octets (échouait avant).
- **`web` décode les réponses « chunked »** (GitHub en envoie même pour HTTP/1.0) : plus de lignes « 8000 » parasites dans la page ; sortie groupée (un appel système par bloc).
- **TLS** : ChaCha20-Poly1305 est proposé en premier (notre AES-GCM logiciel est lent) ; les serveurs qui respectent la préférence du client l'utilisent.
- Observé chez vous en 0.10.2 : HTTPS fonctionne sur example.org et github.com (certificats vérifiés, générateur aléatoire prêt).
- Feuille de route décalée d'un cran : 0.10.4 mises à jour signées, 0.10.5 bibliothèques relogeables.

## 0.10.2 — TLS 1.3 client, certificats X.509 et HTTPS
- **TLS 1.3 client** (`src/tls.c`) : suites TLS_AES_128_GCM_SHA256, TLS_AES_256_GCM_SHA384, TLS_CHACHA20_POLY1305_SHA256 ; échange x25519 ;
  vérification de CertificateVerify (ECDSA P-256/P-384, RSA-PSS, Ed25519) et de Finished ; KeyUpdate pris en charge.
- **Signatures à clé publique** (`src/pubkey.c`) : RSA PKCS#1 v1.5 et PSS (2048 à 4096 bits exigés pour les certificats), ECDSA P-256 et P-384.
- **Certificats X.509** (`src/x509.c`) : lecture DER, nom d'hôte (SAN, jokers, adresse IPv4), chaîne jusqu'à une racine, contraintes de base,
  longueur de chemin, usages de clé, serverAuth, extensions critiques inconnues refusées, SHA-1 et RSA < 2048 refusés, dates vérifiées.
  **40 autorités racines** intégrées (`src/roots.c`, régénérables par `tools/gen_roots.py`).
- **Générateur aléatoire** (`src/rng.c`) : RDRAND, gigue du compteur de cycles, événements clavier et paquets réseau ; TLS refuse de partir tant qu'il n'a pas assez d'entropie.
- **Appels système** 208 à 212 : `tls_connect`, `tls_send`, `tls_recv`, `tls_close`, `tls_error` ; **`web https://hote/chemin`** ; erreurs `E_TLS` (-23) et `E_CERT` (-24) avec message en français.
- **Limites** : une seule session TLS dans tout le système ; TLS 1.3 seulement (pas de 1.2, pas de HelloRetryRequest : serveur sans x25519 refusé) ;
  pas de certificat client, pas de reprise de session, pas de vérification de révocation (CRL/OCSP) ; l'horloge matérielle est l'heure locale sans fuseau
  (les certificats exigent une date ≥ 2025-01-01) ; liste de racines figée à la version ; qualité d'entropie sous émulateur non garantie.
- Tests : vecteurs RSA/ECDSA/X.509 générés avec Python `cryptography`, poignées de main réelles contre OpenSSL (toutes clés et suites, KeyUpdate),
  11 serveurs tricheurs refusés, contrôles par mutation. Réel (internet, boot) à valider chez vous : `make serial`, puis `web https://example.org`.
- Étapes suivantes : 0.10.3 mises à jour signées ; ensuite passage en 64 bits (UEFI) et noyau mi-C mi-Rust (décidés le 19 oct. 2026).

## 0.10.1 — Briques de chiffrement (base de TLS et des mises à jour signées)
- **SHA-256, SHA-384, SHA-512** (`src/sha2.c`), en un coup ou par morceaux ; **HMAC** et **HKDF** (SHA-256 ou SHA-384 choisi à l'exécution).
- **ChaCha20-Poly1305** et **AES-128/256-GCM** (`src/aead.c`) : chiffrement authentifié ; une étiquette fausse rend `-1` sans rien déchiffrer.
- **X25519** (échange de clés) et **Ed25519** (signatures, avec refus des signatures malleables S ≥ L) dans `src/c25519.c`.
- **Commande `sha256 <fichier...>`** : empreinte d'un fichier (page de manuel `man sha256`).
- Interface unique dans `src/crypto.h` ; `u64`/`i64` ajoutés à `types.h`. Pas de division 64 bits (le noyau n'a pas libgcc).
- **Limites** : AES lit sa table par indice (pas à temps constant face à un attaquant sur le même processeur, sans objet ici pour l'instant) ;
  GHASH calculé bit à bit (lent, sûr) ; pas de générateur aléatoire encore (nécessaire pour la prochaine étape, TLS) ; pas d'AES-192.
- Tests : `test_crypto` contre des vecteurs générés par la bibliothèque Python `cryptography` (hashlib pour SHA/HMAC), plus altérations ;
  contrôle par mutation : changer une constante ou une retenue fait échouer le test. Générateur : `tests/gen_crypto_vectors.py`.
- Étapes suivantes : 0.10.2 TLS 1.3 client + certificats + HTTPS, 0.10.3 mises à jour signées.

## 0.10.0 — TCP client pour les programmes
- **TCP** (`src/tcp.c`) : connexions sortantes (client), numéros de séquence modulo 2³², somme de contrôle avec pseudo-en-tête,
  taille maximale de segment négociée (1460 octets), réémission après 1 s sans accusé, délais : 10 s pour ouvrir, 8 s pour envoyer,
  15 s pour recevoir. Fermeture propre (FIN) ; une connexion coupée par le serveur (RST) rend `E_RESET`, un refus `E_REFUSED`.
- **Appels système** 204 à 207 : `sock_connect(hôte, port)` (nom ou adresse), `sock_send`, `sock_recv`, `sock_close`.
  Bibliothèque `user/phoenos.h` mise à jour. Les connexions d'un programme sont fermées à sa fin.
- **Programme `web`** : `web <hôte> [chemin]` récupère une page en HTTP/1.0 (port 80, sans HTTPS).
- **Pile réseau** : `net_set_config` (adresse fixe, pour les tests), `net_ip_send`, `net_pseudo_checksum` exposés à TCP ;
  codes d'erreur `E_REFUSED` (-21) et `E_RESET` (-22).
- **Limites** : client seulement (pas d'écoute), 4 connexions à la fois dans tout le système, un segment en attente à la fois
  (plus lent qu'une vraie pile), tampon de réception de 8 Kio, données hors ordre ignorées, pas de TIME_WAIT, pas de HTTPS.
- Tests : `test_tcp` (faux serveur branché sur la vraie pile : écho, segments multiples, refus, muet, fermeture par le serveur,
  réponse abîmée puis réémission, trame falsifiée ignorée, propriétaires, table pleine, délai) ; `test_syscall` (pointeurs hostiles
  et arguments invalides des appels 204 à 207).
- Pas encore testé dans QEMU ici : `web example.org` est à vérifier sur votre machine (`make serial`).

## 0.9.1 — Cache DNS
- **Cache DNS** (`src/net.c`) : 8 noms, durée de vie donnée par le serveur (3600 s au plus, 0 = pas de cache), casse et point final ignorés,
  les échecs ne sont pas retenus, vidé à chaque nouvelle adresse DHCP. `dns` marque « (cache) », `dns -l` liste le cache et les compteurs,
  `dns -f` le vide. `ping nom` en profite aussi.
- Tests : `test_net` (réutilisation, expiration, casse, remplacement au-delà de 8 noms, vidage), `test_shell` (`dns -l`, `dns -f`).

## 0.9.0 — Premier pilote officiel (réseau filaire e1000) et pile réseau
- **Bus PCI** (`src/pci.c`) : exploration au démarrage, commande `lspci`. **Cadre des pilotes** (`src/driver.c`) : un pilote
  déclare ses identifiants PCI ; officiels (PhoenOS) ou, plus tard, communautaires (à leurs risques). Commande `drivers`.
- **Pilote e1000** (Intel PRO/1000, carte par défaut de QEMU et VirtualBox) : registres MMIO mappés dans le noyau
  (`paging_map_range`, `paging_map_mmio`), anneaux DMA de 32 (réception) et 16 (émission) descripteurs, sans interruption.
- **Pile réseau** (`src/net.c`) : Ethernet, ARP (cache), IPv4 (fragments refusés), ICMP (réponse + `ping`), UDP, client **DHCP**,
  résolveur **DNS** (compression des noms, réponses piégées refusées). Tâche noyau « net » : adresse obtenue au démarrage sans le retarder.
- Commandes `lspci`, `drivers`, `net [up]`, `ping`, `dns` ; pages de manuel et sujet `reseau`. `make run/serial/debug` : `-nic user,model=e1000`.
- Pas encore : TCP (prévu pour les mises à jour 0.10.0), IPv6, Wi-Fi (pilote à part, il faut connaître la puce exacte).
- Tests : `test_net` (fausse carte e1000 + faux réseau : ARP, ping, DHCP avec offre au mauvais identifiant, DNS avec CNAME/NXDOMAIN/piège,
  trames tronquées, fausses sommes, fragments), `test_paging` (plage de matériel), `test_shell` (commandes réseau).

## 0.8.0 — Applications .poe complètes et bibliothèques partagées .psl
- **Format .poe version 2** (en-tête de 208 octets) : en plus de nom/version/auteur, une description et jusqu'à 4
  bibliothèques `.psl` nécessaires. Le format 1 reste lu et créé tel quel ; niveau d'API 2 (`POE_API_LEVEL`).
- **Créer des .poe** : commandes `poepack` (programme → .poe) et `poeunpack` (.poe → programme) dans PhoenOS ;
  `mkpoe` accepte `-d` et `-l`. Les deux utilisent le même code (`poe_build`), qui refuse noms invalides, doublons, ELF faux.
- **Bibliothèques partagées `.psl`** (`/lib/<nom>.psl`, `src/psl.c`, `src/libmgr.c`) : ELF lié à une adresse fixe dans la zone
  0x700000-0x7F0000, avec table d'exports. Le **code est en un seul exemplaire** en mémoire, partagé en lecture seule par tous les
  programmes (`aspace_map_shared`, cadres jamais rendus par `aspace_unmap/destroy`) ; les **données sont privées** à chaque
  programme ; tout est rendu quand le dernier programme s'arrête. Appel système `psl` (203) : adresse d'un symbole,
  chargement à la demande ; `exe` vérifie les bibliothèques annoncées avant de lancer.
- Commandes `psl` (liste / détail / état en mémoire) et `pslpack` (ELF → .psl, exports cherchés dans la table des symboles) ;
  outil `mkpsl` ; `poeinfo` montre description, format et bibliothèques (présentes ou non).
- Exemples : bibliothèques `maths` et `texte` (`lib/`), programme `utilib` (application .poe qui les déclare).
- Le tas d'un programme s'arrête maintenant à 0x6FF000 (avant : sous la pile) pour laisser la zone des bibliothèques.
- Code d'erreur `E_NOLIB` (-20). `user/phoenos.h` : `psl_sym`, `psl_addr`.
- Tests : `test_libs` (vrais ELF liés par `ld` à leur adresse, emballés par le vrai `mkpsl`, **code exécuté** pour de vrai :
  partage des cadres, données privées, conflits de plage, mémoire pleine, 30000 en-têtes abîmés, aucune fuite), `test_poe` (format 2,
  `poe_build`, 20000 en-têtes abîmés), `test_user` (bibliothèques par en-tête et à la demande, échec propre), `test_shell`
  (poepack, poeunpack, pslpack, psl, poeinfo, exe avec bibliothèque absente).

## 0.7.1 — Ctrl+C arrête aussi les enfants
- Corrigé (constaté sur `lance boucle 10`) : Ctrl+C arrêtait `lance` mais son enfant `boucle` continuait seul jusqu'au bout.
  Ctrl+C au premier plan et la commande `kill` arrêtent maintenant le programme **et tous ses descendants**
  (`sched_kill_tree`, `user_kill_tree`) ; l'appel système `kill(pid)` reste limité à `pid`.
- Test : arbre parent → enfant sans fin + enfant qui a lui-même trois enfants, tout s'arrête, aucune page ne fuit.

## 0.7.0 — Programmes complets
- Appels système `spawn` (202), `waitpid` (7 ; pid, -1, `WNOHANG`), `kill` (37), `brk` (45), `getppid` (64),
  `yield` (158). Lien parent/enfant dans l'ordonnanceur (`parent`, `sched_children`, `sched_orphan`) : un enfant
  non détaché est libéré par `waitpid` ; un parent qui s'arrête détache ses enfants.
- Tas par programme : `brk` mappe/démappe des pages (`aspace_unmap`), de la fin du programme jusqu'à la pile
  moins une page de garde ; refus propre si la mémoire manque (rien ne change).
- Ordonnanceur : 16 tâches (15 programmes) ; `ps` affiche la colonne PERE ; commande `wait <numéro>` du shell
  (codes de sortie des 32 derniers programmes retenus).
- Programmes : `lance`, `essaim`, `tas` ; bibliothèque `user/libc.h` (malloc/free/calloc/realloc, printf, snprintf,
  qsort, textes, `err_text`).
- Codes d'erreur `E_CHILD` (-18), `E_INTR` (-19).
- Tests : `test_user` (vrai `user.c` + ordonnanceur : spawn, waitpid pid/-1/WNOHANG, orphelins, kill, brk limites et mémoire
  épuisée, table pleine, aucune fuite de pages), appels système (spawn/waitpid/kill transmis, pointeurs hostiles), shell (`wait`, `ps`),
  `tas` et `libc.h` exécutés pour de vrai sous Linux 32 bits.

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
