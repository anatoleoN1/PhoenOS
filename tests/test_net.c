/*
 * Test du VRAI pilote e1000, du PCI, du cadre des pilotes et de la pile reseau (ARP, IPv4, ICMP, UDP, DHCP, DNS),
 * contre une FAUSSE carte (registres + anneaux DMA) branchee sur un FAUX reseau : une passerelle 10.0.2.2 qui repond
 * a ARP, ping et DHCP, et un serveur DNS 10.0.2.3. Un echange hostile (trames tronquees, mauvaises sommes de controle,
 * reponses DNS piegees) verifie que la pile ne se laisse pas tromper.
 */
#define TEST_HAS_PORTS 1
#include "testlib.h"
#include "pci.h"
#include "driver.h"
#include "netdev.h"
#include "net.h"
#include "sched.h"
#include "pmm.h"
#include "paging.h"

u8 test_scancode;
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }

/* ---------- faux PCI ---------- */
static int pci_present;
static u32 cfg_sel;
static u32 e1000_cmd_reg = 0;                       /* registre commande de la carte (octets 4..5) */
#define BAR0 0xFEBC0000u

static u32 cfg_read(u32 sel)
{
    u32 bus = (sel >> 16) & 0xFF, dev = (sel >> 11) & 31, fn = (sel >> 8) & 7, off = sel & 0xFC;

    if (!pci_present || bus != 0 || fn != 0)
        return 0xFFFFFFFFu;
    if (dev == 0) {                                 /* pont hote */
        switch (off) {
        case 0x00: return 0x12378086u;
        case 0x08: return 0x06000002u;
        case 0x0C: return 0;
        default: return 0;
        }
    }
    if (dev == 3) {                                 /* carte e1000 */
        switch (off) {
        case 0x00: return 0x100E8086u;
        case 0x04: return e1000_cmd_reg;
        case 0x08: return 0x02000003u;
        case 0x0C: return 0;
        case 0x10: return BAR0;
        case 0x3C: return 0x0000010Bu;
        default: return 0;
        }
    }
    return 0xFFFFFFFFu;
}

u32 test_port_in(u16 port, int size)
{
    (void)size;
    if (port == 0x60) return test_scancode;
    if (port == 0xCFC) return cfg_read(cfg_sel);
    out("  reseau verifie : PCI, pilote e1000 (anneaux, erreurs, rotation), ARP, ping, DHCP, DNS, trames hostiles\n");
    return 0;
}

void test_port_out(u16 port, u32 v, int size)
{
    (void)size;
    if (port == 0xCF8) cfg_sel = v;
    else if (port == 0xCFC && (cfg_sel & 0xFFFFFFFCu) == (0x80000000u | (3u << 11) | 0x04))
        e1000_cmd_reg = v;
}

/* ---------- pmm, pagination, ordonnanceur : doublures ---------- */
static u8 pool[64][4096] __attribute__((aligned(4096)));
static int pool_next;
u32 pmm_alloc_frame(void) { return pool_next < 64 ? (u32)pool[pool_next++] : 0; }
void pmm_free_frame(u32 a) { (void)a; }
static u32 mmio_phys, mmio_size;
err_t paging_map_mmio(u32 phys, u32 size) { mmio_phys = phys; mmio_size = size; return E_OK; }
static int task_made; static void (*task_entry)(void *);
err_t sched_create(const char *name, void (*entry)(void *), void *arg, void (*cleanup)(void *), void *owner, struct task **out)
{
    (void)arg; (void)cleanup; (void)owner; (void)out;
    if (strcmp(name, "net") == 0) { task_made++; task_entry = entry; }
    return E_OK;
}
void sched_detach(struct task *t) { (void)t; }
void sched_sleep_ms(u32 ms) { (void)ms; }

void vga_putc(char c) { (void)c; }
void serial_putc(char c) { (void)c; }
u32 pit_uptime_ms(void) { return 0; }

/* ---------- horloge simulee ---------- */
static u32 clock_ms;
u32 net_now_ms(void) { return clock_ms; }
void net_delay_ms(u32 ms) { clock_ms += ms; }
void e1000_hw_delay_ms(u32 ms) { clock_ms += ms; }

