/*
 * Test de la colle HTTPS (tlssock.c) avec de FAUX tcp et tls : une seule session a la fois, reservee a son proprietaire,
 * liberee quand le programme se termine, traduction des erreurs (E_CERT / E_TLS / erreurs TCP), texte de la derniere erreur.
 */
#include "testlib.h"
#include "tlssock.h"
#include "tcp.h"
#include "tls.h"

u8 test_scancode;
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }

static int tcp_open_n, tls_mode, tcp_mode;      /* tls_mode : 0 ok, 1 certificat, 2 autre erreur, 3 alerte ; tcp_mode : 0 ok, 1 refus */
static u32 last_now;
u32 rtc_now_unix(void) { return 1800000000u; }
const char *x509_strerror(int e) { (void)e; return "nom incorrect"; }
const char *tls_strerror(int e) { (void)e; return "reponse du serveur incorrecte"; }
int rng_ready(void) { return 1; }

err_t tcp_connect(u32 owner, u32 ip, u16 port, int *h)
{
    (void)owner; (void)ip; (void)port;
    if (tcp_mode == 1) return E_REFUSED;
    tcp_open_n++;
    *h = 2;
    return E_OK;
}
err_t tcp_close(u32 owner, int h) { (void)owner; (void)h; tcp_open_n--; return E_OK; }
err_t tcp_send(u32 owner, int h, const u8 *d, u32 n, u32 *sent) { (void)owner; (void)h; (void)d; *sent = n; return E_OK; }
i32 tcp_recv(u32 owner, int h, u8 *b, u32 cap) { (void)owner; (void)h; (void)b; (void)cap; return 0; }

int tls_connect(struct tls *t, const struct tls_io *io, const char *host, u32 now)
{
    (void)io; (void)host;
    last_now = now;
    t->cert_err = -6; t->alert = 40;
    t->established = 1;
    return tls_mode == 0 ? TLS_OK : tls_mode == 1 ? TLS_E_CERT : tls_mode == 2 ? TLS_E_PROTOCOL : TLS_E_ALERT;
}
int tls_send(struct tls *t, const u8 *d, u32 n) { (void)t; (void)d; return (int)n; }
int tls_recv(struct tls *t, u8 *b, u32 cap) { (void)t; (void)b; (void)cap; return 7; }
void tls_close(struct tls *t) { (void)t; }

static int has(const char *s, const char *needle)
{
    u32 nl = (u32)strlen(needle), sl = (u32)strlen(s);
    for (u32 i = 0; i + nl <= sl; i++) if (!memcmp(s + i, needle, nl)) return 1;
    return 0;
}

int test_main(void)
{
    int h = -1;
    u8 b[8];

    tlss_reset();
    CHECK(tlss_open_count() == 0);
    CHECK(tlss_send(1, 0, b, 1) == E_INVAL && tlss_recv(1, 0, b, 1) == E_INVAL && tlss_close(1, 0) == E_INVAL);   /* rien d'ouvert */

    tls_mode = 1;                                              /* certificat refuse */
    CHECK(tlss_connect(1, "a.test", 1, 443, &h) == E_CERT);
    CHECK(has(tlss_last_error(), "certificat refuse") && has(tlss_last_error(), "nom incorrect"));
    CHECK(tlss_open_count() == 0 && tcp_open_n == 0);          /* la connexion TCP a ete refermee */
    tls_mode = 2;
    CHECK(tlss_connect(1, "a.test", 1, 443, &h) == E_TLS);
    CHECK(has(tlss_last_error(), "reponse du serveur incorrecte"));
    tls_mode = 3;
    CHECK(tlss_connect(1, "a.test", 1, 443, &h) == E_TLS);
    CHECK(has(tlss_last_error(), "alerte TLS du serveur, code : 40"));
    tcp_mode = 1;
    CHECK(tlss_connect(1, "a.test", 1, 443, &h) == E_REFUSED);
    CHECK(has(tlss_last_error(), "connexion TCP impossible"));
    tcp_mode = 0; tls_mode = 0;

    CHECK(tlss_connect(1, "a.test", 1, 443, &h) == E_OK && h == 0);
    CHECK(last_now == 1800000000u);                            /* l'heure de l'horloge est transmise */
    CHECK(tlss_open_count() == 1 && tcp_open_n == 1);
    CHECK(has(tlss_last_error(), "aucune erreur"));
    CHECK(tlss_connect(2, "b.test", 1, 443, &h) == E_BUSY);    /* une seule session */
    CHECK(tcp_open_n == 1);
    CHECK(tlss_send(2, 0, b, 3) == E_INVAL && tlss_recv(2, 0, b, 3) == E_INVAL && tlss_close(2, 0) == E_INVAL);   /* pas son proprietaire */
    CHECK(tlss_send(1, 1, b, 3) == E_INVAL);                   /* mauvais numero */
    CHECK(tlss_send(1, 0, b, 3) == 3 && tlss_recv(1, 0, b, 8) == 7);
    tlss_release_owner(2);                                     /* un autre programme se termine : rien ne change */
    CHECK(tlss_open_count() == 1);
    tlss_release_owner(1);                                     /* son proprietaire se termine */
    CHECK(tlss_open_count() == 0 && tcp_open_n == 0);
    CHECK(tlss_connect(2, "b.test", 1, 443, &h) == E_OK);
    CHECK(tlss_close(2, 0) == E_OK && tlss_open_count() == 0 && tcp_open_n == 0);
    out("  Colle HTTPS verifiee : une session, proprietaire, liberation, traduction des erreurs\n");
    return 0;
}
