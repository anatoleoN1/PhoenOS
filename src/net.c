/*
 * PhoenOS - net.c : pile reseau minimale (voir net.h). Tout ce qui vient du fil est hostile : chaque lecture est bornee.
 */
#include "net.h"
#include "dlog.h"
#include "string.h"
#include "sched.h"
#include "tcp.h"

#define ETH_HDR   14
#define ETYPE_IP  0x0800
#define ETYPE_ARP 0x0806
#define IP_HDR    20
#define PROTO_ICMP 1
#define PROTO_TCP 6
#define PROTO_UDP 17
#define PORT_DHCP_C 68
#define PORT_DHCP_S 67
#define PORT_DNS  53
#define DNS_SRC_PORT 40053

static const u8 BCAST[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

struct arp_ent { u32 ip; u8 mac[6]; int valid; };

static struct {
    struct netdev *dev;
    struct net_status st;
    struct arp_ent arp[NET_ARP_MAX];
    int arp_next;
    u16 ip_id;
    /* ping en cours */
    int ping_wait; u16 ping_ident, ping_seq; int ping_got; u32 ping_src;
    /* DHCP en cours */
    int dhcp_busy; u32 dhcp_xid; int dhcp_state;       /* 0 rien, 1 attend OFFER, 2 attend ACK */
    u32 dhcp_offer_ip, dhcp_server, dhcp_mask, dhcp_gw, dhcp_dns, dhcp_lease;
    int dhcp_got_ack, dhcp_got_nak;
    /* DNS en cours */
    int dns_wait; u16 dns_id; int dns_done; err_t dns_err; u32 dns_ip, dns_ttl;
    struct { char name[NET_DNS_NAME_MAX]; u32 ip, expire; int valid; } dcache[NET_DNS_CACHE_MAX];
    int dcache_next;
} N;

static u8 txb[NET_FRAME_MAX];
static u8 rxb[NET_FRAME_MAX];

/* ---- utilitaires ---- */
static u16 get16(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static u32 get32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static void put16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

static u16 csum(const u8 *p, u32 n, u32 init)
{
    u32 s = init;

    for (; n > 1; n -= 2, p += 2)
        s += get16(p);
    if (n)
        s += (u32)p[0] << 8;
    while (s >> 16)
        s = (s & 0xFFFF) + (s >> 16);
    return (u16)~s;
}

void net_reset(void)
{
    memset(&N, 0, sizeof N);
    N.dev = 0;
}

struct netdev *net_dev(void) { return N.dev; }

err_t net_init(void)
{
    if (netdev_count() < 1)
        return E_NOENT;
    N.dev = netdev_get(0);
    N.st.present = 1;
    return E_OK;
}

void net_get_status(struct net_status *s) { *s = N.st; }

err_t net_parse_ip(const char *s, u32 *ip)
{
    u32 v = 0;

    for (int i = 0; i < 4; i++) {
        u32 b = 0;
        int nd = 0;

        while (*s >= '0' && *s <= '9') {
            b = b * 10 + (u32)(*s++ - '0');
            if (++nd > 3 || b > 255)
                return E_INVAL;
        }
        if (!nd || (i < 3 && *s++ != '.'))
            return E_INVAL;
        v = (v << 8) | b;
    }
    if (*s)
        return E_INVAL;
    *ip = v;
    return E_OK;
}

void net_ip_str(u32 ip, char *out)
{
    int n = 0;

    for (int i = 3; i >= 0; i--) {
        u32 b = (ip >> (8 * i)) & 0xFF;

        if (b >= 100) out[n++] = (char)('0' + b / 100);
        if (b >= 10) out[n++] = (char)('0' + (b / 10) % 10);
        out[n++] = (char)('0' + b % 10);
        if (i) out[n++] = '.';
    }
    out[n] = 0;
}

/* ---- Ethernet / ARP ---- */
static err_t eth_send(const u8 *dst, u16 type, const u8 *payload, u32 len)
{
    if (!N.dev || len + ETH_HDR > NET_FRAME_MAX)
        return E_INVAL;
    memcpy(txb, dst, 6);
    memcpy(txb + 6, N.dev->mac, 6);
    put16(txb + 12, type);
    memmove(txb + ETH_HDR, payload, len);
    return N.dev->send(N.dev, txb, len + ETH_HDR);
}

static void arp_learn(u32 ip, const u8 *mac)
{
    for (int i = 0; i < NET_ARP_MAX; i++)
        if (N.arp[i].valid && N.arp[i].ip == ip) {
            memcpy(N.arp[i].mac, mac, 6);
            return;
        }
    struct arp_ent *e = &N.arp[N.arp_next];

    N.arp_next = (N.arp_next + 1) % NET_ARP_MAX;
    e->ip = ip;
    memcpy(e->mac, mac, 6);
    e->valid = 1;
}

static int arp_find(u32 ip, u8 *mac)
{
    for (int i = 0; i < NET_ARP_MAX; i++)
        if (N.arp[i].valid && N.arp[i].ip == ip) {
            memcpy(mac, N.arp[i].mac, 6);
            return 1;
        }
    return 0;
}

static void arp_send(u16 op, const u8 *dst_mac, u32 dst_ip, const u8 *target_mac)
{
    u8 p[28];

    put16(p, 1); put16(p + 2, ETYPE_IP); p[4] = 6; p[5] = 4; put16(p + 6, op);
    memcpy(p + 8, N.dev->mac, 6);
    put32(p + 14, N.st.ip);
    memcpy(p + 18, target_mac, 6);
    put32(p + 24, dst_ip);
    eth_send(dst_mac, ETYPE_ARP, p, 28);
}

static void rx_arp(const u8 *p, u32 len)
{
    if (len < 28 || get16(p) != 1 || get16(p + 2) != ETYPE_IP || p[4] != 6 || p[5] != 4)
        return;
    u16 op = get16(p + 6);
    u32 sip = get32(p + 14), tip = get32(p + 24);

    if (sip)
        arp_learn(sip, p + 8);
    if (op == 1 && N.st.configured && tip == N.st.ip) {
        static const u8 z[6];
        (void)z;
        arp_send(2, p + 8, sip, p + 8);
    } else if (op == 2) {
        N.st.arp_replies++;
    }
}

/* Adresse materielle du prochain saut (passerelle si hors du reseau local), avec demande ARP et attente. */
static err_t next_hop_mac(u32 dst, u8 *mac, u32 timeout_ms)
{
    u32 hop = dst;
    u32 t0;

    if (dst == 0xFFFFFFFFu || (N.st.configured && dst == (N.st.ip | ~N.st.mask)))
    {
        memcpy(mac, BCAST, 6);
        return E_OK;
    }
    if (!N.st.configured)
        return E_IO;
    if ((dst & N.st.mask) != (N.st.ip & N.st.mask)) {
        if (!N.st.gw)
            return E_IO;
        hop = N.st.gw;
    }
    if (arp_find(hop, mac))
        return E_OK;
    t0 = net_now_ms();
    for (;;) {
        static const u8 z[6];

        N.st.arp_requests++;
        arp_send(1, BCAST, hop, z);
        u32 t1 = net_now_ms();

        while (net_now_ms() - t1 < 250) {
            net_poll();
            if (arp_find(hop, mac))
                return E_OK;
            net_delay_ms(1);
        }
        if (net_now_ms() - t0 >= timeout_ms)
            return E_TIMEOUT;
    }
}

/* ---- IPv4 ---- */
static err_t ip_send_to(const u8 *dst_mac, u32 src, u32 dst, u8 proto, const u8 *payload, u32 len)
{
    u8 pkt[NET_MTU];

    if (len + IP_HDR > NET_MTU)
        return E_INVAL;
    pkt[0] = 0x45; pkt[1] = 0;
    put16(pkt + 2, (u16)(len + IP_HDR));
    put16(pkt + 4, ++N.ip_id);
    put16(pkt + 6, 0x4000);             /* ne pas fragmenter */
    pkt[8] = 64; pkt[9] = proto;
    put16(pkt + 10, 0);
    put32(pkt + 12, src);
    put32(pkt + 16, dst);
    put16(pkt + 10, csum(pkt, IP_HDR, 0));
    memcpy(pkt + IP_HDR, payload, len);
    return eth_send(dst_mac, ETYPE_IP, pkt, len + IP_HDR);
}

static err_t ip_send(u32 dst, u8 proto, const u8 *payload, u32 len, u32 timeout_ms)
{
    u8 mac[6];
    err_t e = next_hop_mac(dst, mac, timeout_ms);

    if (e != E_OK)
        return e;
    return ip_send_to(mac, N.st.ip, dst, proto, payload, len);
}

/* ---- UDP ---- */
static err_t udp_send_raw(const u8 *dst_mac, u32 src, u32 dst, u16 sport, u16 dport, const u8 *data, u32 len)
{
    u8 d[NET_MTU];
    u8 ph[12];
    u32 sum;

    if (len + 8 + IP_HDR > NET_MTU)
        return E_INVAL;
    put16(d, sport); put16(d + 2, dport); put16(d + 4, (u16)(len + 8)); put16(d + 6, 0);
    memcpy(d + 8, data, len);
    put32(ph, src); put32(ph + 4, dst); ph[8] = 0; ph[9] = PROTO_UDP; put16(ph + 10, (u16)(len + 8));
    {   /* somme de controle : pseudo-en-tete puis datagramme */
        u32 s = 0;

        for (int i = 0; i < 12; i += 2)
            s += get16(ph + i);
        sum = csum(d, len + 8, s);
    }
    if (sum == 0)
        sum = 0xFFFF;
    put16(d + 6, (u16)sum);
    N.st.udp_out++;
    return ip_send_to(dst_mac, src, dst, PROTO_UDP, d, len + 8);
}

/* ---- ICMP ---- */
static void rx_icmp(const u8 *eth_src, u32 src, const u8 *p, u32 len)
{
    if (len < 8 || csum(p, len, 0) != 0)
        return;
    N.st.icmp_in++;
    if (p[0] == 8 && N.st.configured && len <= NET_MTU - IP_HDR) {          /* echo request -> reply */
        u8 r[NET_MTU];

        memcpy(r, p, len);
        r[0] = 0; put16(r + 2, 0);
        put16(r + 2, csum(r, len, 0));
        N.st.icmp_out++;
        ip_send_to(eth_src, N.st.ip, src, PROTO_ICMP, r, len);
    } else if (p[0] == 0 && N.ping_wait && get16(p + 4) == N.ping_ident && get16(p + 6) == N.ping_seq && src == N.ping_src) {
        N.ping_got = 1;
    }
}

err_t net_ping(u32 ip, u32 timeout_ms, u32 *rtt_ms)
{
    u8 m[8 + 32];
    u32 t0, t1;
    err_t e;

    if (!N.dev)
        return E_NOENT;
    if (!N.st.configured)
        return E_IO;
    if (N.ping_wait)
        return E_BUSY;
    memset(m, 0, sizeof m);
    m[0] = 8;
    N.ping_ident = 0x5048; N.ping_seq++;
    put16(m + 4, N.ping_ident); put16(m + 6, N.ping_seq);
    for (int i = 0; i < 32; i++)
        m[8 + i] = (u8)('a' + i % 26);
    put16(m + 2, csum(m, sizeof m, 0));
    N.ping_src = ip; N.ping_got = 0; N.ping_wait = 1;
    t0 = net_now_ms();
    e = ip_send(ip, PROTO_ICMP, m, sizeof m, timeout_ms);
    if (e == E_OK) {
        N.st.icmp_out++;
        e = E_TIMEOUT;
        while (net_now_ms() - t0 < timeout_ms) {
            net_poll();
            if (N.ping_got) {
                e = E_OK;
                break;
            }
            net_delay_ms(1);
        }
    }
    t1 = net_now_ms();
    N.ping_wait = 0;
    if (e == E_OK && rtt_ms)
        *rtt_ms = t1 - t0;
    return e;
}

/* ---- DHCP ---- */
static void dhcp_parse(const u8 *p, u32 len)
{
    u8 type = 0;
    u32 mask = 0, gw = 0, dns = 0, server = 0, lease = 0;
    u32 i;

    if (len < 240 || p[0] != 2 || get32(p + 4) != N.dhcp_xid || get32(p + 236) != 0x63825363u)
        return;
    if (memcmp(p + 28, N.dev->mac, 6) != 0)
        return;
    for (i = 240; i < len;) {
        u8 o = p[i++], l;

        if (o == 0)
            continue;
        if (o == 255)
            break;
        if (i >= len)
            break;
        l = p[i++];
        if (i + l > len)
            break;
        switch (o) {
        case 53: if (l == 1) type = p[i]; break;
        case 1:  if (l == 4) mask = get32(p + i); break;
        case 3:  if (l >= 4) gw = get32(p + i); break;
        case 6:  if (l >= 4) dns = get32(p + i); break;
        case 54: if (l == 4) server = get32(p + i); break;
        case 51: if (l == 4) lease = get32(p + i); break;
        default: break;
        }
        i += l;
    }
    if (type == 2 && N.dhcp_state == 1 && server) {
        N.dhcp_offer_ip = get32(p + 16);
        N.dhcp_server = server; N.dhcp_mask = mask ? mask : 0xFFFFFF00u;
        N.dhcp_gw = gw; N.dhcp_dns = dns; N.dhcp_lease = lease;
        N.dhcp_state = 2;
        N.dhcp_got_ack = 0;
    } else if (type == 5 && N.dhcp_state == 2) {
        N.dhcp_offer_ip = get32(p + 16);
        if (mask) N.dhcp_mask = mask;
        if (gw) N.dhcp_gw = gw;
        if (dns) N.dhcp_dns = dns;
        if (lease) N.dhcp_lease = lease;
        N.dhcp_got_ack = 1;
    } else if (type == 6 && N.dhcp_state == 2) {
        N.dhcp_got_nak = 1;
    }
}

static err_t dhcp_send(u8 type)
{
    u8 b[300];
    u32 n = 240;

    memset(b, 0, sizeof b);
    b[0] = 1; b[1] = 1; b[2] = 6;
    put32(b + 4, N.dhcp_xid);
    put16(b + 10, 0x8000);              /* "repondez en diffusion" : nous n'avons pas encore d'adresse */
    memcpy(b + 28, N.dev->mac, 6);
    put32(b + 236, 0x63825363u);
    b[n++] = 53; b[n++] = 1; b[n++] = type;
    if (type == 3) {
        b[n++] = 50; b[n++] = 4; put32(b + n, N.dhcp_offer_ip); n += 4;
        b[n++] = 54; b[n++] = 4; put32(b + n, N.dhcp_server); n += 4;
    }
    b[n++] = 55; b[n++] = 3; b[n++] = 1; b[n++] = 3; b[n++] = 6;
    b[n++] = 255;
    return udp_send_raw(BCAST, 0, 0xFFFFFFFFu, PORT_DHCP_C, PORT_DHCP_S, b, n < 300 ? 300 : n);
}

err_t net_dhcp(u32 timeout_ms)
{
    u32 t0 = net_now_ms(), tsend = 0;
    int sent_state = 0;
    err_t result = E_TIMEOUT;

    if (!N.dev)
        return E_NOENT;
    if (N.dhcp_busy)
        return E_BUSY;
    if (N.dev->link_up && !N.dev->link_up(N.dev))
        return E_IO;
    N.dhcp_busy = 1;
    N.st.configured = 0;
    N.st.ip = 0;
    N.dhcp_xid = 0x50484F45u ^ net_now_ms();        /* "PHOE" mele a l'heure */
    N.dhcp_state = 1; N.dhcp_got_ack = 0; N.dhcp_got_nak = 0;
    while (net_now_ms() - t0 < timeout_ms) {
        u32 now = net_now_ms();

        if (sent_state != N.dhcp_state || now - tsend >= 1000) {   /* envoi initial, passage d'etape, ou repetition chaque seconde */
            sent_state = N.dhcp_state;
            tsend = now;
            dhcp_send(N.dhcp_state == 1 ? 1 : 3);
        }
        net_poll();
        if (N.dhcp_got_ack) {
            N.st.ip = N.dhcp_offer_ip; N.st.mask = N.dhcp_mask; N.st.gw = N.dhcp_gw;
            N.st.dns = N.dhcp_dns; N.st.lease_s = N.dhcp_lease;
            N.st.configured = 1;
            net_dns_flush();                            /* le serveur DNS a peut-etre change */
            result = E_OK;
            break;
        }
        if (N.dhcp_got_nak) {
            N.dhcp_state = 1; N.dhcp_got_nak = 0; sent_state = 0;
        }
        net_delay_ms(1);
    }
    N.dhcp_state = 0;
    N.dhcp_busy = 0;
    return result;
}

/* ---- DNS ---- */
/* Saute un nom (etiquettes, ou pointeur de compression sur 2 octets) ; renvoie le nouvel indice ou -1 si hors limites. */
static int dns_skip_name(const u8 *p, u32 len, u32 i)
{
    int guard = 0;

    while (i < len) {
        u8 l = p[i];

        if (l == 0)
            return (int)i + 1;
        if ((l & 0xC0) == 0xC0)
            return i + 2 <= len ? (int)i + 2 : -1;
        if (l & 0xC0)
            return -1;
        i += 1u + l;
        if (++guard > 128)
            return -1;
    }
    return -1;
}

static void dns_parse(const u8 *p, u32 len)
{
    u16 qd, an;
    int i;

    if (!N.dns_wait || len < 12 || get16(p) != N.dns_id || !(p[2] & 0x80))
        return;
    N.dns_done = 1;
    if ((p[3] & 0x0F) != 0) {
        N.dns_err = E_NOENT;
        return;
    }
    qd = get16(p + 4); an = get16(p + 6);
    i = 12;
    for (u16 q = 0; q < qd; q++) {
        i = dns_skip_name(p, len, (u32)i);
        if (i < 0 || (u32)i + 4 > len) { N.dns_err = E_IO; return; }
        i += 4;
    }
    for (u16 a = 0; a < an; a++) {
        u16 type, cls, rdl;

        i = dns_skip_name(p, len, (u32)i);
        if (i < 0 || (u32)i + 10 > len) break;
        type = get16(p + i); cls = get16(p + i + 2); rdl = get16(p + i + 8);
        i += 10;
        if ((u32)i + rdl > len) break;
        if (type == 1 && cls == 1 && rdl == 4) {
            N.dns_ip = get32(p + i);
            N.dns_ttl = get32(p + i - 6);
            N.dns_err = E_OK;
            return;
        }
        i += rdl;                       /* CNAME et autres : suivants */
    }
    N.dns_err = E_NOENT;
}

/* ---- cache DNS ---- */
static char lc(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

static void trim_dot_len(const char *n, u32 *len);

static int name_eq(const char *a, const char *b)
{
    u32 la, lb;

    trim_dot_len(a, &la);
    trim_dot_len(b, &lb);
    if (la != lb)
        return 0;
    for (u32 i = 0; i < la; i++)
        if (lc(a[i]) != lc(b[i]))
            return 0;
    return 1;
}

static void trim_dot_len(const char *n, u32 *len)
{
    *len = (u32)strlen(n);
    if (*len && n[*len - 1] == '.')
        (*len)--;
}

static int cache_find(const char *name)
{
    u32 now = net_now_ms();

    for (int i = 0; i < NET_DNS_CACHE_MAX; i++)
        if (N.dcache[i].valid) {
            if ((i32)(now - N.dcache[i].expire) >= 0) {
                N.dcache[i].valid = 0;
                continue;
            }
            if (name_eq(N.dcache[i].name, name))
                return i;
        }
    return -1;
}

static void cache_put(const char *name, u32 ip, u32 ttl)
{
    u32 len;
    int slot = -1;

    trim_dot_len(name, &len);
    if (ttl == 0 || len == 0 || len >= NET_DNS_NAME_MAX)
        return;
    if (ttl > 3600)
        ttl = 3600;
    for (int i = 0; i < NET_DNS_CACHE_MAX && slot < 0; i++)
        if (N.dcache[i].valid && name_eq(N.dcache[i].name, name))
            slot = i;
    for (int i = 0; i < NET_DNS_CACHE_MAX && slot < 0; i++)
        if (!N.dcache[i].valid)
            slot = i;
    if (slot < 0) {
        slot = N.dcache_next;
        N.dcache_next = (N.dcache_next + 1) % NET_DNS_CACHE_MAX;
    }
    memcpy(N.dcache[slot].name, name, len);
    N.dcache[slot].name[len] = 0;
    N.dcache[slot].ip = ip;
    N.dcache[slot].expire = net_now_ms() + ttl * 1000u;
    N.dcache[slot].valid = 1;
}

void net_dns_flush(void)
{
    for (int i = 0; i < NET_DNS_CACHE_MAX; i++)
        N.dcache[i].valid = 0;
}

int net_dns_cache_count(void)
{
    int n = 0;
    u32 now = net_now_ms();

    for (int i = 0; i < NET_DNS_CACHE_MAX; i++)
        if (N.dcache[i].valid && (i32)(now - N.dcache[i].expire) < 0)
            n++;
    return n;
}

int net_dns_cache_get(int idx, char *name, u32 *ip, u32 *secs_left)
{
    u32 now = net_now_ms();
    int k = 0;

    for (int i = 0; i < NET_DNS_CACHE_MAX; i++)
        if (N.dcache[i].valid && (i32)(now - N.dcache[i].expire) < 0) {
            if (k++ == idx) {
                strcpy(name, N.dcache[i].name);
                *ip = N.dcache[i].ip;
                *secs_left = (N.dcache[i].expire - now) / 1000u;
                return 1;
            }
        }
    return 0;
}

err_t net_resolve(const char *name, u32 *ip)
{
    u8 q[300];
    u32 n = 12, len = 0;
    u32 t0;
    u8 mac[6];
    err_t e;

    if (!name || !*name || !ip)
        return E_INVAL;
    if (net_parse_ip(name, ip) == E_OK)
        return E_OK;
    if (!N.dev)
        return E_NOENT;
    if (!N.st.configured || !N.st.dns)
        return E_IO;
    if (N.dns_wait)
        return E_BUSY;
    {
        int c = cache_find(name);

        if (c >= 0) {
            N.st.dns_hits++;
            *ip = N.dcache[c].ip;
            return E_OK;
        }
    }
    memset(q, 0, sizeof q);
    N.dns_id = (u16)(N.dns_id + 0x1D3B + net_now_ms());
    put16(q, N.dns_id);
    put16(q + 2, 0x0100);               /* requete standard, recursion demandee */
    put16(q + 4, 1);
    for (const char *s = name; ;) {     /* nom -> etiquettes */
        u32 l = 0, start = n++;

        while (s[l] && s[l] != '.')
            l++;
        if (l == 0 || l > 63 || n + l + 6 > sizeof q)
            return E_INVAL;
        memcpy(q + n, s, l);
        n += l;
        q[start] = (u8)l;
        len += l + 1;
        s += l;
        if (!*s)
            break;
        s++;
        if (!*s)
            break;                      /* point final accepte */
    }
    q[n++] = 0;
    put16(q + n, 1); put16(q + n + 2, 1);   /* type A, classe IN */
    n += 4;
    e = next_hop_mac(N.st.dns, mac, 3000);
    if (e != E_OK)
        return e;
    N.dns_wait = 1; N.dns_done = 0; N.dns_err = E_TIMEOUT; N.dns_ttl = 0;
    t0 = net_now_ms();
    for (int tries = 0; tries < 3 && !N.dns_done; tries++) {
        u32 t1 = net_now_ms();

        N.st.dns_queries++;
        udp_send_raw(mac, N.st.ip, N.st.dns, DNS_SRC_PORT, PORT_DNS, q, n);
        while (!N.dns_done && net_now_ms() - t1 < 1500) {
            net_poll();
            if (N.dns_done)
                break;
            net_delay_ms(1);
        }
    }
    (void)t0;
    N.dns_wait = 0;
    if (!N.dns_done)
        return E_TIMEOUT;
    if (N.dns_err != E_OK)
        return N.dns_err;
    *ip = N.dns_ip;
    cache_put(name, N.dns_ip, N.dns_ttl);
    return E_OK;
}

/* ---- reception ---- */
static void rx_udp(u32 src, u32 dst, const u8 *p, u32 len)
{
    u16 sport, dport, ulen;

    (void)src; (void)dst;
    if (len < 8)
        return;
    sport = get16(p); dport = get16(p + 2); ulen = get16(p + 4);
    if (ulen < 8 || ulen > len)
        return;
    N.st.udp_in++;
    p += 8; ulen -= 8;
    if (dport == PORT_DHCP_C && sport == PORT_DHCP_S && N.dhcp_state)
        dhcp_parse(p, ulen);
    else if (dport == DNS_SRC_PORT && sport == PORT_DNS)
        dns_parse(p, ulen);
}

static void rx_ip(const u8 *eth_src, const u8 *p, u32 len)
{
    u32 ihl, total, src, dst;

    if (len < IP_HDR || (p[0] >> 4) != 4) { N.st.ip_bad++; return; }
    ihl = (u32)(p[0] & 15) * 4;
    total = get16(p + 2);
    if (ihl < IP_HDR || total < ihl || total > len || csum(p, ihl, 0) != 0) { N.st.ip_bad++; return; }
    if (get16(p + 6) & 0x3FFF)          /* fragment : non pris en charge */
        { N.st.ip_bad++; return; }
    src = get32(p + 12); dst = get32(p + 16);
    if (dst != 0xFFFFFFFFu && dst != N.st.ip && !(N.st.configured && dst == (N.st.ip | ~N.st.mask)) && N.st.ip != 0)
        return;
    switch (p[9]) {
    case PROTO_ICMP: rx_icmp(eth_src, src, p + ihl, total - ihl); break;
    case PROTO_UDP:  rx_udp(src, dst, p + ihl, total - ihl); break;
    case PROTO_TCP:  tcp_rx(src, dst, p + ihl, total - ihl); break;
    default: break;
    }
}

void net_set_config(u32 ip, u32 mask, u32 gw, u32 dns)
{
    N.st.ip = ip;
    N.st.mask = mask;
    N.st.gw = gw;
    N.st.dns = dns;
    N.st.configured = ip != 0;
}

void net_poll(void)
{
    int n;

    if (!N.dev)
        return;
    for (int k = 0; k < 16; k++) {
        n = N.dev->recv(N.dev, rxb, sizeof rxb);
        if (n <= 0)
            break;
        if (n < ETH_HDR)
            continue;
        if (memcmp(rxb, N.dev->mac, 6) != 0 && memcmp(rxb, BCAST, 6) != 0)
            continue;
        {
            u16 t = get16(rxb + 12);
            u8 src[6];
            static u8 copy[NET_FRAME_MAX];

            memcpy(src, rxb + 6, 6);
            memcpy(copy, rxb + ETH_HDR, (u32)n - ETH_HDR);      /* les gestionnaires peuvent emettre : on ne s'appuie pas sur rxb */
            if (t == ETYPE_ARP)
                rx_arp(copy, (u32)n - ETH_HDR);
            else if (t == ETYPE_IP)
                rx_ip(src, copy, (u32)n - ETH_HDR);
        }
    }
}

/* ---- couche TCP (0.10.0) ---- */
u32 net_local_ip(void) { return N.st.ip; }

err_t net_ip_send(u32 dst, u8 proto, const u8 *payload, u32 len, u32 timeout_ms)
{
    return ip_send(dst, proto, payload, len, timeout_ms);
}

u16 net_pseudo_checksum(u32 src, u32 dst, u8 proto, const u8 *seg, u32 len)
{
    u8 ph[12];
    u32 s = 0;

    put32(ph, src); put32(ph + 4, dst); ph[8] = 0; ph[9] = proto; put16(ph + 10, (u16)len);
    for (int i = 0; i < 12; i += 2)
        s += get16(ph + i);
    return csum(seg, len, s);
}

/* ---- tache noyau ---- */
static void net_task(void *arg)
{
    (void)arg;
    if (N.dev && N.dev->link_up(N.dev)) {
        err_t e = net_dhcp(8000);

        if (e == E_OK) {
            char a[16], g[16];

            net_ip_str(N.st.ip, a);
            net_ip_str(N.st.gw, g);
            DINFO("net", "adresse obtenue par DHCP : %s (passerelle %s)", a, g);
        } else {
            DWARN("net", "DHCP : %s", err_str(e));
        }
    }
    for (;;) {
        net_poll();
        sched_sleep_ms(10);
    }
}

void net_start(void)
{
    struct task *t;

    if (!N.dev)
        return;
    if (sched_create("net", net_task, 0, 0, 0, &t) == E_OK)
        sched_detach(t);
}