/* ---------- fausse carte e1000 ---------- */
static u32 regs[0x20000 / 4];
static const u8 OUR_MAC[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static const u8 GW_MAC[6] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x02 };
static const u8 DNS_MAC[6] = { 0x52, 0x55, 0x0a, 0x00, 0x02, 0x03 };
#define IP_GW  0x0A000202u
#define IP_DNS 0x0A000203u
#define IP_US  0x0A00020Fu

struct rxd { u32 lo, hi; u16 length, csum; u8 status, errors; u16 special; } __attribute__((packed));
struct txd { u32 lo, hi; u16 length; u8 cso, cmd, status, css; u16 special; } __attribute__((packed));

#define R_CTRL 0x0000
#define R_STATUS 0x0008
#define R_RCTL 0x0100
#define R_TCTL 0x0400
#define R_RDBAL 0x2800
#define R_RDLEN 0x2808
#define R_RDH 0x2810
#define R_RDT 0x2818
#define R_TDBAL 0x3800
#define R_TDLEN 0x3808
#define R_TDH 0x3810
#define R_TDT 0x3818
#define R_RAL0 0x5400
#define R_RAH0 0x5404

struct inj { u8 data[1600]; u32 len; u8 errors; };
static struct inj q[64];
static int qh, qt;
static int hw_rx_head;                              /* prochaine case que la carte remplira */
static u32 resets, tx_sent, tx_short_seen;
static int peer_enabled = 1;

static void peer_rx(const u8 *f, u32 len);
static void hw_deliver(void);

static void inject(const u8 *data, u32 len, u8 errors)
{
    q[qt].len = len; q[qt].errors = errors; memcpy(q[qt].data, data, len);
    qt = (qt + 1) % 64;
    hw_deliver();                                   /* la carte ecrit toute seule dans la RAM (DMA) */
}

static void hw_deliver(void)
{
    u32 rdlen = regs[R_RDLEN / 4];

    if (!(regs[R_RCTL / 4] & 2) || !rdlen)
        return;
    while (qh != qt) {
        u32 n = rdlen / 16;
        struct rxd *ring = (struct rxd *)(usize)regs[R_RDBAL / 4];

        if ((u32)hw_rx_head == regs[R_RDT / 4])     /* plus de case rendue a la carte */
            return;
        struct rxd *d = &ring[hw_rx_head];
        memcpy((void *)(usize)d->lo, q[qh].data, q[qh].len);
        d->length = (u16)q[qh].len; d->errors = q[qh].errors; d->status = 3;
        hw_rx_head = (int)(((u32)hw_rx_head + 1) % n);
        regs[R_RDH / 4] = (u32)hw_rx_head;
        qh = (qh + 1) % 64;
    }
}

static void hw_tx(u32 new_tail)
{
    u32 n = regs[R_TDLEN / 4] / 16;
    struct txd *ring = (struct txd *)(usize)regs[R_TDBAL / 4];

    while (regs[R_TDH / 4] != new_tail) {
        struct txd *d = &ring[regs[R_TDH / 4]];

        if ((d->cmd & 0x0B) == 0x0B) {
            tx_sent++;
            if (d->length == 60) tx_short_seen++;
            if (peer_enabled) peer_rx((const u8 *)(usize)d->lo, d->length);
            d->status |= 1;
        }
        regs[R_TDH / 4] = (regs[R_TDH / 4] + 1) % n;
    }
}

u32 e1000_hw_read32(u32 addr)
{
    u32 off = addr - BAR0;

    hw_deliver();
    if (off == R_STATUS) return 2;
    if (off == R_CTRL) return regs[off / 4] & ~(1u << 26);
    return regs[off / 4];
}

