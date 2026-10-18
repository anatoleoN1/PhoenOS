/* PhoenOS - pci.c : bus PCI (voir pci.h). */
#include "pci.h"
#include "io.h"
#include "string.h"

#define CFG_ADDR 0xCF8
#define CFG_DATA 0xCFC

static struct pci_dev devs[PCI_MAX_DEVS];
static int ndevs;

static u32 cfg_addr(u8 bus, u8 dev, u8 fn, u8 off)
{
    return 0x80000000u | ((u32)bus << 16) | ((u32)(dev & 31) << 11) | ((u32)(fn & 7) << 8) | (off & 0xFC);
}

u32 pci_read32(u8 bus, u8 dev, u8 fn, u8 off)
{
    outl(CFG_ADDR, cfg_addr(bus, dev, fn, off));
    return inl(CFG_DATA);
}

void pci_write32(u8 bus, u8 dev, u8 fn, u8 off, u32 v)
{
    outl(CFG_ADDR, cfg_addr(bus, dev, fn, off));
    outl(CFG_DATA, v);
}

u16 pci_read16(u8 bus, u8 dev, u8 fn, u8 off)
{
    return (u16)(pci_read32(bus, dev, fn, off) >> ((off & 2) * 8));
}

void pci_write16(u8 bus, u8 dev, u8 fn, u8 off, u16 v)
{
    u32 old = pci_read32(bus, dev, fn, off), sh = (off & 2) * 8;

    pci_write32(bus, dev, fn, off, (old & ~(0xFFFFu << sh)) | ((u32)v << sh));
}

static void add(u8 bus, u8 dev, u8 fn)
{
    struct pci_dev *d;
    u32 id = pci_read32(bus, dev, fn, 0), cls = pci_read32(bus, dev, fn, 8);

    if ((id & 0xFFFF) == 0xFFFF || ndevs >= PCI_MAX_DEVS)
        return;
    d = &devs[ndevs++];
    memset(d, 0, sizeof *d);
    d->bus = bus; d->dev = dev; d->fn = fn;
    d->vendor = (u16)id;
    d->device = (u16)(id >> 16);
    d->revision = (u8)cls;
    d->prog_if = (u8)(cls >> 8);
    d->subclass = (u8)(cls >> 16);
    d->class_code = (u8)(cls >> 24);
    d->irq = (u8)pci_read32(bus, dev, fn, 0x3C);
    if (((pci_read32(bus, dev, fn, 0x0C) >> 16) & 0x7F) == 0)       /* type d'en-tete 0 : un peripherique normal, avec 6 BAR */
        for (int i = 0; i < 6; i++)
            d->bar[i] = pci_read32(bus, dev, fn, (u8)(0x10 + 4 * i));
}

int pci_scan(void)
{
    ndevs = 0;
    if ((pci_read32(0, 0, 0, 0) & 0xFFFF) == 0xFFFF && (pci_read32(0, 1, 0, 0) & 0xFFFF) == 0xFFFF)
        return 0;                                                   /* ni hote ni voisin : pas de bus PCI */
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            u32 id = pci_read32((u8)bus, (u8)dev, 0, 0);

            if ((id & 0xFFFF) == 0xFFFF)
                continue;
            add((u8)bus, (u8)dev, 0);
            if ((pci_read32((u8)bus, (u8)dev, 0, 0x0C) >> 16) & 0x80)    /* peripherique a plusieurs fonctions */
                for (int fn = 1; fn < 8; fn++)
                    add((u8)bus, (u8)dev, (u8)fn);
        }
    }
    return ndevs;
}

int pci_count(void) { return ndevs; }

const struct pci_dev *pci_get(int i)
{
    return i >= 0 && i < ndevs ? &devs[i] : NULL;
}

const char *pci_class_name(u8 c, u8 s)
{
    switch (c) {
    case 0x00: return "Ancien peripherique";
    case 0x01: return s == 1 ? "Disque IDE" : s == 6 ? "Disque SATA" : s == 8 ? "Disque NVMe" : "Stockage";
    case 0x02: return s == 0 ? "Reseau Ethernet" : s == 0x80 ? "Reseau" : "Reseau";
    case 0x03: return s == 0 ? "Carte graphique VGA" : "Carte graphique";
    case 0x04: return "Multimedia";
    case 0x05: return "Memoire";
    case 0x06: return s == 0 ? "Pont hote" : s == 1 ? "Pont ISA" : s == 4 ? "Pont PCI" : "Pont";
    case 0x07: return "Communication";
    case 0x08: return "Systeme";
    case 0x0C: return s == 3 ? "Controleur USB" : "Bus serie";
    case 0x0D: return "Sans fil";
    default:   return "Autre";
    }
}

void pci_enable(const struct pci_dev *d, int bus_master)
{
    u16 cmd = pci_read16(d->bus, d->dev, d->fn, 0x04);

    cmd |= 0x0003;                                                  /* acces aux ports et a la memoire */
    if (bus_master)
        cmd |= 0x0004;
    pci_write16(d->bus, d->dev, d->fn, 0x04, cmd);
}

err_t pci_bar_mem(const struct pci_dev *d, int i, u32 *addr)
{
    u32 b;

    if (i < 0 || i > 5)
        return E_INVAL;
    b = d->bar[i];
    if ((b & 1) || (b & ~0xFu) == 0)
        return E_INVAL;                                             /* ports, ou BAR vide */
    if (((b >> 1) & 3) == 2) {                                      /* 64 bits : la moitie haute est dans le BAR suivant */
        if (i == 5 || d->bar[i + 1] != 0)
            return E_NOSYS;
    }
    *addr = b & ~0xFu;
    return E_OK;
}
