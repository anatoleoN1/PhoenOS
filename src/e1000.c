/*
 * PhoenOS - e1000.c : pilote OFFICIEL de la carte reseau Intel PRO/1000 (famille 8254x : 82540EM, 82545EM...).
 * C'est la carte que QEMU emule par defaut et celle que VirtualBox propose ("Intel PRO/1000 MT Desktop").
 *
 * Principe : les registres sont en memoire (MMIO, BAR0) ; l'emission et la reception passent par deux anneaux de
 * descripteurs places en RAM (DMA) que la carte lit et ecrit toute seule. Pas d'interruption : la pile reseau
 * interroge la carte regulierement (tache "net").
 *   Reception : 32 descripteurs, chacun pointe sur un tampon de 2048 octets ; la carte met le bit DD quand elle a rempli
 *               un tampon. Le pilote le lit, rend le descripteur (RDT) et passe au suivant.
 *   Emission  : 16 descripteurs ; le pilote ecrit la trame dans un tampon, lance la carte (TDT) et attend le bit DD.
 */
#include "driver.h"
#include "netdev.h"
#include "paging.h"
#include "pmm.h"
#include "dlog.h"
#include "string.h"

#define REG_CTRL    0x0000
#define REG_STATUS  0x0008
#define REG_EERD    0x0014
#define REG_IMC     0x00D8
#define REG_RCTL    0x0100
#define REG_TCTL    0x0400
#define REG_TIPG    0x0410
#define REG_RDBAL   0x2800
#define REG_RDBAH   0x2804
#define REG_RDLEN   0x2808
#define REG_RDH     0x2810
#define REG_RDT     0x2818
#define REG_TDBAL   0x3800
#define REG_TDBAH   0x3804
#define REG_TDLEN   0x3808
#define REG_TDH     0x3810
#define REG_TDT     0x3818
#define REG_MTA     0x5200
#define REG_RAL0    0x5400
#define REG_RAH0    0x5404

#define CTRL_SLU    (1u << 6)
#define CTRL_RST    (1u << 26)
#define STATUS_LU   (1u << 1)
#define RCTL_EN     (1u << 1)
#define RCTL_BAM    (1u << 15)
#define RCTL_SECRC  (1u << 26)
#define TCTL_EN     (1u << 1)
#define TCTL_PSP    (1u << 3)
#define RAH_AV      (1u << 31)

#define RX_N        32
#define TX_N        16
#define BUF_SIZE    2048u
#define MMIO_SIZE   0x20000u

struct rx_desc { u32 addr_lo, addr_hi; u16 length, csum; u8 status, errors; u16 special; } __attribute__((packed));
struct tx_desc { u32 addr_lo, addr_hi; u16 length; u8 cso, cmd, status, css; u16 special; } __attribute__((packed));
#define DESC_DD     0x01
#define DESC_EOP    0x02
#define TXCMD       0x0B                /* EOP | IFCS (la carte ajoute le CRC) | RS (elle rend compte dans DD) */

struct e1000 {
    u32 base;                           /* adresse des registres */
    volatile struct rx_desc *rx;
    volatile struct tx_desc *tx;
    u8 *rxbuf[RX_N];
    u8 *txbuf[TX_N];
    u32 rx_cur, tx_cur;
    struct netdev nd;
};

/* Acces aux registres : definitions faibles, remplacees par les tests (faux materiel). */
__attribute__((weak)) u32 e1000_hw_read32(u32 addr) { return *(volatile u32 *)(usize)addr; }
__attribute__((weak)) void e1000_hw_write32(u32 addr, u32 v) { *(volatile u32 *)(usize)addr = v; }
/* Attente (ms) : definition faible, remplacee par les tests. */
__attribute__((weak)) void e1000_hw_delay_ms(u32 ms) { for (volatile u32 i = 0; i < ms * 20000u; i++) ; }

static struct e1000 card;               /* une seule carte pour l'instant */
static int attached;

static u32 rd(const struct e1000 *c, u32 off) { return e1000_hw_read32(c->base + off); }
static void wr(const struct e1000 *c, u32 off, u32 v) { e1000_hw_write32(c->base + off, v); }
static void barrier(void) { __asm__ volatile ("" : : : "memory"); }

static int link_up(struct netdev *d)
{
    struct e1000 *c = d->priv;

    return (rd(c, REG_STATUS) & STATUS_LU) != 0;
}

