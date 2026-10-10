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
cp "$ROOT"/tests/*.c "$ROOT"/tests/testlib.h "$B/src/"

CF="-m32 -static -nostdlib -ffreestanding -fno-builtin -fno-stack-protector -fno-pie \
-fno-tree-loop-distribute-patterns -O2 -Wall -Wextra -Wno-unused-function -I$B/src"

# $1 = nom, $2 = code de sortie attendu, $3.. = options et sources
run() {
    name=$1; want=$2; shift 2
    printf '== %s\n' "$name"
    if ! (cd "$B/src" && gcc $CF "$@" -o "$B/$name"); then
        echo "  ECHEC de compilation"; FAILED=1; return
    fi
    "$B/$name"; got=$?
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
run erreur-double-free  100 -DMODE=2 test_mem.c pmm.c kheap.c string.c
run erreur-debordement  100 -DMODE=3 test_mem.c pmm.c kheap.c string.c
run erreur-kfree-invalide 100 -DMODE=4 test_mem.c pmm.c kheap.c string.c
run erreur-frame-double 100 -DMODE=5 test_mem.c pmm.c kheap.c string.c
run erreur-frame-reservee 100 -DMODE=6 test_mem.c pmm.c kheap.c string.c
run shell-cmdline       0   test_cmdline.c cmdline.c string.c
run clavier-azerty      0   test_keyboard.c keyboard.c string.c
run kprintf             0   test_kprintf.c kprintf.c string.c
SH="test_shell.c shell.c cmdline.c kprintf.c pmm.c kheap.c string.c"
run shell-session       0   -DMODE=0 $SH
run shell-crash-panic   100 -DMODE=1 $SH
run shell-crash-assert  100 -DMODE=2 $SH
run shell-crash-double-free 100 -DMODE=3 $SH
run shell-crash-tas     100 -DMODE=4 $SH

echo
if [ "$FAILED" -eq 0 ]; then echo "TOUS LES TESTS PASSENT"; else echo "DES TESTS ONT ECHOUE"; fi
exit $FAILED
