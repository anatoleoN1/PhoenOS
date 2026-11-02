/*
 * Test du client TLS 1.3 (tls.c) contre un VRAI serveur : "openssl s_server" lance par tests/tls_interop.sh en boucle locale.
 * Scenarios : ECDSA P-256 avec intermediaire, RSA-PSS, Ed25519, ECDSA P-384 ; les trois suites ; un fichier de 300 000 octets
 * (verifie par SHA-256) ; certificat demande au client ; KeyUpdate ; mauvais nom, certificat expire, autorite inconnue ;
 * serveur TLS 1.2 seulement ; octets alteres dans la poignee de main et dans les donnees.
 * Le test tourne en 32 bits avec les memes options que le noyau (pas de libgcc).
 */
#include "testlib.h"
#include "tls.h"
#include "x509.h"
#include "rng.h"

u8 test_scancode;
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }

/* ---- Appels systeme Linux 32 bits ---- */
static long sys5(long n, long a, long b, long c, long d, long e)
{
    long r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e) : "memory");
    return r;
}
static long sockcall(long call, long *args) { return sys3(102, call, (long)args, 0); }

static int tcp_connect_local(int port)
{
    long a[6];
    u8 addr[16] = {2, 0, (u8)(port >> 8), (u8)port, 127, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    long tv[2] = {8, 0};
    long fd;
    a[0] = 2; a[1] = 1; a[2] = 0;
    fd = sockcall(1, a);
    if (fd < 0) return -1;
    a[0] = fd; a[1] = (long)addr; a[2] = 16;
    if (sockcall(3, a) < 0) { sys3(6, fd, 0, 0); return -1; }
    a[0] = fd; a[1] = 1; a[2] = 20; a[3] = (long)tv; a[4] = 8;          /* SO_RCVTIMEO */
    sockcall(14, a);
    return (int)fd;
}

struct conn { int fd; u32 flip_at, count; int flipped; };

static int c_send(void *ctx, const u8 *d, u32 n)
{
    struct conn *c = ctx;
    while (n) {
        long r = sys3(4, c->fd, (long)d, (long)n);
        if (r <= 0) return -1;
        d += r; n -= (u32)r;
    }
    return 0;
}

static int c_recv(void *ctx, u8 *buf, u32 cap)
{
    struct conn *c = ctx;
    long r = sys3(3, c->fd, (long)buf, (long)cap);
    if (r <= 0) return r == 0 ? 0 : -1;
    if (c->flip_at && c->count <= c->flip_at && c->flip_at < c->count + (u32)r) {
        buf[c->flip_at - c->count] ^= 0x01;
        c->flipped = 1;
    }
    c->count += (u32)r;
    return (int)r;
}

static u8 filebuf[4096];
static int read_file(const char *name, u8 *dst, int cap)
{
    long fd = sys3(5, (long)name, 0, 0);
    int n = 0;
    if (fd < 0) return -1;
    for (;;) {
        long r = sys3(3, fd, (long)(dst + n), cap - n);
        if (r <= 0) break;
        n += (int)r;
    }
    sys3(6, fd, 0, 0);
    return n;
}

static struct tls T;
static u8 resp[400000];
static u8 cader[2][1500];
static const u8 *rootp[1];
static u32 rootl[1];
static u32 now_s;
static int base_port;

static int find(const u8 *hay, u32 hn, const char *needle)
{
    u32 nl = (u32)strlen(needle);
    for (u32 i = 0; i + nl <= hn; i++)
        if (memcmp(hay + i, needle, nl) == 0) return 1;
    return 0;
}

/* Poignee de main puis GET ; renvoie le code de connexion, ou 0 et remplit resp et rl. */
static int session(int port_off, const char *host, u32 flip, const char *req, u32 *rl, int *suite, int *flipped)
{
    struct conn c = {0, flip, 0, 0};
    struct tls_io io = {c_send, c_recv, &c};
    int r;
    u32 n = 0;
    c.fd = tcp_connect_local(base_port + port_off);
    if (c.fd < 0) { out("  connexion locale impossible (port "); outnum((u32)(base_port + port_off)); out(")\n"); test_fails++; return -100; }
    r = tls_connect(&T, &io, host, now_s);
    if (suite) *suite = T.suite;
    if (r == TLS_OK) {
        r = tls_send(&T, (const u8 *)req, (u32)strlen(req));
        if (r > 0) r = TLS_OK;
        while (r == TLS_OK) {
            int k = tls_recv(&T, resp + n, (u32)sizeof resp - n);
            if (k == 0) break;
            if (k < 0) { r = k; break; }
            n += (u32)k;
        }
        if (r == TLS_OK) {
            CHECK(tls_recv(&T, resp, 10) == 0);                 /* apres close_notify : toujours 0 */
            tls_close(&T);
        }
    }
    *rl = n;
    if (flipped) *flipped = c.flipped;
    sys3(6, c.fd, 0, 0);
    return r;
}

static void expect_ok(int off, const char *what, int want_suite, const char *cipher_name)
{
    u32 n; int suite = 0, r;
    r = session(off, "tls.test", 0, "GET / HTTP/1.0\r\n\r\n", &n, &suite, NULL);
    if (r != TLS_OK) { out("  "); out(what); out(" : echec "); outnum((u32)(-r)); out(" ("); out(tls_strerror(r)); out(") alerte "); outnum((u32)T.alert); out(" cert "); outnum((u32)(-T.cert_err)); out("\n"); test_fails++; return; }
    CHECK(find(resp, n, "HTTP/1.0 200 ok"));
    if (want_suite) CHECK(suite == want_suite);
    if (cipher_name) {
        if (!find(resp, n, cipher_name)) { out("  "); out(what); out(" : suite "); out(cipher_name); out(" absente de la reponse\n"); test_fails++; }
    }
}

static int expect_fail(int off, const char *host, int want, int want_cert, const char *what)
{
    u32 n; int r = session(off, host, 0, "GET / HTTP/1.0\r\n\r\n", &n, NULL, NULL);
    if (r != want || (want == TLS_E_CERT && T.cert_err != want_cert)) {
        out("  "); out(what); out(" : obtenu "); outnum((u32)(-r)); out(" cert "); outnum((u32)(-T.cert_err)); out(", attendu "); outnum((u32)(-want)); out(" cert "); outnum((u32)(-want_cert)); out("\n");
        test_fails++;
    }
    return r;
}

int test_main(void)
{
    int n;
    u32 rl;
    static u8 hexsha[80];
    static u8 big_sha[32];
    /* configuration ecrite par tls_interop.sh */
    n = read_file("tls_base_port", filebuf, 64);
    if (n <= 0) { out("  tls_base_port absent (lance par tls_interop.sh)\n"); return 1; }
    for (int i = 0; i < n && filebuf[i] >= '0' && filebuf[i] <= '9'; i++) base_port = base_port * 10 + (filebuf[i] - '0');
    now_s = (u32)sys3(13, 0, 0, 0);
    {
        long l0 = read_file("pki/ca.der", cader[0], sizeof cader[0]);
        CHECK(l0 > 100);
        rootp[0] = cader[0]; rootl[0] = (u32)l0;
        x509_set_roots(rootp, rootl, 1);
    }
    rng_init();
    CHECK(rng_ready());

    /* 1. les quatre types de certificat et les trois suites */
    expect_ok(0, "ECDSA P-256 + intermediaire, AES-128-GCM", 0x1301, "TLS_AES_128_GCM_SHA256");
    expect_ok(1, "RSA-PSS, AES-256-GCM", 0x1302, "TLS_AES_256_GCM_SHA384");
    expect_ok(2, "Ed25519, ChaCha20-Poly1305", 0x1303, "TLS_CHACHA20_POLY1305_SHA256");
    expect_ok(3, "ECDSA P-384", 0, NULL);
    expect_ok(6, "serveur qui demande un certificat client", 0, NULL);

    /* 2. gros fichier : 300 000 octets, verifies par SHA-256 */
    {
        int r = session(4, "tls.test", 0, "GET /big.bin HTTP/1.0\r\n\r\n", &rl, NULL, NULL);
        CHECK(r == TLS_OK);
        u32 hdr = 0;
        for (u32 i = 0; i + 4 <= rl; i++)
            if (!memcmp(resp + i, "\r\n\r\n", 4)) { hdr = i + 4; break; }
        CHECK(hdr > 0 && rl - hdr == 300000u);
        sha256(big_sha, resp + hdr, rl - hdr);
        n = read_file("big.sha256", hexsha, 70);
        CHECK(n >= 64);
        {
            u8 want[32];
            for (int i = 0; i < 32; i++) {
                u8 v[2];
                for (int k = 0; k < 2; k++) { u8 ch = hexsha[2 * i + k]; v[k] = (u8)(ch <= '9' ? ch - '0' : ch - 'a' + 10); }
                want[i] = (u8)(v[0] << 4 | v[1]);
            }
            CHECK(memcmp(want, big_sha, 32) == 0);
        }
    }

    /* 3. refus attendus */
    expect_fail(7, "tls.test", TLS_E_CERT, X509_E_HOST, "mauvais nom dans le certificat");
    expect_fail(0, "autre.test", TLS_E_CERT, X509_E_HOST, "nom demande different");
    expect_fail(8, "tls.test", TLS_E_CERT, X509_E_EXPIRED, "certificat expire");
    expect_fail(9, "tls.test", TLS_E_CERT, X509_E_UNTRUSTED, "autorite inconnue");
    {
        u32 nn; int r = session(5, "tls.test", 0, "GET / HTTP/1.0\r\n\r\n", &nn, NULL, NULL);
        CHECK(r == TLS_E_UNSUPPORTED || r == TLS_E_ALERT);        /* serveur TLS 1.2 seulement */
    }

    /* 4. octets alteres en route : la connexion ne doit JAMAIS aboutir proprement */
    {
        static const u32 offs[] = {3, 7, 20, 50, 90, 100, 104, 130, 250, 500, 800, 1000, 1200};
        int tested = 0;
        for (int i = 0; i < (int)(sizeof offs / sizeof offs[0]); i++) {
            u32 nn; int fl = 0;
            int r = session(0, "tls.test", offs[i], "GET / HTTP/1.0\r\n\r\n", &nn, NULL, &fl);
            if (!fl) continue;
            tested++;
            if (r == TLS_OK) { out("  alteration a l'octet "); outnum(offs[i]); out(" : connexion acceptee !\n"); test_fails++; }
        }
        CHECK(tested >= 10);
        out("  alterations testees : "); outnum((u32)tested); out(" positions, toutes refusees\n");
    }

    /* 4b. serveurs qui trichent (tests/fake_tls_server.py) : la liste est dans tls_faults.txt (nom, code d'erreur attendu) */
    {
        static u8 list[1024];
        int len = read_file("tls_faults.txt", list, sizeof list - 1), idx = 0, ntested = 0;
        CHECK(len > 0);
        for (int i = 0; i < len;) {
            int j = i, want = 0;
            while (j < len && list[j] != ' ') j++;
            u32 nl = (u32)(j - i);
            char name[40];
            if (nl >= sizeof name) break;
            memcpy(name, list + i, nl); name[nl] = 0;
            j++;
            while (j < len && list[j] >= '0' && list[j] <= '9') want = want * 10 + (list[j++] - '0');
            while (j < len && list[j] != '\n') j++;
            i = j + 1;
            {
                u32 nn; int r = session(20 + idx, "tls.test", 0, "x", &nn, NULL, NULL);
                if (want == 0 ? !T.established : r != -want) {      /* controle : poignee de main reussie (le serveur ferme ensuite sans close_notify) */
                    out("  serveur tricheur \""); out(name); out("\" : obtenu "); outnum((u32)(-r)); out(", attendu "); outnum((u32)want); out("\n");
                    test_fails++;
                }
                ntested++;
            }
            idx++;
        }
        CHECK(ntested >= 10);
        out("  serveurs tricheurs refuses : "); outnum((u32)ntested - 1); out(" fautes injectees (faux CertificateVerify, faux Finished, secret nul, ...)\n");
    }

    /* 5. KeyUpdate (serveur pilote par tls_interop.sh : "before", K, "mid", k, "after") */
    {
        struct conn c = {0, 0, 0, 0};
        struct tls_io io = {c_send, c_recv, &c};
        u32 got = 0;
        static u8 ku[200];
        int r;
        c.fd = tcp_connect_local(base_port + 10);
        CHECK(c.fd >= 0);
        r = tls_connect(&T, &io, "tls.test", now_s);
        CHECK(r == TLS_OK);
        while (r == TLS_OK && got < 17) {                       /* "before\nmid\nafter\n" = 17 octets */
            int k = tls_recv(&T, ku + got, (u32)sizeof ku - got);
            if (k <= 0) { r = k ? k : -99; break; }
            got += (u32)k;
        }
        if (!(r == TLS_OK && got == 17 && memcmp(ku, "before\nmid\nafter\n", 17) == 0)) {
            out("  KeyUpdate : r="); outnum((u32)(r < 0 ? -r : r)); out(" got="); outnum(got); out(" donnees=["); ku[got < 199 ? got : 199] = 0; out((const char *)ku); out("]\n");
        }
        CHECK(r == TLS_OK && got == 17 && memcmp(ku, "before\nmid\nafter\n", 17) == 0);
        CHECK(tls_send(&T, (const u8 *)"ping\n", 5) == 5);       /* avec les nouvelles cles d'emission */
        CHECK(tls_send(&T, (const u8 *)"pong\n", 5) == 5);
        tls_close(&T);
        sys3(6, c.fd, 0, 0);
    }
    out("  TLS 1.3 verifie contre un vrai serveur OpenSSL : ECDSA, RSA-PSS, Ed25519, trois suites, 300 000 octets, KeyUpdate, refus de certificat, alterations\n");
    return 0;
}
