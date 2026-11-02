/* Connexions HTTPS pour les programmes : colle entre tcp.c et tls.c. Voir tlssock.h. */
#include "tlssock.h"
#include "tcp.h"
#include "tls.h"
#include "x509.h"
#include "rtc.h"
#include "string.h"

static struct tls g_tls;                 /* la session (la seule) : en memoire statique, pas dans la pile */
static int g_used;
static u32 g_owner;
static int g_tcp = -1;
static char g_err[160] = "aucune erreur";

static void set_err(const char *a, const char *b)
{
    u32 n = 0;
    while (a && *a && n < sizeof g_err - 1) g_err[n++] = *a++;
    if (b && *b) {
        if (n && n < sizeof g_err - 3) { g_err[n++] = ' '; g_err[n++] = ':'; g_err[n++] = ' '; }
        while (*b && n < sizeof g_err - 1) g_err[n++] = *b++;
    }
    g_err[n] = 0;
}

void tlss_reset(void)
{
    g_used = 0;
    g_owner = 0;
    g_tcp = -1;
    strcpy(g_err, "aucune erreur");
}

int tlss_open_count(void) { return g_used; }
const char *tlss_last_error(void) { return g_err; }

/* Transport pour tls.c : tout passe par la connexion TCP du proprietaire. */
static int io_send(void *ctx, const u8 *d, u32 n)
{
    (void)ctx;
    while (n) {
        u32 sent = 0;
        err_t e = tcp_send(g_owner, g_tcp, d, n, &sent);
        if (sent == 0 && e != E_OK)
            return -1;
        d += sent; n -= sent;
        if (sent == 0)
            return -1;
    }
    return 0;
}

static int io_recv(void *ctx, u8 *buf, u32 cap)
{
    i32 r;
    (void)ctx;
    r = tcp_recv(g_owner, g_tcp, buf, cap);
    return r < 0 ? -1 : (int)r;
}

static err_t map_error(int r)
{
    if (r == TLS_E_CERT)
        return E_CERT;
    return E_TLS;
}

static void note_failure(int r, const struct tls *t)
{
    if (r == TLS_E_CERT)
        set_err("certificat refuse", x509_strerror(t->cert_err));
    else if (r == TLS_E_ALERT) {
        char num[12];
        u32 v = (u32)t->alert, i = 11;
        num[i] = 0;
        do { num[--i] = (char)('0' + v % 10); v /= 10; } while (v);
        set_err("alerte TLS du serveur, code", num + i);
    } else
        set_err("connexion securisee impossible", tls_strerror(r));
}

err_t tlss_connect(u32 owner, const char *host, u32 ip, u16 port, int *h)
{
    struct tls_io io;
    int r;
    err_t e;
    if (g_used)
        return E_BUSY;
    e = tcp_connect(owner, ip, port, &g_tcp);
    if (e != E_OK) {
        set_err("connexion TCP impossible", err_str(e));
        return e;
    }
    g_used = 1;
    g_owner = owner;
    io.send = io_send; io.recv = io_recv; io.ctx = NULL;
    r = tls_connect(&g_tls, &io, host, rtc_now_unix());
    if (r != TLS_OK) {
        note_failure(r, &g_tls);
        tcp_close(owner, g_tcp);
        g_used = 0;
        g_tcp = -1;
        return map_error(r);
    }
    set_err("aucune erreur", NULL);
    *h = 0;
    return E_OK;
}

static int check(u32 owner, int h)
{
    return g_used && h == 0 && owner == g_owner;
}

i32 tlss_send(u32 owner, int h, const u8 *data, u32 len)
{
    int r;
    if (!check(owner, h))
        return E_INVAL;
    r = tls_send(&g_tls, data, len);
    if (r < 0) {
        note_failure(r, &g_tls);
        return map_error(r);
    }
    return r;
}

i32 tlss_recv(u32 owner, int h, u8 *buf, u32 cap)
{
    int r;
    if (!check(owner, h))
        return E_INVAL;
    r = tls_recv(&g_tls, buf, cap);
    if (r < 0) {
        note_failure(r, &g_tls);
        return map_error(r);
    }
    return r;
}

err_t tlss_close(u32 owner, int h)
{
    if (!check(owner, h))
        return E_INVAL;
    tls_close(&g_tls);
    tcp_close(owner, g_tcp);
    g_used = 0;
    g_tcp = -1;
    return E_OK;
}

void tlss_release_owner(u32 owner)
{
    if (g_used && g_owner == owner) {
        tls_close(&g_tls);
        tcp_close(owner, g_tcp);
        g_used = 0;
        g_tcp = -1;
    }
}
