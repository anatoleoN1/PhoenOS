#!/bin/sh
# Lance les tests automatiques de PhoenOS (make test).
# Ce sont de petits programmes 32 bits autonomes qui exécutent le VRAI code du noyau
# (mémoire, shell, clavier) avec une fausse RAM et de faux scancodes.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B="$ROOT/build/test"
FAILED=0

rm -rf "$B"; mkdir -p "$B/src"
cp "$ROOT"/src/* "$B/src/"
cp "$ROOT"/tests/shim/* "$B/src/"        # remplace io.h et panic.h par des versions de test
cp "$ROOT"/tests/*.c "$ROOT"/tests/testlib.h "$ROOT"/tests/fakeata.h "$B/src/"

CF="-m32 -static -nostdlib -ffreestanding -fno-builtin -fno-stack-protector -fno-pie \
-fno-tree-loop-distribute-patterns -O2 -Wall -Wextra -Wno-unused-function -I$B/src"

# $1 = nom, $2 = code de sortie attendu, $3.. = options et sources
run() {
    name=$1; want=$2; shift 2
    printf '== %s\n' "$name"
    if ! (cd "$B/src" && gcc $CF "$@" -o "$B/$name"); then
        echo "  ECHEC de compilation"; FAILED=1; return
    fi
    (cd "$B" && "./$name"); got=$?
    if [ "$got" -ne "$want" ]; then
        echo "  => ECHEC (code de sortie $got, attendu $want)"; FAILED=1
    else
        echo "  => OK"
    fi
}

# Un test d'exécution 32 bits est-il possible sur cette machine ?
echo 'void _start(void){__asm__ volatile("movl $1,%eax; xorl %ebx,%ebx; int $0x80");}' > "$B/probe.c"
if ! gcc -m32 -static -nostdlib -ffreestanding "$B/probe.c" -o "$B/probe" 2>/dev/null || ! "$B/probe"; then
    echo "Impossible de compiler/executer un programme 32 bits ici (installer gcc-multilib)."
    exit 1
fi

run memoire-frames      0   -DMODE=0 test_mem.c pmm.c kheap.c string.c
run memoire-tas         0   -DMODE=1 test_mem.c pmm.c kheap.c string.c
run memoire-modules 0 -DMODE=7 test_mem.c pmm.c kheap.c string.c
run erreur-double-free  100 -DMODE=2 test_mem.c pmm.c kheap.c string.c
run erreur-debordement  100 -DMODE=3 test_mem.c pmm.c kheap.c string.c
run erreur-kfree-invalide 100 -DMODE=4 test_mem.c pmm.c kheap.c string.c
run erreur-frame-double 100 -DMODE=5 test_mem.c pmm.c kheap.c string.c
run erreur-frame-reservee 100 -DMODE=6 test_mem.c pmm.c kheap.c string.c
run shell-cmdline       0   test_cmdline.c cmdline.c string.c
run clavier-azerty      0   test_keyboard.c keyboard.c serial.c string.c
run horloge 0 test_rtc.c rtc.c string.c

# Programmes utilisateur : compiles comme pour l'ISO (user/*.c, charges a 4 Mio).
# Les numeros d'appels systeme sont ceux de Linux : on les EXECUTE ici pour de vrai, en 32 bits.
mkdir -p "$B/user"
UCF="-m32 -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin -fno-omit-frame-pointer -mno-sse -mno-mmx -nostdlib -static"
printf '== programmes-utilisateur\n'
UFAIL=0
for f in "$ROOT"/user/*.c; do
    n=$(basename "$f" .c); [ "$n" = crt0 ] && continue
    if ! gcc $UCF -T "$ROOT/user/user.ld" -Wl,--build-id=none -I"$ROOT/user" "$f" "$ROOT/user/crt0.c" -o "$B/user/$n" 2> "$B/user/$n.err"; then
        echo "  ECHEC de compilation : $n"; cat "$B/user/$n.err"; UFAIL=1
    fi
done
echo "bonjour
deux" > "$B/user/data.txt"
# 0.5.0 : l'outil mkpoe (compile pour de vrai) emballe des programmes en applications .poe
if ! sh "$ROOT/tools/build-hosttool.sh" "$B/mkpoe" "$ROOT/tools/mkpoe.c" poe.c crc32.c 2> "$B/mkpoe.err"; then
    echo "  ECHEC de compilation : mkpoe"; cat "$B/mkpoe.err"; UFAIL=1
else
    "$B/mkpoe" -n calc -v 9.8.7 -a "Equipe Test" "$B/user/calc" "$B/user/calc.poe" > /dev/null || UFAIL=1
    "$B/mkpoe" -n app2 -v 2.0 -a "Equipe Test" -d "Une application complete" -l maths -l texte "$B/user/calc" "$B/user/app2.poe" > /dev/null || UFAIL=1
    "$B/mkpoe" -n bad -v 1 -a T -l MAJ "$B/user/calc" "$B/user/bad.poe" > /dev/null 2>&1 && { echo "  ECHEC : mkpoe accepte une bibliotheque invalide"; UFAIL=1; }
    "$B/mkpoe" -n dup -v 1 -a T -l maths -l maths "$B/user/calc" "$B/user/bad.poe" > /dev/null 2>&1 && { echo "  ECHEC : mkpoe accepte deux fois la meme bibliotheque"; UFAIL=1; }
    "$B/mkpoe" -n gui -v 1.0 -a Test -g "$B/user/hello" "$B/user/gui.poe" > /dev/null || UFAIL=1
    "$B/mkpoe" -n net -v 1.0 -a Test -r "$B/user/hello" "$B/user/net.poe" > /dev/null || UFAIL=1
fi
ucheck() { # description, attendu (sous-chaine), sortie
    if printf '%s' "$3" | grep -qF -- "$2"; then :; else echo "  ECHEC ($1) : attendu '$2', obtenu : $3"; UFAIL=1; fi
}
U="$B/user"
ucheck "hello arguments" "[2] b c" "$("$U/hello" a "b c")"
ucheck "hello argc" "arguments : 3" "$("$U/hello" a "b c")"
ucheck "primes 30" "2 3 5 7 11 13 17 19 23 29" "$("$U/primes" 30)"
ucheck "primes compte" "10 nombre(s) premier(s)" "$("$U/primes" 30)"
ucheck "primes 1000" "168 nombre(s) premier(s)" "$("$U/primes" 1000)"
ucheck "primes borne" "au plus 100000" "$("$U/primes" 999999 2>&1)"
ucheck "saisie" "Bonjour, Anatole !" "$(printf 'Anatole\n' | "$U/saisie")"
ucheck "saisie vide" "rien recu" "$("$U/saisie" < /dev/null)"
ucheck "compte" "2 ligne(s), 13 octet(s)" "$("$U/compte" "$B/user/data.txt")"
ucheck "compte absent" "impossible d'ouvrir" "$("$U/compte" /nofile 2>&1)"
ucheck "heure" "time()" "$("$U/heure")"

# 0.6.0 : programmes pour le multitache
ucheck "ticker" "zed 3/3" "$("$U/ticker" zed 3 10)"
ucheck "ticker debut" "zed 1/3" "$("$U/ticker" zed 3 10)"
ucheck "ticker usage" "usage : ticker" "$("$U/ticker" 2>&1)"
ucheck "ticker borne" "fois entre 1 et 1000" "$("$U/ticker" a 0 2>&1)"
ucheck "boucle usage" "usage : boucle" "$("$U/boucle" 0 2>&1)"

# 0.7.0 : tas (malloc/free/realloc de user/libc.h, execute pour de vrai sur le brk de Linux) et programmes d'enfants
ucheck "tas somme" "somme des valeurs : 499500 (attendu 499500)" "$("$U/tas" 1000)"
ucheck "tas fin" "tas : tout est correct" "$("$U/tas" 1000)"
ucheck "tas rend la memoire" "a la fin                           : tas pris au noyau   4 Kio, utilise   0 Kio" "$("$U/tas" 5000)"
ucheck "tas gros" "tas : tout est correct" "$("$U/tas" 20000)"
ucheck "tas usage" "usage : tas" "$("$U/tas" 0 2>&1)"
ucheck "lance usage" "usage : lance" "$("$U/lance" 2>&1)"
ucheck "essaim usage" "usage : essaim" "$("$U/essaim" 0 2>&1)"
ucheck "essaim borne" "usage : essaim" "$("$U/essaim" 9 2>&1)"
# printf de libc.h : formats, largeur, zeros, signes
gcc $UCF -T "$ROOT/user/user.ld" -Wl,--build-id=none -I"$ROOT/user" "$ROOT/tests/user_libc_check.c" "$ROOT/user/crt0.c" -o "$B/user/libc_check" 2> "$B/user/libc_check.err" || { echo "  ECHEC de compilation : libc_check"; cat "$B/user/libc_check.err"; UFAIL=1; }
ucheck "libc" "libc : tout est correct" "$("$B/user/libc_check" 2>&1)"

# 0.4.0 : programmes qui ECRIVENT des fichiers (memes appels qu'une vraie machine Linux 32 bits)
rm -f "$B/user/out.txt" "$B/user/notes.txt"
# notes : meme programme, mais son carnet est un fichier de test (le vrai est /notes.txt dans PhoenOS)
gcc $UCF -DFILE_PATH="\"$B/user/notes.txt\"" -T "$ROOT/user/user.ld" -Wl,--build-id=none -I"$ROOT/user" "$ROOT/user/notes.c" "$ROOT/user/crt0.c" -o "$B/user/notes_test" || UFAIL=1
ucheck "copie" "data.txt -> $B/user/out.txt : 13 octet(s)" "$("$U/copie" "$B/user/data.txt" "$B/user/out.txt")"
ucheck "copie contenu" "bonjour" "$(cat "$B/user/out.txt")"
ucheck "copie identique" "identiques" "$("$U/copie" "$B/user/out.txt" "$B/user/out.txt" 2>&1)"
ucheck "copie contenu intact" "deux" "$(cat "$B/user/out.txt")"
ucheck "copie ecrase" "13 octet(s)" "$(printf 'x' > "$B/user/out.txt"; "$U/copie" "$B/user/data.txt" "$B/user/out.txt")"
ucheck "copie sans source" "impossible d'ouvrir" "$("$U/copie" /nofile "$B/user/out.txt" 2>&1)"
ucheck "copie dossier absent" "impossible d'ecrire" "$("$U/copie" "$B/user/data.txt" /nodir/x 2>&1)"
ucheck "copie usage" "usage" "$("$U/copie" a 2>&1)"
ucheck "notes vide" "carnet est vide" "$("$U/notes_test")"
ucheck "notes ajoute" "Note ajoutee" "$("$U/notes_test" premiere note)"
"$U/notes_test" "deuxieme" "note" > /dev/null
ucheck "notes liste 1" " 1  premiere note" "$("$U/notes_test")"
ucheck "notes liste 2" " 2  deuxieme note" "$("$U/notes_test")"
ucheck "notes vider" "Carnet vide" "$("$U/notes_test" -v)"
ucheck "notes deja vide" "deja vide" "$("$U/notes_test" -v)"
rm -f "$B/user/out.txt" "$B/user/notes.txt"
calc() { "$U/calc" "$@" 2>&1; }
for t in "2+3*4=14" "(2+3)*4=20" "10/4=2.5" "2^10=1024" "7%3=1" "-7%3=-1" "-3+5=2" "sqrt(2)=1.4142" "1/3=0.3333" "pi*2=6.2832" \
         "2^-2=0.25" "abs(-4.5)=4.5" "1,5*2=3" "2*3^2=18" "-2^2=-4" "10-2-3=5" "100/3/2=16.6666" "0.1+0.2=0.3" "sqrt(1000000)=1000" \
         "99999999999+1=100000000000" "123456.789*1000=123456789" "-0.5*-4=2" "0.00005=0" "2^64=" "1+2*3-4/2=5" "((((7))))=7"; do
    e=${t%%=*}; want=${t#*=}
    got=$(calc "$e")
    [ "$want" = "" ] && continue
    [ "$got" = "$want" ] || { echo "  ECHEC (calc '$e') : attendu '$want', obtenu '$got'"; UFAIL=1; }
done
ucheck "calc div0" "division par zero" "$(calc 1/0)"
ucheck "calc div0 mod" "division par zero" "$(calc 5%0)"
ucheck "calc sqrt neg" "racine carree d'un nombre negatif" "$(calc 'sqrt(-1)')"
ucheck "calc grand" "resultat trop grand" "$(calc 1000000*1000000)"
ucheck "calc grand puissance" "resultat trop grand" "$(calc 10^20)"
ucheck "calc exposant" "exposant trop grand" "$(calc 2^100)"
ucheck "calc exposant frac" "l'exposant doit etre un entier" "$(calc 2^0.5)"
ucheck "calc incomplet" "expression incomplete" "$(calc '2+')"
ucheck "calc paren" "parenthese fermante attendue" "$(calc '(1+2')"
ucheck "calc paren2" "parenthese ouvrante manquante" "$(calc '1+2)')"
ucheck "calc inconnu" "expression non comprise" "$(calc '3 x')"
ucheck "calc vide" "expression incomplete" "$(calc ' ')"
ucheck "calc args" "7" "$(calc 3 + 4)"
"$U/calc" 1/0 >/dev/null 2>&1; [ $? -eq 1 ] || { echo "  ECHEC (code de sortie de calc)"; UFAIL=1; }
ucheck "calc interactif" "  = 6" "$(printf '1+1\nans*3\nq\n' | "$U/calc")"
ucheck "calc interactif erreur" "erreur : division par zero" "$(printf '1/0\nq\n' | "$U/calc")"
ucheck "calc interactif apres erreur" "  = 4" "$(printf '1/0\n2+2\nq\n' | "$U/calc")"
ucheck "calc fin de fichier" "calc>" "$(printf '5' | "$U/calc")"
"$U/faute" gp >/dev/null 2>&1;  [ $? -eq 139 ] || { echo "  ECHEC (faute gp : arret attendu par instruction privilegiee)"; UFAIL=1; }
"$U/faute" div >/dev/null 2>&1; [ $? -eq 136 ] || { echo "  ECHEC (faute div)"; UFAIL=1; }
"$U/faute" ud >/dev/null 2>&1;  [ $? -eq 132 ] || { echo "  ECHEC (faute ud)"; UFAIL=1; }
"$U/faute" io >/dev/null 2>&1;  [ $? -eq 139 ] || { echo "  ECHEC (faute io)"; UFAIL=1; }
"$U/faute" kern >/dev/null 2>&1; [ $? -eq 139 ] || { echo "  ECHEC (faute kern)"; UFAIL=1; }
"$U/faute" nul >/dev/null 2>&1;  [ $? -eq 139 ] || { echo "  ECHEC (faute nul)"; UFAIL=1; }
"$U/faute" code >/dev/null 2>&1; [ $? -eq 139 ] || { echo "  ECHEC (faute code : ecriture dans le code)"; UFAIL=1; }
"$U/faute" vide >/dev/null 2>&1; [ $? -eq 139 ] || { echo "  ECHEC (faute vide : page non attribuee)"; UFAIL=1; }
"$U/primes" 999999 >/dev/null 2>&1; [ $? -eq 1 ] || { echo "  ECHEC (code de sortie de primes)"; UFAIL=1; }
if [ $UFAIL -eq 0 ]; then echo "  14 programmes compiles et executes (arguments, entree, fichiers, fautes) => OK"; else echo "  => ECHEC"; FAILED=1; fi

# 0.8.0 : bibliotheques partagees .psl : vrais ELF lies a leur adresse (user/psl.ld), emballes par le vrai outil mkpsl
mkdir -p "$B/lib"
LFAIL=0
if ! sh "$ROOT/tools/build-hosttool.sh" "$B/mkpsl" "$ROOT/tools/mkpsl.c" psl.c poe.c elf.c crc32.c 2> "$B/mkpsl.err"; then
    echo "  ECHEC de compilation : mkpsl"; cat "$B/mkpsl.err"; LFAIL=1
else
    lib_build() {   # nom base source exports...
        n=$1; base=$2; src=$3; shift 3
        gcc $UCF -T "$ROOT/user/psl.ld" -Wl,--defsym=LIB_BASE=$base -Wl,--build-id=none "$src" -o "$B/lib/$n.elf" 2> "$B/lib/$n.err" || { cat "$B/lib/$n.err"; LFAIL=1; return; }
        ex=""; for e in "$@"; do ex="$ex -e $e"; done
        "$B/mkpsl" -n $n -v 1.0 -a "Equipe Test" -d "Test $n" $ex "$B/lib/$n.elf" "$B/lib/$n.psl" > /dev/null || LFAIL=1
    }
    lib_build maths 0x700000 "$ROOT/lib/maths.c" maths_pgcd maths_racine maths_fibo maths_appels maths_nom
    lib_build texte 0x710000 "$ROOT/lib/texte.c" texte_longueur texte_maj texte_inverser texte_mots
    lib_build clash 0x701000 "$ROOT/lib/texte.c" texte_mots
    # refus a la creation : adresse hors de la zone, symbole absent, nom invalide
    gcc $UCF -T "$ROOT/user/psl.ld" -Wl,--defsym=LIB_BASE=0x500000 -Wl,--build-id=none "$ROOT/lib/texte.c" -o "$B/lib/far.elf" || LFAIL=1
    "$B/mkpsl" -n far -v 1 -a T -e texte_mots "$B/lib/far.elf" "$B/lib/far.psl" > /dev/null 2>&1 && { echo "  ECHEC : mkpsl accepte une adresse hors zone"; LFAIL=1; }
    "$B/mkpsl" -n texte -v 1 -a T -e inexistant "$B/lib/texte.elf" "$B/lib/x.psl" > /dev/null 2>&1 && { echo "  ECHEC : mkpsl accepte un symbole absent"; LFAIL=1; }
    "$B/mkpsl" -n Texte -v 1 -a T -e texte_mots "$B/lib/texte.elf" "$B/lib/x.psl" > /dev/null 2>&1 && { echo "  ECHEC : mkpsl accepte un nom invalide"; LFAIL=1; }
    gcc $UCF -T "$ROOT/user/user.ld" -Wl,--build-id=none -I"$ROOT/user" "$ROOT/user/utilib.c" "$ROOT/user/crt0.c" -o "$B/user/utilib" || LFAIL=1
    "$B/mkpoe" -n utilib -v 1.0 -a T -d "demo" -l maths -l texte "$B/user/utilib" "$B/user/utilib.poe" > /dev/null || LFAIL=1
fi
[ $LFAIL -eq 0 ] || FAILED=1

# Les ELF reels, pour le chargeur
run elf 0 test_elf.c elf.c string.c
run poe 0 test_poe.c poe.c elf.c crc32.c string.c
run bibliotheques 0 test_libs.c libmgr.c psl.c poe.c elf.c crc32.c aspace.c paging.c fs.c pmm.c kheap.c dlog.c kprintf.c string.c
run pagination 0 test_paging.c paging.c pmm.c string.c
run journal-debug 0 test_dlog.c dlog.c kprintf.c string.c
run ordonnanceur-clavier 0 test_sched_kbd.c sched.c switch.S keyboard.c kheap.c pmm.c string.c
run ordonnanceur 0 test_sched.c sched.c switch.S kheap.c pmm.c string.c
run programmes-multiples 0 test_user.c sched.c switch.S syscall.c libmgr.c psl.c aspace.c elf.c fs.c poe.c crc32.c dlog.c kprintf.c pmm.c kheap.c string.c rtc.c
run espaces-adressage 0 test_aspace.c aspace.c paging.c pmm.c string.c
run appels-systeme 0 test_syscall.c syscall.c libmgr.c psl.c poe.c elf.c crc32.c dlog.c aspace.c fs.c kprintf.c pmm.c kheap.c string.c rtc.c
run ecran-defilement 0 -DVGA_TEST_MEMORY=test_vga test_vga.c vga.c string.c
run kprintf             0   test_kprintf.c kprintf.c string.c
run fichiers 0 test_fs.c fs.c pmm.c kheap.c string.c

# Vraie archive GNU tar (ustar) pour tester le lecteur avec un vrai fichier
mkdir -p "$B/real/etc" "$B/real/docs/a/b"
printf 'Bonjour\n' > "$B/real/etc/motd"; printf 'salut\n' > "$B/real/hello.txt"; printf 'profond\n' > "$B/real/docs/a/b/c.txt"
tar --format=ustar --owner=0 --group=0 --numeric-owner -cf "$B/real.tar" -C "$B/real" .
run initrd 0 test_initrd.c initrd.c fs.c pmm.c kheap.c string.c
run stockage 0 test_storage.c persist.c ata.c crc32.c tarw.c initrd.c fs.c pmm.c kheap.c string.c
if [ -f "$B/src/../snapshot.tar" ] || [ -f "$B/snapshot.tar" ]; then
    printf '== tar-reel\n'
    if tar tvf "$B/snapshot.tar" > "$B/snapshot.lst" 2> "$B/snapshot.err" && [ ! -s "$B/snapshot.err" ]; then
        n=$(wc -l < "$B/snapshot.lst"); echo "  GNU tar relit la sauvegarde PhoenFS : $n elements"
        mkdir -p "$B/snapx" && tar xf "$B/snapshot.tar" -C "$B/snapx" && [ "$(cat "$B/snapx/dir/sub/b.txt")" = "bravo
ligne2" ] && echo "  => OK" || { echo "  => ECHEC (contenu extrait different)"; FAILED=1; }
    else
        echo "  => ECHEC : tar refuse l'archive"; cat "$B/snapshot.err"; FAILED=1
    fi
fi
SH="test_shell.c shell.c dlog.c poe.c psl.c elf.c cmdline.c kprintf.c pmm.c kheap.c string.c fs.c initrd.c persist.c ata.c crc32.c tarw.c rtc.c"
run shell-session       0   -DMODE=0 $SH
run shell-crash-panic   100 -DMODE=1 $SH
run shell-crash-assert  100 -DMODE=2 $SH
run shell-crash-double-free 100 -DMODE=3 $SH
run shell-crash-tas     100 -DMODE=4 $SH

# Le manuel doit documenter chaque commande du shell (et inversement)
printf '== manuel\n'
cmds=$(sed -n 's/^    { G_[A-Z]*, *"\([a-z]*\)".*/\1/p' "$ROOT/src/shell.c" | tr '\n' ' ')
progs=$(for f in "$ROOT"/user/*.c; do n=$(basename "$f" .c); [ "$n" = crt0 ] || printf '%s ' "$n"; done)
for c in $cmds $progs; do
    grep -q "^== $c " "$ROOT/initrd/docs/manuel.txt" || { echo "  commande ou programme sans page de manuel : $c"; FAILED=1; }
done
for c in $(sed -n 's/^== \([a-z]*\) .*/\1/p' "$ROOT/initrd/docs/manuel.txt"); do
    case " $cmds $progs general fichiers stockage programmes multitache processus bibliotheques " in *" $c "*) ;; *) echo "  page de manuel sans commande : $c"; FAILED=1;; esac
done
echo "  manuel verifie : $(echo $cmds | wc -w) commandes et $(echo $progs | wc -w) programmes documentes"

echo
if [ "$FAILED" -eq 0 ]; then echo "TOUS LES TESTS PASSENT"; else echo "DES TESTS ONT ECHOUE"; fi
exit $FAILED
