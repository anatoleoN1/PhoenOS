#!/bin/sh
# PhoenOS - tools/debug-session.sh  (0.6.2)
# Lance PhoenOS dans QEMU en gardant ABSOLUMENT TOUT dans des fichiers, depuis le debut de la commande :
#   logs/<date-heure>/session.log   deroulement de la session (variables, etapes, commande QEMU, code de sortie)
#   logs/<date-heure>/build.log     sortie complete de la compilation (make iso) : rien n'est affiche a l'ecran
#   logs/<date-heure>/os.log        journal INTERNE de PhoenOS (port de debogage 0xE9) : demarrage, commandes tapees,
#                                   touches, programmes, appels systeme, ordonnanceur, fautes, panique...
#   logs/<date-heure>/console.log   tout ce qui est passe par le port serie (ce que vous voyez dans le terminal)
#   logs/<date-heure>/qemu.log      journal de QEMU : exceptions/interruptions CPU, redemarrages, erreurs invite
#   logs/latest                     lien vers la derniere session
# Usage : sh tools/debug-session.sh <serial|window|sdl|gtk>
# Variables (fournies par le Makefile) : MAKE QEMU ISO DISK QEMU_DISK ; QEMU_D (options -d de QEMU, vide = pas de qemu.log).

MODE=${1:-serial}
MAKE=${MAKE:-make}
QEMU=${QEMU:-qemu-system-i386}
ISO=${ISO:-}
DISK=${DISK:-disk.img}
QEMU_D=${QEMU_D-int,cpu_reset,guest_errors,unimp}

TS=$(date +%Y%m%d-%H%M%S)
DIR=logs/$TS
n=1
while [ -e "$DIR" ]; do DIR=logs/$TS-$n; n=$((n + 1)); done
mkdir -p "$DIR" || { echo "Impossible de creer $DIR" >&2; exit 1; }
rm -f logs/latest
ln -s "$(basename "$DIR")" logs/latest 2>/dev/null

SESSION=$DIR/session.log
T0=$(date +%s)
slog() { printf '[%s +%3ss] %s\n' "$(date +%H:%M:%S)" "$(( $(date +%s) - T0 ))" "$*" >> "$SESSION"; }

slog "=== Session de debug PhoenOS ($MODE) ==="
slog "commande     : $0 $*"
slog "repertoire   : $(pwd)"
slog "version      : $(sed -n 's/.*"\(.*\)".*/\1/p' src/version.h 2>/dev/null | head -1)"
slog "systeme hote : $(uname -a)"
slog "qemu         : $($QEMU --version 2>&1 | head -1)"
slog "gcc          : $(gcc --version 2>&1 | head -1)"
slog "affichage    : DISPLAY='$DISPLAY' WAYLAND_DISPLAY='$WAYLAND_DISPLAY'"
slog "disque       : $DISK ($( [ -f "$DISK" ] && wc -c < "$DISK" || echo absent ) octets)"
slog "options -d   : '${QEMU_D}'"

echo ">> Session de debug : $DIR  (tout y est enregistre ; resume a la fin)"

slog "--- compilation (voir build.log) ---"
if ! $MAKE iso > "$DIR/build.log" 2>&1; then
    slog "ECHEC de la compilation"
    echo ">> ECHEC de la compilation. Fin du journal :" >&2
    tail -n 20 "$DIR/build.log" >&2
    echo ">> Journal complet : $DIR/build.log" >&2
    exit 1
fi
slog "compilation reussie ($(wc -l < "$DIR/build.log") lignes)"
if [ -z "$ISO" ]; then ISO=$(ls -t phoenos-*.iso 2>/dev/null | head -1); fi
slog "image iso    : $ISO ($(wc -c < "$ISO") octets)"
if [ ! -f "$DISK" ]; then
    $MAKE "$DISK" >> "$DIR/build.log" 2>&1 && slog "disque $DISK cree"
fi

# Console serie : en mode terminal, entree/sortie dans ce terminal, copie fidele dans console.log.
case "$MODE" in
serial)
    CON="-display none -chardev stdio,id=con,mux=on,signal=off,logfile=$DIR/console.log -serial chardev:con -mon chardev=con"
    echo ">> Mode terminal : Ctrl+A puis X pour quitter."
    ;;
window | sdl | gtk)
    DISP=
    [ "$MODE" = sdl ] && DISP="-display sdl"
    [ "$MODE" = gtk ] && DISP="-display gtk"
    CON="$DISP -chardev stdio,id=con,logfile=$DIR/console.log -serial chardev:con"
    ;;
*)
    slog "mode inconnu : $MODE"
    echo "Mode inconnu : $MODE (serial, window, sdl ou gtk)" >&2
    exit 1
    ;;
esac

QD=
[ -n "$QEMU_D" ] && QD="-d $QEMU_D -D $DIR/qemu.log"

# shellcheck disable=SC2086
set -- $QEMU $QEMU_DISK -cdrom "$ISO" -no-reboot -rtc base=localtime \
    -debugcon "file:$DIR/os.log" -global isa-debugcon.iobase=0xe9 $CON $QD
slog "--- lancement de QEMU ---"
slog "commande QEMU : $*"
T1=$(date +%s)
"$@" 2>> "$SESSION"
RC=$?
T2=$(date +%s)
slog "QEMU termine : code $RC apres $((T2 - T1)) s"

# Resume : cherche les problemes dans tous les journaux et l'ecrit dans session.log ET a l'ecran.
{
    echo "--- RESUME ---"
    echo "fichiers : $(for f in session build os console qemu; do [ -f "$DIR/$f.log" ] && printf '%s.log=%s lignes  ' "$f" "$(wc -l < "$DIR/$f.log")"; done)"
    if [ -f "$DIR/os.log" ]; then
        e=$(grep -c ' ERROR ' "$DIR/os.log"); w=$(grep -c ' WARN ' "$DIR/os.log")
        echo "os.log   : $e erreur(s), $w avertissement(s) ; panique : $(grep -c 'PANIQUE' "$DIR/os.log")"
        grep -m 5 ' ERROR ' "$DIR/os.log" | sed 's/^/           /'
    fi
    if [ -f "$DIR/qemu.log" ]; then
        echo "qemu.log : triple faute : $(grep -c 'Triple fault' "$DIR/qemu.log") ; exceptions CPU (v=0d, v=0e...) : $(grep -c 'v=0[0-9a-f] ' "$DIR/qemu.log")"
        grep -m 3 -i 'triple fault\|guest_errors\|unimp' "$DIR/qemu.log" | sed 's/^/           /'
    fi
    echo "dossier  : $DIR   (derniere session : logs/latest)"
} | tee -a "$SESSION"
exit "$RC"
