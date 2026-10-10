# Manuel de PhoenOS 0.0.7

> Ce document est généré à partir de `initrd/docs/manuel.txt` (lisible dans PhoenOS avec `man`).

```
Tapez  man            pour la liste des sujets,
       man <sujet>    pour lire un sujet (exemple : man ls).
Ce fichier est /docs/manuel.txt ; le meme texte est dans MANUEL.md du projet.

Les commandes de PhoenOS ressemblent a celles d'Unix mais ne sont PAS identiques :
chaque page indique ce qui differe. Les options se placent juste apres le nom de la
commande, une lettre par option, et se combinent (-rp = -r -p). Une option inconnue
affiche la ligne "usage". Pas d'options longues (--exemple).

```

## Sommaire

- [`general`](#general) — ligne de commande, touches, redirection
- [`fichiers`](#fichiers) — le systeme de fichiers en RAM
- [`help`](#help) — rappel rapide d'une commande
- [`man`](#man) — ce manuel
- [`clear`](#clear) — efface l'ecran
- [`version`](#version) — version et date de compilation
- [`uptime`](#uptime) — temps depuis le demarrage
- [`mem`](#mem) — etat de la memoire
- [`memtest`](#memtest) — teste le tas
- [`sleep`](#sleep) — attend
- [`colors`](#colors) — les 16 couleurs du mode texte
- [`dmesg`](#dmesg) — journal du noyau
- [`history`](#history) — commandes precedentes
- [`crash`](#crash) — provoque une erreur (pour voir l'ecran d'erreur)
- [`reboot`](#reboot) — redemarre
- [`shutdown`](#shutdown) — eteint la machine
- [`halt`](#halt) — arrete le processeur
- [`ls`](#ls) — liste un dossier
- [`cat`](#cat) — affiche des fichiers
- [`cd`](#cd) — change de dossier
- [`pwd`](#pwd) — dossier courant
- [`mkdir`](#mkdir) — cree des dossiers
- [`touch`](#touch) — cree des fichiers vides
- [`rm`](#rm) — supprime
- [`rmdir`](#rmdir) — supprime des dossiers vides
- [`cp`](#cp) — copie un fichier
- [`mv`](#mv) — deplace ou renomme
- [`tree`](#tree) — arborescence
- [`df`](#df) — occupation du systeme de fichiers
- [`hexdump`](#hexdump) — affiche un fichier en hexadecimal
- [`echo`](#echo) — affiche du texte


## general

*ligne de commande, touches, redirection*
```
Saisie
  Fleches haut/bas   historique (16 commandes) ; la saisie en cours est conservee
  Tab                complete une commande (1er mot) ou un chemin (mots suivants)
  Ctrl+C             abandonne la ligne        Ctrl+U   l'efface
  Ctrl+L             efface l'ecran, garde la ligne
Guillemets
  "a  b" regroupe plusieurs mots en un seul argument : echo "a   b" c
  Maximum 8 arguments par commande.
Redirection
  commande > fichier     ecrit la sortie dans le fichier (cree ou remplace)
  commande >> fichier    ajoute la sortie a la fin du fichier
  Le ">" doit etre l'avant-dernier mot de la ligne ; entre guillemets il reste du texte.
  Les messages d'erreur restent a l'ecran et ne vont jamais dans le fichier.
  Pas de tubes (|) ni de redirection d'entree (<) pour l'instant.
Chemins
  /a/b est absolu ; a/b est relatif au dossier courant (affiche dans le prompt).
  "." = ici, ".." = dossier parent. "~" n'existe pas : "cd" seul ramene a la racine /.
  Noms : 31 caracteres maximum ; chemins : 255 ; majuscules et minuscules comptent :
  "A" et "a" sont deux noms differents.
Differences avec Unix : pas de droits, de dates ni de proprietaires ; pas de fichiers
  caches ; pas de jokers (*, ?) ; tout est en memoire (voir "fichiers").
```

## fichiers

*le systeme de fichiers en RAM*
```
  Au demarrage, GRUB charge l'initrd (dossier initrd/ du projet) dans le systeme de
  fichiers. Ce que vous creez ensuite est PERDU a l'extinction (pas de disque encore).
  Limites : fichiers 1 Mio, 4096 elements, 32 niveaux de dossiers.
```

## help

*rappel rapide d'une commande*
```
  Syntaxe : help [commande]
  Sans argument : liste toutes les commandes par groupe. Avec un nom : sa syntaxe en
  une ligne. Pour les details et exemples : man.
```

## man

*ce manuel*
```
  Syntaxe : man [sujet]
  Sans argument : liste des sujets. Avec un sujet : sa page.
  Lit /docs/manuel.txt : si vous le modifiez (cat, echo >>), man suit.
```

## clear

*efface l'ecran*
```
  Syntaxe : clear        (Ctrl+L fait pareil sans perdre la ligne en cours)
```

## version

*version et date de compilation*
```
  Syntaxe : version
```

## uptime

*temps depuis le demarrage*
```
  Syntaxe : uptime
  Affiche heures, minutes, secondes et le nombre de ticks (100 par seconde).
```

## mem

*etat de la memoire*
```
  Syntaxe : mem
  Memoire physique detectee / libre, taille du tas du noyau, blocs utilises et libres.
```

## memtest

*teste le tas*
```
  Syntaxe : memtest [Kio]
  Alloue un bloc (64 Kio par defaut, de 1 a 4096), y ecrit 4 motifs (00, FF, AA, 55), les
  relit, verifie l'integrite du tas et mesure le temps. Exemple : memtest 1024
  Specifique a PhoenOS (pas d'equivalent direct sous Unix).
```

## sleep

*attend*
```
  Syntaxe : sleep <ms>
  Difference : l'argument est en MILLISECONDES (pas en secondes). Maximum 60000.
  Exemple : sleep 1500
```

## colors

*les 16 couleurs du mode texte*
```
  Syntaxe : colors
```

## dmesg

*journal du noyau*
```
  Syntaxe : dmesg
  Rejoue tout ce que le noyau a ecrit depuis le demarrage (les 8 derniers Kio).
  La sortie ne peut pas etre redirigee vers un fichier pour l'instant.
```

## history

*commandes precedentes*
```
  Syntaxe : history
  Les 16 dernieres, de la plus ancienne a la plus recente. Utilisez les fleches pour les rappeler.
```

## crash

*provoque une erreur (pour voir l'ecran d'erreur)*
```
  Syntaxe : crash <type>
  Types : div0 (division par zero), gp (faute de protection), ud (instruction invalide),
          panic, assert, doublefree (double liberation memoire), heap (tas corrompu)
  L'ecran d'erreur orange (ou violet selon la compilation) s'affiche ; redemarrez ensuite.
  Specifique a PhoenOS, pour tester la robustesse du noyau.
```

## reboot

*redemarre*
```
  Syntaxe : reboot
  Sous "make run" (QEMU lance avec -no-reboot) cela quitte QEMU au lieu de redemarrer.
```

## shutdown

*eteint la machine*
```
  Syntaxe : shutdown
  Fonctionne sous QEMU et VirtualBox. Sur un vrai PC : arret du processeur seulement.
```

## halt

*arrete le processeur*
```
  Syntaxe : halt
```

## ls

*liste un dossier*
```
  Syntaxe : ls [-l] [chemin]
  Options :
    -l   une ligne par element : type (d dossier, - fichier), taille en octets, nom
  Les dossiers s'affichent en cyan avec un "/" final, tries par ordre alphabetique.
  Sans chemin : le dossier courant. Sur un fichier : affiche ce fichier.
  Differences : pas de -a ni -h ; la taille de -l est toujours en octets.
  Exemples : ls    ls -l /docs    ls etc
```

## cat

*affiche des fichiers*
```
  Syntaxe : cat [-n] <fichier>...
  Options :
    -n   numerote les lignes (compteur continu sur tous les fichiers)
  A l'ecran, les octets de controle sont remplaces par "." ; vers un fichier (cat a > b),
  la copie est fidele. Il faut au moins un fichier (pas de lecture du clavier).
  Exemples : cat /etc/motd    cat -n /README.txt    cat a b > ab
```

## cd

*change de dossier*
```
  Syntaxe : cd [dossier]
  Sans argument : retourne a la racine / (et non a un "dossier personnel").
  Exemples : cd /docs    cd ..    cd
```

## pwd

*dossier courant*
```
  Syntaxe : pwd
```

## mkdir

*cree des dossiers*
```
  Syntaxe : mkdir [-p] <dossier>...
  Options :
    -p   cree aussi les dossiers parents manquants ; pas d'erreur si le dossier existe
  Exemples : mkdir projets    mkdir -p a/b/c
```

## touch

*cree des fichiers vides*
```
  Syntaxe : touch <fichier>...
  Difference : ne fait que creer (pas de dates a mettre a jour) ; un fichier existant
  n'est pas modifie.
```

## rm

*supprime*
```
  Syntaxe : rm [-r] <chemin>...
  Options :
    -r   supprime aussi les dossiers, avec tout leur contenu
  Sans -r, un dossier donne l'erreur "est un dossier". Refus pour la racine et pour tout
  dossier qui contient le dossier courant ("ressource occupee"). Pas de -f ni -i : aucune
  confirmation, aucune corbeille. Exemples : rm a.txt    rm -r projets
```

## rmdir

*supprime des dossiers vides*
```
  Syntaxe : rmdir <dossier>...
  Erreur "dossier non vide" sinon (utilisez rm -r).
```

## cp

*copie un fichier*
```
  Syntaxe : cp <source> <destination>
  Si la destination est un dossier, le fichier y est copie sous le meme nom ; si c'est
  un fichier existant, il est remplace. Differences : fichiers seulement (pas de dossiers,
  pas de -r) ; copier un fichier sur lui-meme est refuse.
  Exemples : cp /hello.txt .    cp a.txt b.txt
```

## mv

*deplace ou renomme*
```
  Syntaxe : mv <source> <destination>
  Destination = dossier existant : on y deplace. Sinon : renommage. Une destination qui
  existe deja donne "existe deja" (rien n'est ecrase, contrairement a Unix).
  Marche aussi pour les dossiers (jamais dans eux-memes).
```

## tree

*arborescence*
```
  Syntaxe : tree [-d] [dossier]
  Options :
    -d   affiche les dossiers seulement
  Termine par le nombre de dossiers et de fichiers. Specifique : decoupage plus simple
  que l'outil Unix du meme nom.
```

## df

*occupation du systeme de fichiers*
```
  Syntaxe : df
  Nombre de fichiers, de dossiers, octets de contenu et limites. Pas de disques : tout
  est en RAM.
```

## hexdump

*affiche un fichier en hexadecimal*
```
  Syntaxe : hexdump <fichier> [octets]
  Affiche les 256 premiers octets par defaut (ou le nombre demande) : decalage, 16 octets
  en hexadecimal, puis le texte entre | |. Difference : pas d'options ; le nombre
  d'octets est un argument.
  Exemples : hexdump hello.txt    hexdump hello.txt 1000
```

## echo

*affiche du texte*
```
  Syntaxe : echo [-n] [texte...]
  Options :
    -n   pas de retour a la ligne final
  Pas d'interpretation de \n ni de $variables. Se combine avec la redirection :
  echo salut > a.txt    echo suite >> a.txt
```
