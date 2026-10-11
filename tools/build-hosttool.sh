#!/bin/sh
# Compile un outil de la machine de developpement qui reutilise du code du noyau (poe.c, psl.c...).
#   build-hosttool.sh sortie outil.c fichier_noyau.c...
# Les fonctions de string.c sont renommees (pk_*) pour ne pas se melanger avec la libc de la machine.
set -e
OUT=$1; TOOL=$2; shift 2
D=$(dirname "$OUT"); ROOT=$(cd "$(dirname "$0")/.." && pwd)
REN="-Dmemset=pk_memset -Dmemcpy=pk_memcpy -Dmemmove=pk_memmove -Dmemcmp=pk_memcmp -Dstrchr=pk_strchr -Dstrcpy=pk_strcpy -Dstrlen=pk_strlen -Dstrcmp=pk_strcmp"
OBJS=""
for f in "$@" string.c; do
    o="$D/$(basename "$OUT")_$(basename "$f" .c).o"
    gcc -O2 -Wall -Wextra -Werror -fno-builtin -fno-tree-loop-distribute-patterns $REN -iquote "$ROOT/src" -c "$ROOT/src/$f" -o "$o"
    OBJS="$OBJS $o"
done
gcc -O2 -Wall -Wextra -Werror -iquote "$ROOT/src" "$TOOL" $OBJS -o "$OUT"
rm -f $OBJS