static err_t do_send(struct netdev *d, const u8 *frame, u32 len)
{
    struct e1000 *c = d->priv;
    volatile struct tx_desc *t = &c->tx[c->tx_cur];
    u32 waited = 0;

    if (len < 14 || len > NET_FRAME_MAX) {
        d->tx_errors++;
        return E_INVAL;
    }
    while (!(t->status & DESC_DD) && t->cmd) {      /* ce descripteur est encore en cours d'emission : on attend un peu */
        if (waited++ > 1000) {
            d->tx_errors++;
            return E_TIMEOUT;
        }
        e1000_hw_delay_ms(1);
        barrier();
    }
    memcpy(c->txbuf[c->tx_cur], frame, len);
    if (len < 60) {                                 /* trame Ethernet minimale : 60 octets avant le CRC */
        memset(c->txbuf[c->tx_cur] + len, 0, 60 - len);
        len = 60;
    }
    t->length = (u16)len;
    t->cso = 0;
    t->css = 0;
    t->special = 0;
    t->status = 0;
    t->cmd = TXCMD;
    barrier();
    c->tx_cur = (c->tx_cur + 1) % TX_N;
    wr(c, REG_TDT, c->tx_cur);
    d->tx_frames++;
    d->tx_bytes += len;
    return E_OK;
}

static int do_recv(struct netdev *d, u8 *buf, u32 cap)
{
    struct e1000 *c = d->priv;
    volatile struct rx_desc *r = &c->rx[c->rx_cur];
    u32 len;

    barrier();
    if (!(r->status & DESC_DD))
        return 0;
    len = r->length;
    if (!(r->status & DESC_EOP) || r->errors || len < 14 || len > BUF_SIZE || len > cap) {
        d->rx_dropped++;                            /* trame coupee, en erreur ou trop grande : on la jette */
        len = 0;
    } else {
        memcpy(buf, c->rxbuf[c->rx_cur], len);
        d->rx_frames++;
        d->rx_bytes += len;
    }
    r->status = 0;
    wr(c, REG_RDT, c->rx_cur);                      /* rend le descripteur a la carte */
    c->rx_cur = (c->rx_cur + 1) % RX_N;
    return (int)len;
}

static int read_mac(struct e1000 *c, u8 *mac)
{
    u32 lo = rd(c, REG_RAL0), hi = rd(c, REG_RAH0);

    if (hi & RAH_AV) {
        mac[0] = (u8)lo; mac[1] = (u8)(lo >> 8); mac[2] = (u8)(lo >> 16); mac[3] = (u8)(lo >> 24);
        mac[4] = (u8)hi; mac[5] = (u8)(hi >> 8);
        return 1;
    }
    for (int i = 0; i < 3; i++) {                   /* sinon : lue dans la memoire EEPROM de la carte */
        u32 v = 0;

        wr(c, REG_EERD, 1u | ((u32)i << 8));
        for (int t = 0; t < 1000; t++) {
            v = rd(c, REG_EERD);
            if (v & 0x10)
                break;
            e1000_hw_delay_ms(1);
        }
        if (!(v & 0x10))
            return 0;
        mac[2 * i] = (u8)(v >> 16);
        mac[2 * i + 1] = (u8)(v >> 24);
    }
    return 1;
}

