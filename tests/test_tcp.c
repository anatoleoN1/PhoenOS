/*
 * Test du client TCP (tcp.c) contre un FAUX serveur branche sur la VRAIE pile reseau (net.c, ARP, IPv4) :
 * selon le port demande, il accepte (et renvoie ce qu'il recoit), refuse (RST), reste muet, ferme apres
 * avoir repondu, ou abime une reponse pour forcer une reemission. Une trame falsifiee est ajoutee pour
 * verifier qu'elle est ignoree.
 */
#include "testlib.h"
#include "netdev.h"
#include "net.h"
#include "tcp.h"
#include "sched.h"
#include "string.h"

u8 test_scancode;
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }

void vga_putc(char c) { (void)c; }
void serial_putc(char c) { (void)c; }
u32 pit_uptime_ms(void) { return 0; }
err_t sched_create(const char *name, void (*entry)(void *), void *arg, void (*cleanup)(void *), void *owner, struct task **out)
{
    (void)name; (void)entry; (void)arg; (void)cleanup; (void)owner; (void)out;
    return E_OK;
}
void sched_detach(struct task *t) { (void)t; }
void sched_sleep_ms(u32 ms) { (void)ms; }

/* ---------- horloge simulee ---------- */
static u32 clock_ms;
u32 net_now_ms(void) { return clock_ms; }
void net_delay_ms(u32 ms) { clock_ms += ms; }

