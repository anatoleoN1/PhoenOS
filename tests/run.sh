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
SH="test_shell.c shell.c cmdline.c kprintf.c pmm.c kheap.c string.c fs.c initrd.c persist.c ata.c crc32.c tarw.c rtc.c"
run shell-session       0   -DMODE=0 $SH
run shell-crash-panic   100 -DMODE=1 $SH
run shell-crash-assert  100 -DMODE=2 $SH
run shell-crash-double-free 100 -DMODE=3 $SH
run shell-crash-tas     100 -DMODE=4 $SH

# Le manuel doit documenter chaque commande du shell (et inversement)
printf '== manuel\n'
cmds=$(sed -n 's/^    { G_[A-Z]*, *"\([a-z]*\)".*/\1/p' "$ROOT/src/shell.c" | tr '\n' ' ')
for c in $cmds; do
    grep -q "^== $c " "$ROOT/initrd/docs/manuel.txt" || { echo "  commande sans page de manuel : $c"; FAILED=1; }
done
for c in $(sed -n 's/^== \([a-z]*\) .*/\1/p' "$ROOT/initrd/docs/manuel.txt"); do
    case " $cmds general fichiers stockage " in *" $c "*) ;; *) echo "  page de manuel sans commande : $c"; FAILED=1;; esac
done
echo "  manuel verifie : $(echo $cmds | wc -w) commandes documentees"

echo
if [ "$FAILED" -eq 0 ]; then echo "TOUS LES TESTS PASSENT"; else echo "DES TESTS ONT ECHOUE"; fi
exit $FAILED