void e1000_hw_write32(u32 addr, u32 v)
{
    u32 off = addr - BAR0;

    regs[off / 4] = v;
    if (off == R_CTRL && (v & (1u << 26))) {
        resets++;
        memset(regs, 0, sizeof regs);
        regs[R_RAL0 / 4] = OUR_MAC[0] | (OUR_MAC[1] << 8) | (OUR_MAC[2] << 16) | ((u32)OUR_MAC[3] << 24);
        regs[R_RAH0 / 4] = OUR_MAC[4] | (OUR_MAC[5] << 8) | (1u << 31);
        hw_rx_head = 0; qh = qt = 0;
    }
    if (off == R_TDT) hw_tx(v);
    if (off == R_RDT) hw_deliver();
}

/* ---------- construction de trames pour le faux reseau ---------- */
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

/* IP + payload dans une trame Ethernet vers `dst_mac`, depuis `src_mac`. */
static u32 build_ip(u8 *f, const u8 *dst_mac, const u8 *src_mac, u32 src, u32 dst, u8 proto, const u8 *pl, u32 pl_len)
{
    memcpy(f, dst_mac, 6); memcpy(f + 6, src_mac, 6); put16(f + 12, 0x0800);
    u8 *ip = f + 14;
    ip[0] = 0x45; ip[1] = 0; put16(ip + 2, (u16)(20 + pl_len)); put16(ip + 4, 1); put16(ip + 6, 0); ip[8] = 64; ip[9] = proto;
    put16(ip + 10, 0); put32(ip + 12, src); put32(ip + 16, dst);
    put16(ip + 10, cs(ip, 20, 0));
    memcpy(ip + 20, pl, pl_len);
    return 14 + 20 + pl_len;
}

static u32 build_udp(u8 *f, const u8 *dst_mac, const u8 *src_mac, u32 src, u32 dst, u16 sp, u16 dp, const u8 *data, u32 len)
{
    u8 d[600];
    put16(d, sp); put16(d + 2, dp); put16(d + 4, (u16)(8 + len)); put16(d + 6, 0);
    memcpy(d + 8, data, len);
    return build_ip(f, dst_mac, src_mac, src, dst, 17, d, 8 + len);
}

static u32 arp_reply_frame(u8 *f, const u8 *mac, u32 ip)
{
    memcpy(f, OUR_MAC, 6); memcpy(f + 6, mac, 6); put16(f + 12, 0x0806);
    u8 *p = f + 14;
    put16(p, 1); put16(p + 2, 0x0800); p[4] = 6; p[5] = 4; put16(p + 6, 2);
    memcpy(p + 8, mac, 6); put32(p + 14, ip); memcpy(p + 18, OUR_MAC, 6); put32(p + 24, IP_US);
    return 42;
}

/* ---------- le faux reseau ---------- */
static u32 n_arp_req, n_discover, n_request, n_echo_in, n_dns_q, offer_xid_wrong_first = 1;
static u32 last_arp_reply_seen, last_icmp_reply_seen;
static int hostile_dns_mode;                        /* 0 normal ; 1 reponse piegee */
static u32 resolve_ip_answer = 0x5DB8D822u;         /* 93.184.216.34 */

static void dhcp_reply(u8 type, const u8 *req)
{
    u8 b[320], f[700];
    u32 n = 240;
    memset(b, 0, sizeof b);
    b[0] = 2; b[1] = 1; b[2] = 6;
    if (type == 2 && offer_xid_wrong_first) {       /* d'abord une offre avec un mauvais identifiant : doit etre ignoree */
        u8 bad[320];
        memcpy(bad, req, 300);
        offer_xid_wrong_first = 0;
        bad[0] = 2; bad[4] ^= 0xFF;
        u32 bn = build_udp(f, OUR_MAC, GW_MAC, IP_GW, 0xFFFFFFFFu, 67, 68, bad, 300);
        inject(f, bn, 0);
    }
    memcpy(b + 4, req + 4, 4);                      /* xid */
    put32(b + 16, IP_US);                           /* yiaddr */
    memcpy(b + 28, req + 28, 6);                    /* chaddr */
    put32(b + 236, 0x63825363u);
    b[n++] = 53; b[n++] = 1; b[n++] = type;
    b[n++] = 54; b[n++] = 4; put32(b + n, IP_GW); n += 4;
    b[n++] = 51; b[n++] = 4; put32(b + n, 86400); n += 4;
    b[n++] = 1; b[n++] = 4; put32(b + n, 0xFFFFFF00u); n += 4;
    b[n++] = 3; b[n++] = 4; put32(b + n, IP_GW); n += 4;
    b[n++] = 6; b[n++] = 4; put32(b + n, IP_DNS); n += 4;
    b[n++] = 255;
    u32 fn = build_udp(f, OUR_MAC, GW_MAC, IP_GW, 0xFFFFFFFFu, 67, 68, b, n);
    inject(f, fn, 0);
}