/* ---------- utilitaires ---------- */
static u16 get16(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static u32 get32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static void put16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

static u16 cs(const u8 *p, u32 n, u32 init)
{
    u32 s = init;

    for (; n > 1; n -= 2, p += 2) s += get16(p);
    if (n) s += (u32)p[0] << 8;
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (u16)~s;
}

/* Somme TCP avec pseudo-en-tete, ecrite independamment de tcp.c (le test ne reutilise pas le code teste). */
static u16 pcs(u32 src, u32 dst, u8 proto, const u8 *seg, u32 len)
{
    u8 ph[12];
    u32 s = 0;

    put32(ph, src); put32(ph + 4, dst); ph[8] = 0; ph[9] = proto; put16(ph + 10, (u16)len);
    for (int i = 0; i < 12; i += 2) s += get16(ph + i);
    return cs(seg, len, s);
}

/* ---------- le faux reseau ---------- */
static const u8 OUR_MAC[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static const u8 SRV_MAC[6] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x14 };
#define IP_US  0x0A00020Fu                  /* 10.0.2.15 */
#define IP_SRV 0x0A000214u                  /* 10.0.2.20 */

#define QN 64
struct frame { u8 d[1600]; u32 len; };
static struct frame rq[QN];
static int rqh, rqt;
static int dups_seen, bad_seen;

static void enqueue(const u8 *f, u32 len)
{
    if ((rqt + 1) % QN == rqh || len > sizeof rq[0].d)
        return;
    memcpy(rq[rqt].d, f, len);
    rq[rqt].len = len;
    rqt = (rqt + 1) % QN;
}

static int fake_recv(struct netdev *d, u8 *buf, u32 cap)
{
    u32 n;

    (void)d;
    if (rqh == rqt)
        return 0;
    n = rq[rqh].len < cap ? rq[rqh].len : cap;
    memcpy(buf, rq[rqh].d, n);
    rqh = (rqh + 1) % QN;
    return (int)n;
}

static int fake_link(struct netdev *d) { (void)d; return 1; }

/* Un serveur par port : 80 echo ; 81 refuse ; 82 muet ; 83 echo puis ferme ; 84 premiere reponse abimee ; 85 ne repond pas. */
struct conn { int used; u16 cport, sport; u32 snd, rcv; int corrupt_done; u8 last[1460]; u32 last_len, last_seq; };
static struct conn cs_tab[16];

static void srv_seg(const struct conn *c, u8 flags, u32 seq, const u8 *data, u32 len, int corrupt)
{
    u8 seg[20 + 1460];
    u8 f[1600];
    u32 n = 0;

    put16(seg, c->sport); put16(seg + 2, c->cport);
    put32(seg + 4, seq); put32(seg + 8, c->rcv);
    seg[12] = 0x50; seg[13] = flags; put16(seg + 14, 8192); put16(seg + 16, 0); put16(seg + 18, 0);
    if (len) memcpy(seg + 20, data, len);
    put16(seg + 16, pcs(IP_SRV, IP_US, 6, seg, 20 + len));
    if (corrupt)
        seg[16] ^= 0x55;
    /* trame Ethernet + IPv4 */
    memcpy(f, OUR_MAC, 6); memcpy(f + 6, SRV_MAC, 6); put16(f + 12, 0x0800);
    f[14] = 0x45; f[15] = 0; put16(f + 16, (u16)(20 + 20 + len)); put16(f + 18, 7); put16(f + 20, 0x4000);
    f[22] = 64; f[23] = 6; put16(f + 24, 0); put32(f + 26, IP_SRV); put32(f + 30, IP_US);
    put16(f + 24, cs(f + 14, 20, 0));
    memcpy(f + 34, seg, 20 + len);
    n = 14 + 20 + 20 + len;
    enqueue(f, n);
}

static void arp_answer(const u8 *req)
{
    u8 f[64];

    memcpy(f, req + 6, 6);              /* vers l'emetteur de la demande */
    memcpy(f + 6, SRV_MAC, 6); put16(f + 12, 0x0806);
    put16(f + 14, 1); put16(f + 16, 0x0800); f[18] = 6; f[19] = 4; put16(f + 20, 2);
    memcpy(f + 22, SRV_MAC, 6); put32(f + 28, IP_SRV);
    memcpy(f + 32, OUR_MAC, 6); put32(f + 38, IP_US);
    enqueue(f, 42);
}

static struct conn *conn_for(u16 cport, u16 sport, int create)
{
    struct conn *free_slot = NULL;

    for (int i = 0; i < 16; i++) {
        if (cs_tab[i].used && cs_tab[i].cport == cport && cs_tab[i].sport == sport)
            return &cs_tab[i];
        if (!cs_tab[i].used && !free_slot)
            free_slot = &cs_tab[i];
    }
    if (!create || !free_slot)
        return NULL;
    memset(free_slot, 0, sizeof *free_slot);
    free_slot->used = 1;
    free_slot->cport = cport;
    free_slot->sport = sport;
    return free_slot;
}

/* Une trame envoyee par la pile : le faux serveur reagit. */
static void peer(const u8 *f, u32 len)
{
    const u8 *ip, *tcp;
    u32 ihl, total, tlen, hl, seq, plen;
    u16 cport, sport;
    u8 flags;
    struct conn *c;

    if (len >= 42 && get16(f + 12) == 0x0806) {
        if (get16(f + 20) == 1 && get32(f + 38) == IP_SRV)
            arp_answer(f);
        return;
    }
    if (len < 14 + 20 || get16(f + 12) != 0x0800)
        return;
    ip = f + 14;
    ihl = (u32)(ip[0] & 15) * 4;
    total = get16(ip + 2);
    if (ip[9] != 6 || get32(ip + 16) != IP_SRV || total > len - 14 || total < ihl + 20)
        return;
    tcp = ip + ihl;
    tlen = total - ihl;
    if (pcs(get32(ip + 12), IP_SRV, 6, tcp, tlen) != 0) {
        bad_seen++;
        return;
    }
    cport = get16(tcp); sport = get16(tcp + 2);
    seq = get32(tcp + 4);
    flags = tcp[13];
    hl = (u32)(tcp[12] >> 4) * 4;
    if (hl < 20 || hl > tlen)
        return;
    plen = tlen - hl;

    if (flags & 0x04)                                   /* RST : rien a faire */
        return;
    if ((flags & 0x02) && !(flags & 0x10)) {            /* SYN */
        struct conn fresh;

        if (sport == 85)
            return;                                     /* ne repond pas */
        if (sport == 81) {                              /* refuse */
            memset(&fresh, 0, sizeof fresh);
            fresh.cport = cport; fresh.sport = sport; fresh.rcv = seq + 1;
            srv_seg(&fresh, 0x14, 0, NULL, 0, 0);
            return;
        }
        c = conn_for(cport, sport, 1);
        if (!c)
            return;
        c->snd = 5000u + cport + 1;
        c->rcv = seq + 1;
        srv_seg(c, 0x12, c->snd - 1, NULL, 0, 0);       /* SYN-ACK */
        return;
    }
    c = conn_for(cport, sport, 0);
    if (!c)
        return;
    if (plen && seq != c->rcv) {                        /* doublon : on renvoie la derniere reponse */
        dups_seen++;
        if (c->last_len)
            srv_seg(c, 0x18, c->last_seq, c->last, c->last_len, 0);
        else
            srv_seg(c, 0x10, c->snd, NULL, 0, 0);
        return;
    }
    if (plen)
        c->rcv += plen;
    if (flags & 0x01) {                                 /* FIN du client */
        c->rcv++;
        srv_seg(c, 0x10, c->snd, NULL, 0, 0);
        return;
    }
    if (plen == 0)
        return;
    if (sport == 82) {                                  /* muet : acquitte seulement */
        srv_seg(c, 0x10, c->snd, NULL, 0, 0);
        return;
    }
    {   /* echo */
        const u8 *data = tcp + hl;
        int corrupt = (sport == 84 && !c->corrupt_done);

        if (sport == 84)
            c->corrupt_done = 1;
        if (plen > sizeof c->last)
            plen = sizeof c->last;
        memcpy(c->last, data, plen);
        c->last_len = plen;
        c->last_seq = c->snd;
        srv_seg(c, 0x18, c->snd, data, plen, corrupt);
        c->snd += plen;
        if (sport == 83) {                              /* puis le serveur ferme */
            srv_seg(c, 0x11, c->snd, NULL, 0, 0);
            c->snd++;
        }
    }
}

static err_t fake_send(struct netdev *d, const u8 *frame, u32 len)
{
    (void)d;
    peer(frame, len);
    return E_OK;
}

static struct netdev dev = { "eth0", "faux", { 0 }, fake_link, fake_send, fake_recv, NULL, 0, 0, 0, 0, 0, 0 };

/* ---------- le test ---------- */
static u8 buf[9000];
static u8 big[3000];

int test_main(void)
{
    int h1 = -1, h2 = -1, hs[4];
    u32 sent = 0, t0;
    i32 n;
    int got;

    memcpy(dev.mac, OUR_MAC, 6);
    for (u32 i = 0; i < sizeof big; i++)
        big[i] = (u8)(i * 7 + 3);

    CHECK(netdev_register(&dev) == E_OK);
    CHECK(net_init() == E_OK);
    tcp_reset();

    /* sans configuration : pas de reseau */
    CHECK(tcp_connect(7, IP_SRV, 80, &h1) == E_IO);

    net_set_config(IP_US, 0xFFFFFF00u, 0, 0);

    /* 1. connexion ouverte */
    CHECK(tcp_connect(7, IP_SRV, 80, &h1) == E_OK && h1 == 0);
    CHECK(tcp_open_count() == 1);

    /* 2. aller-retour court */
    CHECK(tcp_send(7, h1, (const u8 *)"hello", 5, &sent) == E_OK && sent == 5);
    n = tcp_recv(7, h1, buf, sizeof buf);
    CHECK(n == 5 && memcmp(buf, "hello", 5) == 0);

    /* 3. plusieurs segments (3000 octets = 3 segments) */
    CHECK(tcp_send(7, h1, big, sizeof big, &sent) == E_OK && sent == sizeof big);
    got = 0;
    while (got < (int)sizeof big) {
        n = tcp_recv(7, h1, buf + got, sizeof buf - (u32)got);
        if (n <= 0)
            break;
        got += n;
    }
    CHECK(got == (int)sizeof big && memcmp(buf, big, sizeof big) == 0);

    /* 4. une connexion n'est utilisable que par son programme */
    CHECK(tcp_recv(8, h1, buf, 10) == E_INVAL);
    CHECK(tcp_send(8, h1, (const u8 *)"x", 1, &sent) == E_INVAL);
    CHECK(tcp_close(8, h1) == E_INVAL);
    CHECK(tcp_recv(7, 9, buf, 10) == E_INVAL);

    /* 5. serveur qui refuse */
    CHECK(tcp_connect(7, IP_SRV, 81, &h2) == E_REFUSED);

    /* 6. serveur muet : la reception expire apres 15 s simulees */
    CHECK(tcp_connect(7, IP_SRV, 82, &h2) == E_OK);
    CHECK(tcp_send(7, h2, (const u8 *)"ping", 4, &sent) == E_OK && sent == 4);
    t0 = clock_ms;
    n = tcp_recv(7, h2, buf, 10);
    CHECK(n == E_TIMEOUT);
    CHECK(clock_ms - t0 >= 15000u);
    CHECK(tcp_close(7, h2) == E_OK);

    /* 7. reponse abimee : la pile la rejette et le client renvoie ses donnees */
    dups_seen = 0;
    CHECK(tcp_connect(7, IP_SRV, 84, &h2) == E_OK);
    CHECK(tcp_send(7, h2, (const u8 *)"retry", 5, &sent) == E_OK && sent == 5);
    n = tcp_recv(7, h2, buf, sizeof buf);
    CHECK(n == 5 && memcmp(buf, "retry", 5) == 0);
    CHECK(dups_seen >= 1);
    CHECK(tcp_close(7, h2) == E_OK);

    /* 8. le serveur ferme apres avoir repondu : 0 octet = fin normale */
    CHECK(tcp_connect(7, IP_SRV, 83, &h2) == E_OK);
    CHECK(tcp_send(7, h2, (const u8 *)"bye", 3, &sent) == E_OK && sent == 3);
    n = tcp_recv(7, h2, buf, sizeof buf);
    CHECK(n == 3 && memcmp(buf, "bye", 3) == 0);
    n = tcp_recv(7, h2, buf, sizeof buf);
    CHECK(n == 0);
    CHECK(tcp_close(7, h2) == E_OK);
    CHECK(tcp_open_count() == 1);                       /* il reste la connexion 1 */

    /* 9. un segment falsifie (somme fausse) est ignore */
    tcp_release_owner(7);
    CHECK(tcp_open_count() == 0);
    CHECK(tcp_connect(7, IP_SRV, 80, &h2) == E_OK);
    {
        struct conn *c = NULL;
        u8 forged[8];

        for (int i = 0; i < 16; i++)                    /* la connexion la plus recente vers le port 80 */
            if (cs_tab[i].used && cs_tab[i].sport == 80 && (!c || cs_tab[i].cport > c->cport))
                c = &cs_tab[i];
        CHECK(c != NULL);
        if (c) {
            memcpy(forged, "XXXX", 4);
            srv_seg(c, 0x18, c->snd, forged, 4, 1);    /* somme volontairement fausse */
            CHECK(tcp_send(7, h2, (const u8 *)"ok", 2, &sent) == E_OK && sent == 2);
            n = tcp_recv(7, h2, buf, sizeof buf);
            CHECK(n == 2 && memcmp(buf, "ok", 2) == 0);
        }
    }
    CHECK(tcp_close(7, h2) == E_OK);

    /* 10. quatre connexions au plus, puis E_BUSY */
    for (int i = 0; i < 4; i++)
        CHECK(tcp_connect(7, IP_SRV, 80, &hs[i]) == E_OK && hs[i] == i);
    CHECK(tcp_connect(7, IP_SRV, 80, &h2) == E_BUSY);
    CHECK(tcp_open_count() == 4);
    tcp_release_owner(7);
    CHECK(tcp_open_count() == 0);

    /* 11. le serveur ne repond pas au SYN : delai depasse */
    t0 = clock_ms;
    CHECK(tcp_connect(7, IP_SRV, 85, &h2) == E_TIMEOUT);
    CHECK(clock_ms - t0 >= 10000u);
    CHECK(tcp_open_count() == 0);

    out("  TCP verifie : connexion, echo, segments multiples, refus, delai, reemission, fermeture, proprietaires, table pleine\n");
    return 0;
}
