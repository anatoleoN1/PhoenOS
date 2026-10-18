#ifndef PHOENOS_PCI_H
#define PHOENOS_PCI_H

#include "types.h"
#include "error.h"

/*
 * Bus PCI (0.9.0), acces par ports 0xCF8 / 0xCFC ("mecanisme 1" : toutes les machines x86 courantes, QEMU, VirtualBox).
 * pci_scan() liste les peripheriques une fois au demarrage ; les pilotes (driver.h) s'attachent a ceux qu'ils connaissent.
 */
#define PCI_MAX_DEVS 64

struct pci_dev {
    u8  bus, dev, fn;
    u16 vendor, device;
    u8  class_code, subclass, prog_if, revision;
    u8  irq;                            /* ligne d'interruption annoncee par le BIOS (255 : aucune) */
    u32 bar[6];                         /* registres BAR tels quels (bit 0 : 1 = ports, 0 = memoire) */
};

#define PCI_CLASS_NETWORK   0x02
#define PCI_CLASS_DISPLAY   0x03
#define PCI_CLASS_BRIDGE    0x06

u32  pci_read32(u8 bus, u8 dev, u8 fn, u8 off);
void pci_write32(u8 bus, u8 dev, u8 fn, u8 off, u32 v);
u16  pci_read16(u8 bus, u8 dev, u8 fn, u8 off);
void pci_write16(u8 bus, u8 dev, u8 fn, u8 off, u16 v);

/* Parcourt les bus, remplit la table. Renvoie le nombre de peripheriques (0 : pas de bus PCI). */
int  pci_scan(void);
int  pci_count(void);
const struct pci_dev *pci_get(int i);

/* Nom lisible de la classe ("Reseau Ethernet", "Pont", ...). */
const char *pci_class_name(u8 class_code, u8 subclass);

/* Autorise le peripherique : acces memoire et ports, et (bus_master) acces direct a la RAM (DMA). */
void pci_enable(const struct pci_dev *d, int bus_master);

/* Adresse d'un BAR memoire (sans les bits de type) ; E_INVAL si c'est un BAR de ports ou vide, E_NOSYS si 64 bits au-dela de 4 Gio. */
err_t pci_bar_mem(const struct pci_dev *d, int i, u32 *addr);

#endif