static void dns_reply(const u8 *q_, u32 qlen)
{
    u8 r[512], f[700];
    u32 n = qlen, name_len = 0;
    u8 labels[256];
    int i = 12;
    u32 ln = 0;

    while (q_[i]) { u8 l = q_[i]; memcpy(labels + ln, q_ + i, l + 1u); ln += l + 1u; i += l + 1; }
    labels[ln] = 0; name_len = ln + 1;
    memcpy(r, q_, qlen);
    n_dns_q++;
    if (hostile_dns_mode == 1) {                    /* reponse piegee : pointeur de compression vers lui-meme puis longueur absurde */
        put16(r + 2, 0x8180); put16(r + 6, 3);
        u32 k = qlen;
        r[k++] = 0xC0; r[k++] = 0x0C; put16(r + k, 5); put16(r + k + 2, 1); put32(r + k + 4, 60); put16(r + k + 8, 0xFFF0); k += 10;
        u32 fn = build_udp(f, OUR_MAC, DNS_MAC, IP_DNS, IP_US, 53, 40053, r, k);
        inject(f, fn, 0);
        return;
    }
    /* nom "inconnu.test" -> NXDOMAIN */
    if (name_len > 8 && memcmp(labels + 1, "inconnu", 7) == 0) {
        put16(r + 2, 0x8183); put16(r + 6, 0);
        u32 fn = build_udp(f, OUR_MAC, DNS_MAC, IP_DNS, IP_US, 53, 40053, r, n);
        inject(f, fn, 0);
        return;
    }
    put16(r + 2, 0x8180); put16(r + 6, 2);
    /* reponse 1 : CNAME (pointeur de compression sur le nom de la question) ; reponse 2 : A */
    r[n++] = 0xC0; r[n++] = 0x0C; put16(r + n, 5); put16(r + n + 2, 1); put32(r + n + 4, 60); put16(r + n + 8, 2); n += 10;
    r[n++] = 0xC0; r[n++] = 0x0C;
    r[n++] = 0xC0; r[n++] = 0x0C; put16(r + n, 1); put16(r + n + 2, 1); put32(r + n + 4, 60); put16(r + n + 8, 4); n += 10;
    put32(r + n, resolve_ip_answer); n += 4;
    u32 fn = build_udp(f, OUR_MAC, DNS_MAC, IP_DNS, IP_US, 53, 40053, r, n);
    inject(f, fn, 0);
}

static void peer_rx(const u8 *fr, u32 len)
{
    u8 f[700];
    u16 et;

    if (len < 14) return;
    et = get16(fr + 12);
    if (et == 0x0806 && len >= 42) {
        u32 tip = get32(fr + 14 + 24);
        if (get16(fr + 14 + 6) == 1) {
            n_arp_req++;
            if (tip == IP_GW) inject(f, arp_reply_frame(f, GW_MAC, IP_GW), 0);
            else if (tip == IP_DNS) inject(f, arp_reply_frame(f, DNS_MAC, IP_DNS), 0);
        } else if (get16(fr + 14 + 6) == 2) {
            last_arp_reply_seen++;
        }
        return;
    }
    if (et != 0x0800 || len < 34) return;
    const u8 *ip = fr + 14;
    u32 ihl = (u32)(ip[0] & 15) * 4, total = get16(ip + 2), dst = get32(ip + 16);
    if (total + 14 > len) return;
    if (ip[9] == 1) {                               /* ICMP */
        const u8 *ic = ip + ihl;
        u32 il = total - ihl;
        if (ic[0] == 8 && memcmp(fr, GW_MAC, 6) == 0) {      /* echo vers la passerelle (ou au-dela) */
            u8 r[100];
            n_echo_in++;
            memcpy(r, ic, il); r[0] = 0; put16(r + 2, 0); put16(r + 2, cs(r, il, 0));
            u32 fn = build_ip(f, OUR_MAC, GW_MAC, dst, get32(ip + 12), 1, r, il);
            inject(f, fn, 0);
        } else if (ic[0] == 0) {
            last_icmp_reply_seen++;
        }
    } else if (ip[9] == 17) {                       /* UDP */
        const u8 *u = ip + ihl;
        u16 dp = get16(u + 2);
        const u8 *d = u + 8;
        u32 dl = get16(u + 4) - 8;
        if (dp == 67 && dl >= 241) {
            u8 type = 0;
            for (u32 i = 240; i + 2 < dl;) { if (d[i] == 53) { type = d[i + 2]; break; } i += 2u + d[i + 1]; }
            if (type == 1) { n_discover++; dhcp_reply(2, d); }
            else if (type == 3) { n_request++; dhcp_reply(5, d); }
        } else if (dp == 53) {
            dns_reply(d, dl);
        }
    }
}