static err_t attach(const struct pci_dev *p)
{
    struct e1000 *c = &card;
    u32 bar, ring_rx, ring_tx, f;
    err_t e;

    if (attached)
        return E_BUSY;                              /* une seule carte pour l'instant */
    e = pci_bar_mem(p, 0, &bar);
    if (e != E_OK)
        return e;
    memset(c, 0, sizeof *c);
    e = paging_map_mmio(bar, MMIO_SIZE);
    if (e != E_OK)
        return e;
    c->base = bar;
    pci_enable(p, 1);                               /* acces a la memoire et DMA */

    wr(c, REG_IMC, 0xFFFFFFFFu);                    /* aucune interruption : on interroge */
    wr(c, REG_CTRL, rd(c, REG_CTRL) | CTRL_RST);    /* remise a zero de la carte */
    for (int t = 0; t < 100 && (rd(c, REG_CTRL) & CTRL_RST); t++)
        e1000_hw_delay_ms(1);
    if (rd(c, REG_CTRL) & CTRL_RST)
        return E_TIMEOUT;
    wr(c, REG_IMC, 0xFFFFFFFFu);
    wr(c, REG_CTRL, rd(c, REG_CTRL) | CTRL_SLU);    /* lien actif */
    if (!read_mac(c, c->nd.mac))
        return E_IO;
    for (u32 i = 0; i < 128; i++)                   /* table de multidiffusion vide */
        wr(c, REG_MTA + 4 * i, 0);

    /* anneaux et tampons : de la RAM (la memoire du noyau est en identite : adresse virtuelle = adresse physique) */
    ring_rx = pmm_alloc_frame();
    ring_tx = pmm_alloc_frame();
    if (!ring_rx || !ring_tx) {
        if (ring_rx) pmm_free_frame(ring_rx);
        if (ring_tx) pmm_free_frame(ring_tx);
        return E_NOMEM;
    }
    memset((void *)ring_rx, 0, PAGE_SIZE);
    memset((void *)ring_tx, 0, PAGE_SIZE);
    c->rx = (volatile struct rx_desc *)ring_rx;
    c->tx = (volatile struct tx_desc *)ring_tx;
    for (int i = 0; i < RX_N; i += 2) {             /* deux tampons de 2 Kio par page */
        f = pmm_alloc_frame();
        if (!f)
            return E_NOMEM;                         /* (les pages deja prises restent au noyau : echec au demarrage seulement) */
        c->rxbuf[i] = (u8 *)f;
        c->rxbuf[i + 1] = (u8 *)f + BUF_SIZE;
    }
    for (int i = 0; i < TX_N; i += 2) {
        f = pmm_alloc_frame();
        if (!f)
            return E_NOMEM;
        c->txbuf[i] = (u8 *)f;
        c->txbuf[i + 1] = (u8 *)f + BUF_SIZE;
    }
    for (int i = 0; i < RX_N; i++) {
        c->rx[i].addr_lo = (u32)c->rxbuf[i];
        c->rx[i].addr_hi = 0;
        c->rx[i].status = 0;
    }
    for (int i = 0; i < TX_N; i++) {
        c->tx[i].addr_lo = (u32)c->txbuf[i];
        c->tx[i].addr_hi = 0;
        c->tx[i].cmd = 0;
        c->tx[i].status = DESC_DD;
    }
    wr(c, REG_RDBAL, ring_rx);
    wr(c, REG_RDBAH, 0);
    wr(c, REG_RDLEN, RX_N * (u32)sizeof(struct rx_desc));
    wr(c, REG_RDH, 0);
    wr(c, REG_RDT, RX_N - 1);                       /* toutes les cases sauf une appartiennent a la carte */
    wr(c, REG_TDBAL, ring_tx);
    wr(c, REG_TDBAH, 0);
    wr(c, REG_TDLEN, TX_N * (u32)sizeof(struct tx_desc));
    wr(c, REG_TDH, 0);
    wr(c, REG_TDT, 0);
    wr(c, REG_TIPG, 0x0060200Au);
    wr(c, REG_TCTL, TCTL_EN | TCTL_PSP | (0x10u << 4) | (0x40u << 12));
    wr(c, REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_SECRC);       /* trames pour nous et diffusion, 2048 octets, sans le CRC */

    c->nd.driver = "e1000";
    c->nd.link_up = link_up;
    c->nd.send = do_send;
    c->nd.recv = do_recv;
    c->nd.priv = c;
    e = netdev_register(&c->nd);
    if (e != E_OK)
        return e;
    attached = 1;
    DINFO("e1000", "carte %s : MAC %02x:%02x:%02x:%02x:%02x:%02x, registres a %#x, lien %s", c->nd.name, c->nd.mac[0], c->nd.mac[1],
          c->nd.mac[2], c->nd.mac[3], c->nd.mac[4], c->nd.mac[5], bar, link_up(&c->nd) ? "actif" : "inactif");
    return E_OK;
}

/* Modeles reconnus : 82540EM (QEMU, VirtualBox), 82545EM (VirtualBox "MT Server"/VMware), 82543GC, 82541, 82544... */
static const struct pci_id ids[] = {
    { 0x8086, 0x100E }, { 0x8086, 0x100F }, { 0x8086, 0x1004 }, { 0x8086, 0x1001 }, { 0x8086, 0x1008 },
    { 0x8086, 0x1019 }, { 0x8086, 0x101E }, { 0x8086, 0x1026 }, { 0x8086, 0x1076 }, { 0x8086, 0x1078 },
};

const struct driver e1000_driver = {
    .name = "e1000",
    .version = "0.9.0",
    .summary = "Carte reseau Intel PRO/1000 (QEMU, VirtualBox)",
    .kind = "ethernet",
    .official = 1,
    .ids = ids,
    .nids = (int)(sizeof ids / sizeof ids[0]),
    .attach = attach,
};

void e1000_reset_for_tests(void)
{
    attached = 0;
}
