#!/bin/bash
# Lance les serveurs "openssl s_server" puis le test TLS (build/test/tls). Appele par tests/run.sh.
# Usage : tls_interop.sh <dossier de build des tests> <racine du depot>
B=$1; ROOT=$2
command -v openssl >/dev/null 2>&1 || { echo "  (openssl absent : test TLS ignore)"; exit 0; }
python3 -c "import cryptography" 2>/dev/null || { echo "  (python3 'cryptography' absent : test TLS ignore)"; exit 0; }
T="$B/tlswork"; rm -rf "$T"; mkdir -p "$T"
python3 "$ROOT/tests/gen_tls_pki.py" "$T/pki" || exit 1
cd "$T" || exit 1
head -c 300000 /dev/urandom > big.bin; sha256sum big.bin | cut -d' ' -f1 > big.sha256
BASE=$(( 20000 + $$ % 20000 ))
echo $BASE > tls_base_port
PIDS=""
start() {  # port, options...
    p=$1; shift
    openssl s_server -accept $((BASE + p)) -quiet "$@" >/dev/null 2>&1 &
    PIDS="$PIDS $!"
}
P=pki
start 0 -cert $P/srv_ec.pem -key $P/srv_ec.key -cert_chain $P/inter.pem -tls1_3 -ciphersuites TLS_AES_128_GCM_SHA256 -www
start 1 -cert $P/srv_rsa.pem -key $P/srv_rsa.key -tls1_3 -ciphersuites TLS_AES_256_GCM_SHA384 -www
start 2 -cert $P/srv_ed.pem -key $P/srv_ed.key -tls1_3 -ciphersuites TLS_CHACHA20_POLY1305_SHA256 -www
start 3 -cert $P/srv_p384.pem -key $P/srv_p384.key -tls1_3 -www
start 4 -cert $P/srv_ec.pem -key $P/srv_ec.key -cert_chain $P/inter.pem -tls1_3 -WWW
start 5 -cert $P/srv_ec.pem -key $P/srv_ec.key -cert_chain $P/inter.pem -tls1_2 -www
start 6 -cert $P/srv_ec.pem -key $P/srv_ec.key -cert_chain $P/inter.pem -tls1_3 -verify 1 -CAfile $P/ca.pem -www
start 7 -cert $P/srv_wrongname.pem -key $P/srv_wrongname.key -tls1_3 -www
start 8 -cert $P/srv_expired.pem -key $P/srv_expired.key -tls1_3 -www
start 9 -cert $P/srv_other_ca.pem -key $P/srv_other_ca.key -tls1_3 -www
# serveur interactif pour KeyUpdate : K = KeyUpdate avec demande de reponse, k = sans
mkfifo kufifo
openssl s_server -accept $((BASE + 10)) -cert $P/srv_ec.pem -key $P/srv_ec.key -cert_chain $P/inter.pem -tls1_3 < kufifo > ku_server.log 2>&1 &
PIDS="$PIDS $!"
( sleep 3; echo before; sleep 0.5; echo K; sleep 0.5; echo mid; sleep 0.5; echo k; sleep 0.5; echo after; sleep 300 ) > kufifo &
FEED=$!
# serveurs tricheurs : un par faute, une connexion chacun
cp "$ROOT/tests/tls_faults.txt" tls_faults.txt
idx=0
while read -r name want; do
    [ -z "$name" ] && continue
    python3 "$ROOT/tests/fake_tls_server.py" $((BASE + 20 + idx)) "$name" $P/srv_ec.pem $P/srv_ec.key $P/inter.pem > "fake_$name.log" 2>&1 &
    PIDS="$PIDS $!"
    idx=$((idx + 1))
done < tls_faults.txt
# attendre que les ports ecoutent (sans s'y connecter : le serveur interactif n'accepte qu'un client)
i=0
while [ $i -lt 100 ]; do
    ok=1
    for p in 0 1 2 3 4 5 6 7 8 9 10 $(seq 20 $((19 + idx))); do
        if command -v ss >/dev/null 2>&1; then ss -ltn 2>/dev/null | grep -q ":$((BASE + p)) " || ok=0; else sleep 2; break; fi
    done
    [ $ok = 1 ] && break
    sleep 0.2; i=$((i + 1))
done
cp "$B/tls" ./tls 2>/dev/null
./tls; rc=$?
kill $PIDS $FEED 2>/dev/null; wait 2>/dev/null
sleep 0.5
if [ $rc -eq 0 ]; then
    while read -r name want; do
        [ -z "$name" ] && continue
        if [ "$name" = ok ]; then
            grep -q "RESULT client_finished_ok" "fake_$name.log" || { echo "  serveur de controle : le Finished du client n'a pas ete accepte ($(cat fake_$name.log))"; rc=1; }
        else
            ! grep -q "RESULT client_finished_ok" "fake_$name.log" || { echo "  faute $name : le client a termine la poignee de main !"; rc=1; }
        fi
    done < tls_faults.txt
fi
if [ $rc -eq 0 ]; then
    grep -q ping ku_server.log && grep -q pong ku_server.log || { echo "  KeyUpdate : le serveur n'a pas recu ping/pong apres le changement de cles"; rc=1; }
fi
exit $rc