/* ---------- le test ---------- */
static void expect_ip(u32 ip, u32 want) { CHECK(ip == want); }

int test_main(void)
{
    struct net_status st;
    struct netdev *nd;
    u32 ip = 0, rtt = 0;
    char s[20];
    u8 frame[1700];

    /* --- adresses IPv4 --- */
    CHECK(net_parse_ip("10.0.2.15", &ip) == E_OK && ip == IP_US);
    CHECK(net_parse_ip("0.0.0.0", &ip) == E_OK && ip == 0);
    CHECK(net_parse_ip("255.255.255.255", &ip) == E_OK && ip == 0xFFFFFFFFu);
    CHECK(net_parse_ip("1.2.3", &ip) == E_INVAL);
    CHECK(net_parse_ip("1.2.3.4.5", &ip) == E_INVAL);
    CHECK(net_parse_ip("256.1.1.1", &ip) == E_INVAL);
    CHECK(net_parse_ip("a.b.c.d", &ip) == E_INVAL);
    CHECK(net_parse_ip("", &ip) == E_INVAL);
    CHECK(net_parse_ip("1..2.3", &ip) == E_INVAL);
    CHECK(net_parse_ip("1.2.3.4 ", &ip) == E_INVAL);
    CHECK(net_parse_ip("0001.2.3.4", &ip) == E_INVAL);
    net_ip_str(IP_US, s); CHECK(strcmp(s, "10.0.2.15") == 0);
    net_ip_str(0, s); CHECK(strcmp(s, "0.0.0.0") == 0);
    net_ip_str(0xFFFFFFFFu, s); CHECK(strcmp(s, "255.255.255.255") == 0);

    /* --- PCI : pas de bus, puis bus avec pont hote + carte --- */
    pci_present = 0;
    CHECK(pci_scan() == 0 && pci_count() == 0 && pci_get(0) == NULL);
    CHECK(drivers_attach_all() == 0);
    pci_present = 1;
    CHECK(pci_scan() == 2 && pci_count() == 2);
    const struct pci_dev *pd = pci_get(1);
    CHECK(pd && pd->bus == 0 && pd->dev == 3 && pd->fn == 0 && pd->vendor == 0x8086 && pd->device == 0x100E);
    CHECK(pd && pd->class_code == 2 && pd->subclass == 0 && pd->bar[0] == BAR0 && pd->irq == 0x0B);
    CHECK(strcmp(pci_class_name(2, 0), "Reseau Ethernet") == 0 && strcmp(pci_class_name(6, 0), "Pont hote") == 0);
    {
        u32 a = 0;
        CHECK(pci_bar_mem(pd, 0, &a) == E_OK && a == BAR0);
        CHECK(pci_bar_mem(pd, 1, &a) == E_INVAL);   /* BAR vide */
    }

    /* --- cadre des pilotes + e1000 --- */
    CHECK(drivers_count() == 1 && drivers_get(0) && strcmp(drivers_get(0)->name, "e1000") == 0 && drivers_get(0)->official);
    CHECK(drivers_attach_all() == 1);
    CHECK(drivers_bindings() == 1 && drivers_binding(0)->status == E_OK && drivers_binding(0)->dev == 1);
    CHECK(drivers_for_dev(1) == drivers_get(0) && drivers_for_dev(0) == NULL);
    CHECK(mmio_phys == BAR0 && mmio_size == 0x20000);
    CHECK((e1000_cmd_reg & 7) == 7);                /* ports + memoire + DMA autorises */
    CHECK(resets == 1);
    CHECK(netdev_count() == 1);
    nd = netdev_get(0);
    CHECK(nd && strcmp(nd->name, "eth0") == 0 && strcmp(nd->driver, "e1000") == 0);
    CHECK(nd && memcmp(nd->mac, OUR_MAC, 6) == 0);
    CHECK(nd && nd->link_up(nd) == 1);
    CHECK((regs[R_RCTL / 4] & 2) && (regs[R_RCTL / 4] & (1u << 15)) && (regs[R_TCTL / 4] & 2));
    CHECK(regs[R_RDLEN / 4] == 32 * 16 && regs[R_TDLEN / 4] == 16 * 16 && regs[R_RDT / 4] == 31 && regs[R_RDH / 4] == 0);
    CHECK(regs[R_RDBAL / 4] != 0 && regs[R_TDBAL / 4] != 0 && (regs[R_RDBAL / 4] & 0xFFF) == 0);
    CHECK(netdev_get(1) == NULL && netdev_get(-1) == NULL);

    /* envoi : refus des trames invalides, rembourrage des trames courtes */
    CHECK(nd->send(nd, frame, 10) == E_INVAL);
    CHECK(nd->send(nd, frame, NET_FRAME_MAX + 1) == E_INVAL);
    CHECK(nd->tx_errors == 2);
    peer_enabled = 0;
    memset(frame, 0xAB, sizeof frame);
    CHECK(nd->send(nd, frame, 20) == E_OK && tx_sent == 1 && tx_short_seen == 1);
    peer_enabled = 1;
    CHECK(nd->recv(nd, frame, sizeof frame) == 0);
    /* plus de 16 envois d'affilee : l'anneau tourne */
    peer_enabled = 0;
    for (int i = 0; i < 40; i++)
        CHECK(nd->send(nd, frame, 100) == E_OK);
    peer_enabled = 1;
    CHECK(tx_sent == 41 && nd->tx_frames == 41);

    /* reception : trame normale, trame en erreur, trame trop grande pour le tampon de l'appelant */
    {
        u8 t[100]; int n;
        memset(t, 0x11, sizeof t);
        inject(t, 100, 0);
        inject(t, 80, 1);                           /* erreur materielle : jetee */
        inject(t, 90, 0);
        n = nd->recv(nd, frame, sizeof frame); CHECK(n == 100 && frame[0] == 0x11);
        n = nd->recv(nd, frame, sizeof frame); CHECK(n == 0 && nd->rx_dropped == 1);
        n = nd->recv(nd, frame, 50); CHECK(n == 0 && nd->rx_dropped == 2);       /* 90 octets > capacite 50 */
        n = nd->recv(nd, frame, sizeof frame); CHECK(n == 0);
        /* 100 trames d'affilee : jamais de perte, les 32 cases tournent */
        int got = 0;
        for (int k = 0; k < 100; k++) {
            t[0] = (u8)k; inject(t, 64, 0);
            n = nd->recv(nd, frame, sizeof frame);
            if (n == 64 && frame[0] == (u8)k) got++;
        }
        CHECK(got == 100);
    }

    /* --- pile reseau --- */
    net_reset();
    CHECK(net_dev() == NULL);
    CHECK(net_dhcp(100) == E_NOENT && net_ping(IP_GW, 100, &rtt) == E_NOENT);
    CHECK(net_init() == E_OK && net_dev() == nd);
    net_get_status(&st);
    CHECK(st.present && !st.configured);
    CHECK(net_ping(IP_GW, 100, &rtt) == E_IO);      /* pas encore d'adresse */
    CHECK(net_resolve("phoenos.test", &ip) == E_IO);
    CHECK(net_resolve("10.1.2.3", &ip) == E_OK && ip == 0x0A010203u);   /* un nom numerique n'a pas besoin de DNS */
    CHECK(net_resolve("", &ip) == E_INVAL && net_resolve(0, &ip) == E_INVAL);

    /* DHCP */
    CHECK(net_dhcp(8000) == E_OK);
    net_get_status(&st);
    CHECK(st.configured);
    expect_ip(st.ip, IP_US); expect_ip(st.mask, 0xFFFFFF00u); expect_ip(st.gw, IP_GW); expect_ip(st.dns, IP_DNS);
    CHECK(st.lease_s == 86400);
    CHECK(n_discover >= 1 && n_request >= 1);

    /* ping : la passerelle (ARP puis echo), puis une adresse hors du reseau (via la passerelle) */
    CHECK(net_ping(IP_GW, 2000, &rtt) == E_OK && rtt < 50);
    CHECK(n_echo_in == 1);
    CHECK(net_ping(0x08080808u, 2000, &rtt) == E_OK);
    CHECK(n_echo_in == 2);
    CHECK(net_ping(0x0A000263u, 1000, &rtt) == E_TIMEOUT);       /* meme reseau, personne : l'ARP reste sans reponse */

    /* DNS */
    CHECK(net_resolve("phoenos.test", &ip) == E_OK && ip == 0x5DB8D822u);     /* CNAME + A, compression */
    CHECK(net_resolve("www.phoenos.test.", &ip) == E_OK);                      /* point final accepte */
    CHECK(net_resolve("inconnu.test", &ip) == E_NOENT);
    CHECK(net_resolve("a..b", &ip) == E_INVAL);
    {
        char longlabel[100];
        memset(longlabel, 'x', 70); longlabel[70] = 0;
        CHECK(net_resolve(longlabel, &ip) == E_INVAL);                          /* etiquette > 63 */
    }
    hostile_dns_mode = 1;
    CHECK(net_resolve("piege.test", &ip) == E_NOENT);                           /* longueur absurde : refusee, aucun plantage */
    hostile_dns_mode = 0;
    CHECK(net_resolve("phoenos.test", &ip) == E_OK);

    /* cache DNS (0.9.1) */
    {
        char nm[NET_DNS_NAME_MAX]; u32 left = 0, q0, i2;
        struct net_status s1, s2;

        net_dns_flush();
        CHECK(net_dns_cache_count() == 0);
        q0 = n_dns_q;
        CHECK(net_resolve("cache.test", &ip) == E_OK && n_dns_q == q0 + 1 && net_dns_cache_count() == 1);
        CHECK(net_resolve("cache.test", &ip) == E_OK && n_dns_q == q0 + 1 && ip == 0x5DB8D822u);   /* servi par le cache */
        CHECK(net_resolve("CACHE.Test", &ip) == E_OK && n_dns_q == q0 + 1);                        /* casse ignoree */
        CHECK(net_resolve("cache.test.", &ip) == E_OK && n_dns_q == q0 + 1);                       /* point final */
        net_get_status(&s1);
        CHECK(s1.dns_hits >= 3);
        CHECK(net_dns_cache_get(0, nm, &i2, &left) == 1 && strcmp(nm, "cache.test") == 0 && i2 == 0x5DB8D822u && left <= 60 && left >= 58);
        CHECK(net_dns_cache_get(1, nm, &i2, &left) == 0);
        clock_ms += 61000;                                              /* duree de vie (60 s) ecoulee : on redemande */
        CHECK(net_dns_cache_count() == 0);
        CHECK(net_resolve("cache.test", &ip) == E_OK && n_dns_q == q0 + 2);
        CHECK(net_resolve("inconnu.test", &ip) == E_NOENT && net_dns_cache_count() == 1);          /* un echec n'est pas retenu */
        CHECK(net_resolve("inconnu.test", &ip) == E_NOENT && n_dns_q == q0 + 4);
        for (int k = 0; k < 12; k++) {                                  /* plus de 8 noms : les plus anciens sont remplaces */
            char nn[24] = "hote";
            nn[4] = (char)('a' + k); nn[5] = 0;
            CHECK(net_resolve(nn, &ip) == E_OK);
        }
        CHECK(net_dns_cache_count() == NET_DNS_CACHE_MAX);
        q0 = n_dns_q;
        CHECK(net_resolve("hotel", &ip) == E_OK && n_dns_q == q0);       /* le plus recent est la */
        CHECK(net_resolve("hotea", &ip) == E_OK && n_dns_q == q0 + 1);   /* le plus ancien est parti */
        net_dns_flush();
        CHECK(net_dns_cache_count() == 0);
        CHECK(net_resolve("10.9.8.7", &ip) == E_OK && net_dns_cache_count() == 0);   /* un nombre ne passe pas par le cache */
        net_get_status(&s2);
        CHECK(s2.dns_queries > s1.dns_queries);
    }

    /* on nous pose des questions : ARP et echo vers nous */
    {
        u8 f[200], a[28];
        u32 before = last_arp_reply_seen, before_i = last_icmp_reply_seen, n;
        memcpy(f, nd->mac, 6); memcpy(f, "\xff\xff\xff\xff\xff\xff", 6);          /* diffusion */
        memcpy(f + 6, GW_MAC, 6); put16(f + 12, 0x0806);
        put16(a, 1); put16(a + 2, 0x0800); a[4] = 6; a[5] = 4; put16(a + 6, 1);
        memcpy(a + 8, GW_MAC, 6); put32(a + 14, IP_GW); memset(a + 18, 0, 6); put32(a + 24, IP_US);
        memcpy(f + 14, a, 28);
        inject(f, 42, 0);
        net_poll();
        CHECK(last_arp_reply_seen == before + 1);
        /* echo vers nous */
        u8 ic[40];
        memset(ic, 0, sizeof ic); ic[0] = 8; put16(ic + 4, 7); put16(ic + 6, 9);
        for (int i = 8; i < 40; i++) ic[i] = (u8)i;
        put16(ic + 2, cs(ic, 40, 0));
        n = build_ip(f, OUR_MAC, GW_MAC, IP_GW, IP_US, 1, ic, 40);
        inject(f, n, 0);
        net_poll();
        CHECK(last_icmp_reply_seen == before_i + 1);
        /* hostiles : somme ICMP fausse, IP tronque, fragment, IP pour quelqu'un d'autre, version 6 : aucune reponse, aucun plantage */
        ic[10] ^= 0x55;
        n = build_ip(f, OUR_MAC, GW_MAC, IP_GW, IP_US, 1, ic, 40); inject(f, n, 0);
        ic[10] ^= 0x55;
        n = build_ip(f, OUR_MAC, GW_MAC, IP_GW, IP_US, 1, ic, 40); f[14 + 6] = 0x20; inject(f, n, 0);       /* MF : fragment */
        n = build_ip(f, OUR_MAC, GW_MAC, IP_GW, 0x0A000263u, 1, ic, 40); inject(f, n, 0);                  /* pas pour nous */
        n = build_ip(f, OUR_MAC, GW_MAC, IP_GW, IP_US, 1, ic, 40); f[14] = 0x65; inject(f, n, 0);          /* version 6 */
        n = build_ip(f, OUR_MAC, GW_MAC, IP_GW, IP_US, 1, ic, 40); f[14 + 2] = 0xFF; inject(f, n, 0);       /* longueur > trame */
        inject(f, 20, 0);                                                                                    /* trame trop courte */
        inject(f, 14, 0);
        memset(f, 0, 14); memcpy(f, "\x02\x00\x00\x00\x00\x01", 6); inject(f, 60, 0);                         /* pas pour notre MAC */
        net_poll();
        CHECK(last_icmp_reply_seen == before_i + 1);
        net_get_status(&st);
        CHECK(st.ip_bad >= 3);
    }

    /* tache noyau */
    net_start();
    CHECK(task_made == 1 && task_entry != NULL);

    out("  reseau verifie : PCI, pilote e1000 (anneaux, erreurs, rotation), ARP, ping, DHCP, DNS, trames hostiles\n");
    return 0;
}
