# Manuel de PhoenOS 0.10.1

> Généré à partir de `initrd/docs/manuel.txt` (lisible dans PhoenOS avec `man`).

```
Tapez  man            pour la liste des sujets,
       man <sujet>    pour lire un sujet (exemple : man ls).
Ce fichier est /docs/manuel.txt ; le meme texte est dans MANUEL.md du projet.

Les commandes de PhoenOS ressemblent a celles d'Unix mais ne sont PAS identiques :
chaque page indique ce qui differe. Les options se placent juste apres le nom de la
commande, une lettre par option, et se combinent (-rp = -r -p). Une option inconnue
affiche la ligne "usage". Pas d'options longues (--exemple).

== general  - ligne de commande, touches, redirection
Saisie
  Fleches haut/bas   historique (16 commandes) ; la saisie en cours est conservee
  Tab                complete une commande (1er mot) ou un chemin (mots suivants)
  Ctrl+C             abandonne la ligne        Ctrl+U   l'efface
  Ctrl+L             efface l'ecran, garde la ligne
  PageHaut/PageBas   remontent / redescendent dans l'historique de l'ecran (500 lignes,
                     12 par appui) ; toute autre touche, ou toute sortie, ramene en bas
  Sans ces touches (Chromebook...) : Ctrl+Haut / Ctrl+Bas, Alt+Haut / Alt+Bas,
                     ou Ctrl+B (remonte) / Ctrl+F (descend) font la meme chose
  Texte long : man ls | more   (lit page par page : Espace, Entree, q)
Console serie (sans fenetre)
  Avec "make serial" (ou "-serial stdio" sous QEMU), le terminal ou tourne QEMU sert de
  clavier ET d'ecran : tout ce que PhoenOS affiche y est recopie, et les touches tapees dans
  ce terminal sont lues comme celles du clavier (Entree, Retour arriere, Tab, fleches,
  PageHaut/Bas, Suppr, Ctrl+C/L/U, lettres accentuees). "make serial" : Ctrl+A puis X quitte QEMU.
  Dans ce mode le defilement de l'ecran est celui de votre terminal (molette, Maj+PageHaut).
Guillemets
  "a  b" regroupe plusieurs mots en un seul argument : echo "a   b" c
  Maximum 16 arguments par commande (tubes compris), 32 mots par ligne.
Plusieurs commandes sur une ligne (0.6.3)
  cmd1 ; cmd2          l'une apres l'autre (un echec n'arrete pas la suite)
  cmd1 & cmd2 &        les deux en arriere-plan, sans copier-coller : ticker A 5 400 & ticker B 5 300 &
  cmd1 & cmd2          cmd1 en arriere-plan, cmd2 au premier plan
  ; et & peuvent etre colles aux mots (ls;pwd) ; entre guillemets ce sont de simples lettres : echo "a;b"
Redirection
  commande > fichier     ecrit la sortie dans le fichier (cree ou remplace)
  commande >> fichier    ajoute la sortie a la fin du fichier
  Le ">" doit etre l'avant-dernier mot de la ligne ; entre guillemets il reste du texte.
  Les messages d'erreur restent a l'ecran et ne vont jamais dans le fichier.
Tubes
  cmd1 | cmd2 | cmd3     la sortie de chaque commande devient l'entree de la suivante
  Le "|" doit etre un mot a part, entoure d'espaces ("a|b" est un seul mot) ; entre
  guillemets il reste du texte. 4 commandes reliees au plus. Chaque sortie intermediaire
  est gardee en memoire (128 Kio au plus). Les erreurs restent a l'ecran.
  Lisent un tube : cat, head, tail, wc, grep, sort. Exemple : ls -l / | grep docs | wc
  Pas de redirection d'entree (<). Le ">" ne s'ecrit qu'en fin de ligne : a | b > f
Chemins
  /a/b est absolu ; a/b est relatif au dossier courant (affiche dans le prompt).
  "." = ici, ".." = dossier parent. "~" n'existe pas : "cd" seul ramene a la racine /.
  Noms : 31 caracteres maximum ; chemins : 255 ; majuscules et minuscules comptent :
  "A" et "a" sont deux noms differents.
Differences avec Unix : pas de droits ni de proprietaires (les dates existent, voir "date") ; pas de fichiers
  caches ; pas de jokers (*, ?) ; tout est en memoire (voir "fichiers").

== fichiers  - le systeme de fichiers en RAM
  Au demarrage, GRUB charge l'initrd (dossier initrd/ du projet) dans le systeme de
  fichiers. Les fichiers vivent en memoire : sans disque, ils sont PERDUS a l'extinction.
  Avec un disque PhoenOS (voir "stockage"), ils sont enregistres puis relus au demarrage.
  Limites : fichiers 1 Mio, 4096 elements, 32 niveaux de dossiers.
  Chaque fichier et dossier garde sa date de modification (ls -l), enregistree aussi dans
  la sauvegarde disque. Heure locale lue sur l'horloge de la machine, sans fuseaux horaires.

== stockage  - disque et sauvegarde
  PhoenOS peut garder vos fichiers sur un disque ATA/IDE (QEMU : disk.img, cree par
  "make run" ; VirtualBox : ajouter un disque IDE).
  Mise en route : rien a faire. Un disque VIERGE est prepare automatiquement (au demarrage,
  ou au premier save). Un disque qui contient deja autre chose n'est jamais touche : il faut
  alors taper  format oui oui  pour l'effacer et l'utiliser (voir man format).
  Ensuite : save enregistre, load relit ; shutdown, reboot et halt enregistrent tout seuls
  les modifications. Au demarrage, la derniere sauvegarde est relue AUTOMATIQUEMENT
  par-dessus les fichiers de l'initrd (vos fichiers gagnent sur ceux de l'initrd).
  Consequence : apres une mise a jour de PhoenOS, un fichier de l'initrd que vous aviez
  modifie ou enregistre garde son ancienne version ; "make newdisk" repart d'un disque vide.
  Un fichier SUPPRIME qui venait de l'initrd reapparait au demarrage suivant.
  Securite : la sauvegarde alterne entre deux emplacements du disque (A et B) et ne valide
  le nouveau qu'une fois entierement ecrit ; une coupure de courant pendant l'ecriture
  laisse l'ancienne sauvegarde intacte. Chaque sauvegarde est protegee par une somme de
  controle (CRC32) ; si la plus recente est abimee, la precedente est utilisee.
  Limites : 8 Mio de fichiers au plus (tar), disques jusqu'a 128 Gio, un seul disque,
  pas de partitions (le disque entier est reserve a PhoenOS).

== programmes  - programmes utilisateur (mode protege, ring 3)
  Depuis la 0.1.0, PhoenOS lance de vrais programmes, separes du noyau. Ce sont des fichiers
  ELF 32 bits statiques (dossier user/ du projet, compiles par "make iso") places dans /bin.
  Taper le nom d'un programme le lance : hello a b    primes 100    ls /bin
  "run <chemin> [args]" lance un programme situe ailleurs. Une commande integree (ls, cat...)
  a toujours priorite sur un programme de meme nom.
  Programmes fournis :
    hello    affiche ses arguments et son numero de processus
    primes   nombres premiers jusqu'a N (primes 100)
    saisie   demande un nom (clavier ou tube : echo Zoe | saisie) et dit bonjour
    compte   lignes et octets de fichiers (compte /etc/motd)
    heure    heure machine (secondes depuis 1970), duree depuis le demarrage, pause
    faute    provoque une erreur (gp | div | ud | io | kern | nul | code | vide) : le noyau arrete le programme
    calc     calculatrice (calc 2+3*4, ou sans argument : mode interactif ; man calc)
    copie    copie un fichier (copie a.txt b.txt)
    notes    carnet de notes dans /notes.txt (notes acheter du pain ; notes ; notes -v)
    ticker   compte avec des pauses (ticker A 5 400 &)       boucle   calcul sans pause (boucle 5 &)
  Ce que le noyau garantit : un programme n'a le droit ni de couper les interruptions, ni
  d'acceder aux ports, ni d'executer une instruction privilegiee ; en cas de faute il est
  arrete avec un message (adresse, exception) et vous revenez au shell. Ctrl+C arrete un
  programme qui attend une saisie. Les programmes marchent dans les tubes et les redirections :
  primes 50 > p.txt    echo Zoe | saisie.
  Memoire : depuis la 0.5.0 chaque programme a SON espace d'adressage (son propre repertoire
  de pages). Il voit ses pages a partir de 4 Mio ; le noyau lui donne uniquement ce dont il a
  besoin (son code, ses donnees, 64 Kio de pile) et reprend tout a la fin. Le code du programme
  est en lecture seule. Toute autre adresse l'arrete aussitot : le noyau (faute kern), l'adresse 0
  (faute nul), une page non attribuee (faute vide), son propre code (faute code). Le noyau et le
  shell restent intacts.
  Limite : 4 Mio d'adresses par programme (code, donnees, tas, bibliotheques, pile). Plusieurs programmes tournent
  ensemble (man multitache) et peuvent en lancer d'autres (man processus).
  Depuis la 0.4.0 un programme peut aussi CREER, ECRIRE et SUPPRIMER des fichiers (open avec
  options, write, unlink). Les ecritures vont toujours en fin de fichier. Pour garder ces
  fichiers apres un redemarrage : save (voir man stockage).
  Appels systeme (int 0x80 : numero dans eax, arguments dans ebx, ecx, edx, resultat dans eax ;
  negatif = erreur) : exit 1, read 3, write 4, open 5 (options O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, O_APPEND), close 6, unlink 10, time 13,
  getpid 20, sleep_ms 200, uptime_ms 201 ; depuis la 0.7.0 : waitpid 7, kill 37, brk 45, getppid 64,
  yield 158, spawn 202 (man processus) ; depuis la 0.8.0 : psl 203 (man bibliotheques). Seuls sleep_ms, uptime_ms, log (199),
  spawn et psl sont propres a PhoenOS.
  Ecrire un programme : user/phoenos.h (mini-bibliotheque), user/hello.c (exemple) ; tout
  fichier user/nom.c devient /bin/nom. Code de sortie : main renvoie un entier (0 = succes) ;
  un code non nul est signale par le shell.

== help  - rappel rapide d'une commande
  Syntaxe : help [commande]
  Sans argument : liste toutes les commandes par groupe. Avec un nom : sa syntaxe en
  une ligne. Pour les details et exemples : man.

== man  - ce manuel
  Syntaxe : man [sujet]
  Sans argument : liste des sujets. Avec un sujet : sa page.
  Lit /docs/manuel.txt : si vous le modifiez (cat, echo >>), man suit.

== clear  - efface l'ecran
  Syntaxe : clear        (Ctrl+L fait pareil sans perdre la ligne en cours)

== version  - version et date de compilation
  Syntaxe : version

== uptime  - temps depuis le demarrage
  Syntaxe : uptime
  Affiche heures, minutes, secondes et le nombre de ticks (100 par seconde).

== mem  - etat de la memoire
  Syntaxe : mem
  Memoire physique detectee / libre, etat de la pagination (memoire mappee, taille des tables),
  taille du tas du noyau, blocs utilises et libres.
  Pagination : toute la RAM (1 Gio au plus) est visible du noyau seul ; la page 0 est absente
  (un pointeur nul fait une faute au lieu de passer) ; le code et les constantes du noyau sont
  en lecture seule ; seule la zone des programmes est accessible en mode utilisateur.

== memtest  - teste le tas
  Syntaxe : memtest [Kio]
  Alloue un bloc (64 Kio par defaut, de 1 a 4096), y ecrit 4 motifs (00, FF, AA, 55), les
  relit, verifie l'integrite du tas et mesure le temps. Exemple : memtest 1024
  Specifique a PhoenOS (pas d'equivalent direct sous Unix).

== date  - date et heure de la machine
  Syntaxe : date [-s]
  Options :
    -s   affiche les secondes ecoulees depuis le 1er janvier 1970 au lieu de la date
  Lit l'horloge materielle (CMOS). Heure locale telle que reglee dans la machine ;
  sous QEMU, "make run" utilise l'heure de votre ordinateur. On ne peut pas la regler.
  Si l'horloge est illisible, un message l'indique et les fichiers n'ont pas de date.
  Exemple : date  ->  lundi 5 octobre 2026, 07:31:12

== run  - lance un programme utilisateur
  Syntaxe : run <programme> [arguments...]
  Lance le fichier ELF indique (nom seul : cherche dans /bin). Equivalent de taper le nom d'un
  programme de /bin, mais fonctionne aussi pour un chemin quelconque : run /home/prog a b.
  Message d'erreur "pas un programme PhoenOS valide" si le fichier n'est pas un ELF 32 bits statique.
  Voir "man programmes".  Exemple : run hello monde

== ps  - liste les programmes en cours
  Syntaxe : ps
  Montre le shell (numero 0) et chaque programme lance (numero, PERE = numero du programme qui l'a
  lance (0 : le shell, ou orphelin), etat, tours, nom).
  Etats : pret (attend son tour), actif (sur le processeur), attend (dort, ou attend le clavier),
  fini (termine, pas encore ramasse). TOURS = nombre de fois que la minuterie l'a interrompu
  pendant qu'il calculait. "(arriere-plan)" : lance avec &. Voir "man multitache".

== wait  - attend la fin d'un programme en arriere-plan (0.7.0)
  Syntaxe : wait <numero>
  Bloque le shell jusqu'a la fin du programme (numero donne par ps ou par "[n] ... en arriere-plan"),
  puis affiche son code de sortie : "[3] termine, code 0". Ctrl+C arrete l'attente (pas le programme).
  Si le programme est deja fini, le code est quand meme rappele (les 32 derniers sont retenus).
  Exemple : ticker A 3 300 &   puis   wait 1

== schedlog  - journal de l'ordonnanceur
  Syntaxe : schedlog
  Diagnostic : liste les 128 derniers evenements du multitache avec l'heure (en ticks de 10 ms) :
  S a->b commutation de la tache a vers la tache b, B une tache se bloque, W elle repart, X elle se
  termine. Sert a comprendre un comportement inattendu (voir "man multitache").

== kill  - arrete un programme
  Syntaxe : kill <numero>
  Arrete le programme dont ps donne le numero ET tous ceux qu'il a lances (ses enfants, petits-enfants...).
  Ctrl+C au premier plan fait de meme. Il s'arrete des qu'il reprend la main (tout de suite
  s'il dort ou attend, au prochain tick de 10 ms s'il calcule). Le shell (numero 0) ne s'arrete pas.
  Exemple : boucle 60 &   puis   kill 1

== multitache  - plusieurs programmes en meme temps (0.6.0)
  Un programme lance normalement garde le clavier : le shell attend sa fin (Ctrl+C l'arrete).
  Ajoutez & en fin de ligne pour le lancer en ARRIERE-PLAN : le shell rend la main aussitot.
    ticker A 5 400 &      ticker B 5 300 &      ps      kill 2
  Plusieurs sur une seule ligne : ticker A 5 400 & ticker B 5 300 & ps   (voir "man general")
  Ce qui change pour un programme en arriere-plan :
   - il n'a pas de clavier : lire l'entree standard donne tout de suite la fin de fichier ;
   - il affiche sur le meme ecran, mais sans casser ce que vous tapez (0.6.3) : la ligne en cours est
     effacee puis redessinee sous sa sortie (sauf si elle depasse une ligne d'ecran) ; a sa fin le noyau
     ecrit "[numero] nom termine" ;
   - & ne se combine pas avec les tubes (|) ni les redirections (>) ; pas de & pour les commandes
     integrees (seulement run, exe ou un nom de programme).
  Ordonnanceur : tourniquet, au plus 15 programmes en meme temps, 16 Kio de pile noyau chacun.
  La minuterie (100 Hz) interrompt un programme qui calcule pour donner la main au suivant ;
  quand un programme dort (sleep_ms) ou attend, les autres tournent. Chaque programme a son propre
  espace d'adresses : aucun ne peut lire ou ecrire la memoire d'un autre.
  Limite : le noyau lui-meme n'est pas interruptible (une commande du shell va jusqu'au bout).
  Programmes de demonstration : ticker (compte avec pauses) et boucle (calcul sans pause).

== processus  - programmes qui en lancent d'autres, tas (0.7.0)
  Un programme peut lancer d'autres programmes (ses ENFANTS) et attendre leur fin :
    pid = spawn("ticker", argv);        lance /bin/ticker (argv : textes termines par 0 ; argv[0] = nom)
    waitpid(pid, &code, 0);             attend cet enfant ; code = son code de sortie
    waitpid(-1, &code, 0);              attend le PREMIER enfant qui finit (renvoie son numero)
    waitpid(pid, &code, WNOHANG);       ne bloque pas : 0 s'il tourne encore
    kill(pid, 15);  yield();  getppid();
  Arret : Ctrl+C et la commande kill arretent le programme et tous ses descendants ; l'appel systeme kill(pid)
  n'arrete que `pid` (ses enfants deviennent alors orphelins).
  Regles : un enfant n'a pas de clavier (lire l'entree donne la fin de fichier), s'affiche sur le meme
  ecran, et ne s'attend qu'une fois (ensuite waitpid renvoie -18 : pas un de vos enfants). Si le parent
  s'arrete avant ses enfants, ceux-ci continuent seuls (PERE 0 dans ps) et sont liberes a leur fin.
  Erreurs de spawn : -3 fichier introuvable, -5 trop de programmes (15 au plus), -8 trop d'arguments
  (16 mots, 4 Kio), -16 pas un programme, -17 pointeur invalide, -12 c'est un dossier.
  Le TAS : brk(0) donne la fin du tas, brk(adresse) la deplace (de 0 a ~4 Mio apres le programme, une
  page de garde avant la pile). user/libc.h fournit malloc, free, calloc, realloc, printf, snprintf,
  qsort et les fonctions de textes par-dessus brk. Demonstrations : lance, essaim, tas.
  Numeros d'appels : waitpid 7, kill 37, brk 45 (Linux), getppid 64, yield 158 (Linux), spawn 202.
  Differences avec Linux : pas de fork/exec (spawn fait les deux), le "statut" de waitpid est le code
  de sortie lui-meme (pas code*256), le signal de kill est ignore (le programme est toujours arrete).

== lance  - lance un programme comme enfant et l'attend (0.7.0)
  Syntaxe : lance <programme> [arguments...]
  Lance le programme (nom de /bin ou chemin) avec spawn, attend sa fin avec waitpid, puis affiche
  son code de sortie et la duree. Exemple : lance primes 50      lance ticker A 3 200

== essaim  - plusieurs enfants a la fois (0.7.0)
  Syntaxe : essaim [enfants 1-8] [fois 1-100] [ms 10-5000]
  Lance N ticker en meme temps (le dernier lance est le plus rapide) puis les attend avec waitpid(-1) :
  ils sont annonces dans l'ordre de leur FIN, pas de leur lancement. Defaut : 4 enfants, 3 fois, 100 ms.
  Exemple : essaim 3 4 150      puis, pendant qu'il tourne :  ps  (colonne PERE)

== tas  - demonstration du tas : malloc, free, realloc (0.7.0)
  Syntaxe : tas [blocs]
  Alloue une liste de N blocs (defaut 1000, max 20000), la parcourt, la libere, cree des trous qu'il
  reutilise, agrandit un tableau par realloc et le trie. Affiche a chaque etape la memoire prise au noyau
  par brk et la memoire utilisee : on voit le tas grandir, se fragmenter puis etre rendu. Voir man processus.

== ticker  - compte avec des pauses
  Syntaxe : ticker <nom> [fois] [ms]
  Affiche "nom 1/N", "nom 2/N"... avec une pause de <ms> millisecondes entre deux lignes (defaut :
  5 fois, 500 ms). Fait pour etre lance plusieurs fois avec & : ticker A 5 400 & ticker B 5 300 &

== boucle  - occupe le processeur
  Syntaxe : boucle [secondes]
  Calcule sans arret pendant N secondes (defaut 3, max 600), sans aucun appel au noyau, et affiche
  "boucle <numero> : 1 s", "2 s"... Preuve de la preemption : le shell reste utilisable et
  deux boucle & avancent ensemble. Ctrl+C (au premier plan) ou kill l'arrete.

== sleep  - attend
  Syntaxe : sleep <ms>
  Difference : l'argument est en MILLISECONDES (pas en secondes). Maximum 60000.
  Exemple : sleep 1500

== colors  - les 16 couleurs du mode texte
  Syntaxe : colors

== dmesg  - journal du noyau
  Syntaxe : dmesg
  Rejoue tout ce que le noyau a ecrit depuis le demarrage (les 8 derniers Kio).
  La sortie ne peut pas etre redirigee vers un fichier pour l'instant.

== log  - journal de debogage du systeme (0.6.2)
  Syntaxe : log [-a | -n N] [-t tag] [-l niveau]   affiche les N dernieres lignes (20 par defaut ; -a : toutes)
            log level [niveau]                     montre ou change le niveau minimal enregistre
            log clear                              vide le journal
            log save [fichier]                     ecrit le journal dans un fichier (/var/log/phoenos.log)
            log add <texte>                        ajoute une ligne de votre cru (tag "user")
  Niveaux : trace, debug, info, warn, error (ou 0 a 4). Rien n'est affiche a l'ecran tout seul : le systeme
  note en silence le demarrage, chaque commande tapee, chaque touche (trace), les programmes lances,
  termines ou fautifs, les appels systeme, l'ordonnanceur et un battement toutes les 5 s (tag "tick").
  Un programme peut ecrire dans le meme journal avec l'appel systeme log() (tag = nom du programme).
  Exemples : log -t ticker     log -l warn -a     log save   puis   cat /var/log/phoenos.log
  Hors de l'OS : "make debug" garde tout dans logs/latest/ (os.log, qemu.log, console.log, session.log).

== history  - commandes precedentes
  Syntaxe : history
  Les 16 dernieres, de la plus ancienne a la plus recente. Utilisez les fleches pour les rappeler.

== crash  - provoque une erreur (pour voir l'ecran d'erreur)
  Syntaxe : crash <type>
  Types : div0 (division par zero), gp (faute de protection), ud (instruction invalide),
          null (ecriture a l'adresse 0), ro (ecriture dans le code du noyau, en lecture seule),
          panic, assert, doublefree (double liberation memoire), heap (tas corrompu)
  L'ecran d'erreur orange (ou violet selon la compilation) s'affiche ; redemarrez ensuite.
  Specifique a PhoenOS, pour tester la robustesse du noyau.

== reboot  - redemarre
  Syntaxe : reboot
  Enregistre d'abord les fichiers modifies si un disque PhoenOS est pret (message
  "Sauvegarde des fichiers sur le disque... ok").
  Sous "make run" (QEMU lance avec -no-reboot) cela quitte QEMU au lieu de redemarrer.

== shutdown  - eteint la machine
  Syntaxe : shutdown
  Enregistre d'abord les fichiers modifies si un disque PhoenOS est pret.
  Fonctionne sous QEMU et VirtualBox. Sur un vrai PC : arret du processeur seulement.

== halt  - arrete le processeur
  Syntaxe : halt
  Enregistre d'abord les fichiers modifies si un disque PhoenOS est pret.

== ls  - liste un dossier
  Syntaxe : ls [-l] [chemin]
  Options :
    -l   une ligne par element : type (d dossier, - fichier), taille en octets,
         date de modification (AAAA-MM-JJ HH:MM, "-" si inconnue), nom
  Les dossiers s'affichent en cyan avec un "/" final, tries par ordre alphabetique.
  Sans chemin : le dossier courant. Sur un fichier : affiche ce fichier.
  Differences : pas de -a ni -h ; la taille de -l est toujours en octets.
  Exemples : ls    ls -l /docs    ls etc

== cat  - affiche des fichiers
  Syntaxe : cat [-n] [fichier...]
  Options :
    -n   numerote les lignes (compteur continu sur tous les fichiers)
  A l'ecran, les octets de controle sont remplaces par "." ; vers un fichier (cat a > b),
  la copie est fidele. Sans fichier, cat copie l'entree d'un tube (ls | cat -n) ; jamais le clavier.
  Exemples : cat /etc/motd    cat -n /README.txt    cat a b > ab

== cd  - change de dossier
  Syntaxe : cd [dossier]
  Sans argument : retourne a la racine / (et non a un "dossier personnel").
  Exemples : cd /docs    cd ..    cd

== pwd  - dossier courant
  Syntaxe : pwd

== mkdir  - cree des dossiers
  Syntaxe : mkdir [-p] <dossier>...
  Options :
    -p   cree aussi les dossiers parents manquants ; pas d'erreur si le dossier existe
  Exemples : mkdir projets    mkdir -p a/b/c

== touch  - cree des fichiers vides
  Syntaxe : touch <fichier>...
  Difference : ne fait que creer (pas de dates a mettre a jour) ; un fichier existant
  n'est pas modifie.

== rm  - supprime
  Syntaxe : rm [-r] <chemin>...
  Options :
    -r   supprime aussi les dossiers, avec tout leur contenu
  Sans -r, un dossier donne l'erreur "est un dossier". Refus pour la racine et pour tout
  dossier qui contient le dossier courant ("ressource occupee"). Pas de -f ni -i : aucune
  confirmation, aucune corbeille. Exemples : rm a.txt    rm -r projets

== rmdir  - supprime des dossiers vides
  Syntaxe : rmdir <dossier>...
  Erreur "dossier non vide" sinon (utilisez rm -r).

== cp  - copie un fichier
  Syntaxe : cp <source> <destination>
  Si la destination est un dossier, le fichier y est copie sous le meme nom ; si c'est
  un fichier existant, il est remplace. Differences : fichiers seulement (pas de dossiers,
  pas de -r) ; copier un fichier sur lui-meme est refuse.
  Exemples : cp /hello.txt .    cp a.txt b.txt

== mv  - deplace ou renomme
  Syntaxe : mv <source> <destination>
  Destination = dossier existant : on y deplace. Sinon : renommage. Une destination qui
  existe deja donne "existe deja" (rien n'est ecrase, contrairement a Unix).
  Marche aussi pour les dossiers (jamais dans eux-memes).

== tree  - arborescence
  Syntaxe : tree [-d] [dossier]
  Options :
    -d   affiche les dossiers seulement
  Termine par le nombre de dossiers et de fichiers. Specifique : decoupage plus simple
  que l'outil Unix du meme nom.

== df  - occupation du systeme de fichiers
  Syntaxe : df
  Nombre de fichiers, de dossiers, octets de contenu et limites, puis l'etat du disque
  (aucun / non formate / pret, avec "modifications non enregistrees" le cas echeant).

== hexdump  - affiche un fichier en hexadecimal
  Syntaxe : hexdump <fichier> [octets]
  Affiche les 256 premiers octets par defaut (ou le nombre demande) : decalage, 16 octets
  en hexadecimal, puis le texte entre | |. Difference : pas d'options ; le nombre
  d'octets est un argument.
  Exemples : hexdump hello.txt    hexdump hello.txt 1000

== sha256  - empreinte SHA-256 de fichiers
  Syntaxe : sha256 <fichier...>
  Affiche, pour chaque fichier, son empreinte SHA-256 (64 caracteres hexadecimaux) puis son
  nom, comme la commande sha256sum de Linux. Deux fichiers identiques ont la meme empreinte ;
  le moindre octet different change tout. Sert a verifier un fichier et prepare les mises a jour signees.
  Exemples : sha256 hello.txt    sha256 a.txt b.txt

== disks  - disques et etat du stockage
  Syntaxe : disks
  Liste les peripheriques ATA trouves (canal, maitre/esclave, nom, taille en Mio) ; les
  lecteurs de CD sont indiques "non utilisable". Termine par l'etat du stockage PhoenOS :
  aucun disque / non formate / pret (generation, taille, modifications en attente).
  Voir aussi : stockage.

== format  - prepare le disque pour PhoenOS
  Syntaxe : format oui
  Ecrit une sauvegarde vide dans les deux emplacements du disque. Inutile sur un disque
  vierge (prepare tout seul depuis la 0.5.3) ; sert a EFFACER une sauvegarde ou a reprendre
  un disque deja utilise. Le mot "oui" est obligatoire (protection contre une faute de frappe).
  ATTENTION : sur un disque deja formate, EFFACE la sauvegarde existante.
  Si le premier secteur du disque contient deja autre chose que du PhoenFS, la commande
  refuse : "format oui oui" force l'effacement. Difference avec Unix : pas de choix de
  systeme de fichiers ni de partition ; le disque entier est utilise.

== save  - enregistre les fichiers sur le disque
  Syntaxe : save
  Ecrit tout le systeme de fichiers dans l'emplacement le plus ancien du disque, puis le
  valide. Sur un disque vierge, le prepare d'abord automatiquement. Erreur si les fichiers
  depassent 8 Mio, ou si le disque contient d'autres donnees (voir format).
  Pas d'equivalent direct sous Unix (sync ecrit des caches, pas une sauvegarde complete).

== load  - relit la sauvegarde du disque
  Syntaxe : load
  Relit la derniere sauvegarde valide PAR-DESSUS les fichiers actuels : les fichiers de meme
  nom sont remplaces, les autres restent (rien n'est supprime). Utile pour annuler des
  modifications non enregistrees d'un fichier (cat, puis load).

== echo  - affiche du texte
  Syntaxe : echo [-n] [texte...]
  Options :
    -n   pas de retour a la ligne final
  Pas d'interpretation de \n ni de $variables. Se combine avec la redirection :
  echo salut > a.txt    echo suite >> a.txt

== head  - premieres lignes
  Syntaxe : head [-N] [fichier]
  Affiche les N premieres lignes (10 par defaut). N s'ecrit collee au tiret : head -3 a.txt.
  Sans fichier, lit l'entree d'un tube. Difference : une seule source, pas de -n ni -c.
  Exemples : head -5 /README.txt    cat a | head -2

== tail  - dernieres lignes
  Syntaxe : tail [-N] [fichier]
  Affiche les N dernieres lignes (10 par defaut). Sans fichier, lit l'entree d'un tube.
  Difference : pas de -f ; N s'ecrit collee au tiret : tail -3 a.txt.
  Exemple : dmesg | tail -5

== wc  - compte lignes, mots et octets
  Syntaxe : wc [-lwc] [fichier...]
  Options :
    -l   nombre de lignes     -w   nombre de mots     -c   nombre d'octets
  Sans option : les trois. Plusieurs fichiers : une ligne par fichier puis "total".
  Sans fichier, lit l'entree d'un tube. Exemple : ls / | wc -l

== grep  - garde les lignes qui contiennent un motif
  Syntaxe : grep [-inv] <motif> [fichier...]
  Options :
    -i   ignore majuscules et minuscules (lettres non accentuees)
    -n   prefixe chaque ligne par son numero ("3:ligne")
    -v   inverse : garde les lignes qui NE contiennent PAS le motif
  Le motif est un texte simple, PAS une expression reguliere. Avec plusieurs fichiers, chaque
  ligne est precedee du nom du fichier. Sans fichier, lit l'entree d'un tube. Le code de
  retour ne distingue pas "aucune ligne trouvee".
  Exemples : grep -in erreur log.txt    ls -l / | grep docs

== sort  - trie les lignes
  Syntaxe : sort [-ru] [fichier]
  Options :
    -r   ordre inverse     -u   supprime les lignes identiques (apres tri)
  Tri octet par octet : les majuscules passent avant les minuscules. 16384 lignes au
  maximum. Sans fichier, lit l'entree d'un tube. Difference : pas de -n ni -k.
  Exemple : cat noms.txt | sort -u > propres.txt

== more  - lit un texte page par page
  Syntaxe : more [fichier]      ou      commande | more
  Affiche 23 lignes puis s'arrete avec l'invite "-- Suite --".
    Espace    page suivante          Entree   une ligne de plus
    q ou Ctrl+C   quitte
  Sans fichier, lit l'entree du tube. Exemple : man general | more
  Si la sortie est redirigee (> fichier) ou suivie d'une autre commande, more affiche tout.

== calc  - calculatrice (programme utilisateur)
  Syntaxe : calc <expression>     ou     calc   (mode interactif : une expression par ligne)
  Operateurs : + - * / % ^ (puissance entiere), parentheses, moins unaire.
  Fonctions : sqrt(x) abs(x).  Constantes : pi, ans (dernier resultat).
  Nombres decimaux avec "." ou ",", 4 decimales, valeurs jusqu'a 100 000 000 000.
  Calcul a virgule fixe (pas de flottants). Division par zero : message d'erreur.
  Exemples : calc 2+3*4 (14)   calc "(1+2)^3" (27)   calc sqrt(2)   echo 10/4 | calc
  En mode interactif : q (ou quit, exit, Ctrl+C) pour sortir.  Voir "man programmes".

== faute  - provoque une erreur dans un programme (pour tester la protection)
  Syntaxe : faute <type>   (types : gp | div | ud | io | kern | nul | code | vide)
  Types : gp (instruction privilegiee), div (division par zero), ud (instruction invalide),
          io (acces a un port), kern (ecriture dans la memoire du noyau), nul (adresse 0),
          code (ecriture dans le code du programme lui-meme : lecture seule),
          vide (lecture d'une page que le noyau n'a pas donnee au programme).
  Le noyau arrete uniquement le programme, affiche la cause et l'adresse, puis vous revenez
  au shell : rien d'autre n'est touche. Sans argument : message d'usage.
  Exemple : faute kern   puis   mem   (le noyau est intact)

== hello  - programme d'exemple
  Syntaxe : hello [mots...]
  Affiche ses arguments et son numero de processus. Voir "man programmes".

== primes  - nombres premiers
  Syntaxe : primes [N]
  Affiche les nombres premiers jusqu'a N. Exemple : primes 100   primes 50 > p.txt

== saisie  - demande un nom
  Syntaxe : saisie
  Lit une ligne (clavier ou tube) et dit bonjour. Exemple : echo Zoe | saisie

== compte  - lignes et octets de fichiers
  Syntaxe : compte <fichier...>
  Exemple : compte /etc/motd

== heure  - heure machine
  Syntaxe : heure
  Affiche l'heure machine (secondes depuis 1970), la duree depuis le demarrage, puis fait une pause de 200 ms.

== copie  - copie un fichier (programme utilisateur)
  Syntaxe : copie <source> <destination>
  Copie le contenu et ECRASE la destination si elle existe. Refuse de copier un fichier sur
  lui-meme. Le dossier de la destination doit exister. Exemple : copie /etc/motd /moi.txt
  Difference avec la commande cp : copie est un programme (ring 3) qui utilise open/read/write.

== notes  - carnet de notes (programme utilisateur)
  Syntaxe : notes <texte...>     ajoute une ligne a /notes.txt
            notes                affiche les notes, numerotees
            notes -v             vide le carnet
  Pour les garder apres un redemarrage : save.  Exemple : notes acheter du pain

== exe  - lance une application .poe
  Syntaxe : exe <application>[.poe] [arguments...]
  Une application .poe est un programme emballe avec une etiquette (nom, version, auteur,
  description, exigences, bibliotheques). exe la cherche telle quelle, avec ".poe", puis dans
  /apps. Avant de la lancer, PhoenOS verifie l'etiquette et la somme de controle, verifie que
  les bibliotheques .psl annoncees existent dans /lib et sont valides (sinon il le dit et ne
  lance rien), et refuse proprement ce qu'il ne sait pas encore faire (interface graphique,
  reseau, systeme trop ancien).
  Taper simplement le nom (calc) marche aussi si /apps/calc.poe existe.
  Voir aussi poeinfo, poepack, bibliotheques.  Exemple : exe calc 2+3      exe utilib

== poeinfo  - etiquette d'une application .poe
  Syntaxe : poeinfo <application>[.poe]
  Affiche nom, version, auteur, description, format (1 ou 2), bibliotheques necessaires (et si
  elles sont presentes), exigences, taille et etat (valide, abimee, trop recente...).
  Exemple : poeinfo calc      poeinfo utilib

== poepack  - cree une application .poe (0.8.0)
  Syntaxe : poepack -n nom -v version -a auteur [-d texte] [-l biblio]... [-g] [-r] <programme> <sortie.poe>
  Emballe un programme (fichier ELF ; un nom sans / est cherche dans /bin) dans une application
  .poe avec son etiquette. -d : une phrase de presentation (47 caracteres au plus). -l : une
  bibliotheque .psl dont l'application a besoin (jusqu'a 4, noms de /lib sans .psl) ; elles seront
  chargees automatiquement au lancement. -g / -r : l'application exige l'interface graphique / le reseau
  (PhoenOS la refusera tant que ce n'est pas disponible). Sans -d ni -l, le fichier est au format 1
  (96 octets d'en-tete) ; avec l'un des deux, au format 2 (208 octets). Les deux se lisent.
  Un fichier existant de meme nom est remplace. Rien n'est cree si une verification echoue.
  Exemples : poepack -n mon -v 1.0 -a Moi hello /apps/mon.poe
             poepack -n calcul -v 1.0 -a Moi -d "Calculs" -l maths /bin/hello /apps/calcul.poe
  Sur la machine de developpement, l'outil mkpoe (tools/mkpoe.c) fait la meme chose.

== poeunpack  - extrait le programme d'une application .poe (0.8.0)
  Syntaxe : poeunpack <application>[.poe] <sortie>
  Ecrit dans <sortie> le programme ELF contenu dans l'application (le contraire de poepack).
  Refuse un .poe tronque ou dont la somme de controle est fausse.  Exemple : poeunpack calc /calc.elf

== psl  - bibliotheques partagees (0.8.0)
  Syntaxe : psl [bibliotheque]
  Sans argument : liste les bibliotheques de /lib (nom, version, plage d'adresses, description) et
  celles qui sont chargees en memoire (nombre de programmes qui les utilisent, pages de code
  partagees). Avec un nom : detail (auteur, plage, taille, symboles exportes, etat). Voir man bibliotheques.
  Exemples : psl      psl maths

== pslpack  - cree une bibliotheque .psl (0.8.0)
  Syntaxe : pslpack -n nom -v version -a auteur [-d texte] -e symbole... <elf> <sortie.psl>
  Emballe un ELF lie pour la zone des bibliotheques (voir man bibliotheques) en .psl. Chaque -e
  nomme une fonction ou variable a exporter (cherchee dans la table des symboles de l'ELF, qui ne
  doit donc pas etre "strip" ; 16 au plus, 15 caracteres A-Z a-z 0-9 _). Le nom (a-z 0-9 _ -) doit
  etre celui du fichier : /lib/<nom>.psl. PhoenOS verifie le resultat : adresses dans la zone
  0x700000-0x7F0000, segments qui ne partagent pas de page, exports dans la bibliotheque.
  Exemple : pslpack -n texte -v 1.0 -a Moi -e texte_mots /texte.elf /lib/texte.psl

== bibliotheques  - bibliotheques partagees .psl (0.8.0)
  Une bibliotheque est un fichier /lib/<nom>.psl : un programme ELF avec une etiquette et la liste
  des fonctions et variables qu'il offre (ses "exports"). Elle est liee a une ADRESSE FIXE dans la
  zone 0x700000-0x7F0000 de l'espace de chaque programme (pas de relocation : chaque bibliotheque
  a sa propre plage ; deux bibliotheques dont les plages se chevauchent ne vont pas ensemble).
  Partage : le code (et les constantes) n'existe qu'en UN exemplaire en memoire, en lecture seule, pour
  tous les programmes qui l'utilisent. Les donnees (variables globales) sont recopiees pour chaque
  programme : une bibliotheque ne change jamais ce que voit un autre programme. Quand le dernier
  programme qui l'utilise s'arrete, la memoire est rendue.
  Comment un programme l'obtient :
    - par son en-tete .poe : poepack -l maths ... (chargee avant la premiere instruction)
    - ou a la demande : psl_sym("maths", "maths_pgcd") renvoie l'adresse d'une fonction (appel
      systeme 203 ; NULL si la bibliotheque ou le symbole n'existe pas). Voir user/phoenos.h.
  Les 2 exemples fournis : maths (pgcd, racine, fibo, appels, nom) a 0x700000, texte (longueur,
  maj, inverser, mots) a 0x710000 ; le programme utilib (exe utilib) les utilise toutes les deux.
  Ecrire une bibliotheque : un fichier lib/<nom>.c (sans libc), lie avec user/psl.ld et
  -Wl,--defsym=LIB_BASE=0x7X0000 ; "make iso" la met dans /lib. Limites : 8 bibliotheques en memoire,
  8 par programme, 256 Kio de code partage et 512 Kio de fichier chacune. Les bibliotheques ne
  peuvent pas en appeler d'autres. Le tas d'un programme s'arrete maintenant a 0x6FF000.

== utilib  - utilise les bibliotheques maths et texte (0.8.0)
  Syntaxe : exe utilib [a [b]]
  Programme d'exemple (application .poe qui declare maths et texte) : pgcd, racine, Fibonacci, mots
  d'une phrase, majuscules, inverse, et le compteur d'appels a maths propre a CE programme. Lancez-en
  deux ensemble (exe utilib &) puis psl : le code de maths est partage. Voir man bibliotheques.


== lspci  - peripheriques PCI et leur pilote (0.9.0)
  Syntaxe : lspci
  Une ligne par peripherique du bus PCI : position bus:peripherique.fonction, identifiants
  vendeur:modele (hexadecimal), type, et le pilote de PhoenOS qui le prend en charge (ou
  "pas de pilote"). Sur QEMU et VirtualBox on voit par exemple le pont hote, le disque IDE et
  la carte reseau 8086:100e (pilote e1000). Voir aussi : drivers, reseau.

== drivers  - pilotes du noyau (0.9.0)
  Syntaxe : drivers
  Liste les pilotes connus : nom, version, genre, officiel ou communaute, description, puis
  pour chacun le peripherique auquel il est attache et son etat ("en service" ou la cause de
  l'echec). 0.9.0 : un seul pilote officiel, e1000 (reseau filaire Intel PRO/1000). Voir : reseau.

== net  - etat du reseau, adresse automatique (0.9.0)
  Syntaxe : net [up]
  Sans argument : carte (nom, pilote, adresse MAC, lien), adresse IP, masque, passerelle, DNS et
  compteurs de trames. "net up" relance la demande d'adresse (DHCP, 8 s au plus). Au demarrage
  la tache "net" la demande toute seule, sans retarder le demarrage : si "net" dit "pas d'adresse",
  attendez quelques secondes ou tapez net up. Voir aussi : ping, dns, reseau.

== ping  - teste une machine du reseau (0.9.0)
  Syntaxe : ping <adresse|nom>
  Envoie 4 messages ICMP et affiche le temps de reponse (precision 10 ms). Le nom est cherche par
  le DNS. Sous QEMU, ping 10.0.2.2 (la passerelle) doit repondre ; les adresses d'Internet peuvent
  rester sans reponse selon la machine hote. Differences avec Unix : toujours 4 envois, pas d'options.

== dns  - adresse d'un nom (0.9.0)
  Syntaxe : dns <nom> | -l | -f
  Demande au serveur DNS (obtenu par DHCP) l'adresse IPv4 d'un nom : dns exemple.org. Les reponses
  piegees ou tronquees sont refusees. Depuis la 0.9.1 les reponses sont gardees en cache (8 noms, pour la
  duree donnee par le serveur, 1 h au plus ; un nom deja connu est repondu sans reseau, marque "(cache)").
  -l liste le cache et les compteurs, -f le vide. Le cache est aussi vide a chaque nouvelle adresse DHCP.
  Les echecs ne sont pas retenus. Pas d'IPv6. Voir aussi : net, reseau.

== web  - recupere une page web (programme, 0.10.0)
  Syntaxe : web <hote> [chemin]
  Ouvre une connexion TCP au port 80 de l'hote (nom ou adresse), envoie une requete HTTP/1.0 et affiche
  la reponse telle quelle (en-tetes compris). Exemple : web example.org       web example.org /index.html
  Sans HTTPS (port 443) : les sites en https ne repondent pas. Sous QEMU la sortie passe par la machine hote.

== reseau  - carte, pilote et pile reseau (0.9.0)
  PhoenOS parle au reseau en trois etages : le PILOTE de la carte (e1000, officiel, integre au
  noyau), la pile (Ethernet, ARP, IPv4, ICMP, UDP) et les services DHCP (adresse automatique) et DNS
  (noms). Tout fonctionne par interrogation (sans interruption) : la tache "net" regarde la carte
  toutes les 10 ms. Depuis la 0.10.0, les PROGRAMMES peuvent ouvrir des connexions TCP (voir web).
  Le Wi-Fi viendra avec son propre pilote ; QEMU ne l'emule pas.
  Pilotes : officiels = ecrits par PhoenOS ; communaute = a leurs risques et perils (licence, art. 5).
  Sous QEMU "make run" donne une carte e1000 (reseau 10.0.2.0/24, passerelle 10.0.2.2, DNS 10.0.2.3).
  Commandes : lspci, drivers, net, ping, dns. Programme : web (0.10.0).
```
