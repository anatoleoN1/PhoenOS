/*
 * PhoenOS - ata.c
 * Pilote ATA PIO (interrogation, sans interruption).
 *
 * Registres d'un canal (base = 0x1F0 primaire, 0x170 secondaire) :
 *   +0 données (16 bits)  +1 erreur  +2 nombre de secteurs  +3/+4/+5 adresse LBA bas/milieu/haut
 *   +6 choix du disque + LBA haut  +7 état (lecture) / commande (écriture)
 * Registre de contrôle (0x3F6 / 0x376) : état alternatif (lecture), contrôle (écriture).
 */
#include "ata.h"
#include "io.h"
#include "string.h"

#define REG_DATA    0
#define REG_COUNT   2
#define REG_LBA0    3
#define REG_LBA1    4
#define REG_LBA2    5
#define REG_DRIVE   6
#define REG_STATUS  7
#define REG_CMD     7

#define ST_ERR  0x01
#define ST_DRQ  0x08
#define ST_DF   0x20
#define ST_BSY  0x80

#define CMD_READ     0x20
#define CMD_WRITE    0x30
#define CMD_FLUSH    0xE7
#define CMD_IDENTIFY 0xEC

#define POLL_MAX 2000000                        /* lectures d'état avant d'abandonner (~ quelques secondes) */
#define MAX_DEVS 4

static struct ata_dev devs[MAX_DEVS];
static int ndevs;

/* ~400 ns d'attente exigée après un changement de disque ou une commande : 4 lectures d'état. */
static void delay400(const struct ata_dev *d)
{
    for (int i = 0; i < 4; i++)
        (void)inb(d->ctrl);
}

/* Attend que le disque ne soit plus occupé. Renvoie l'état, ou -1 si délai dépassé. */
static int wait_not_busy(const struct ata_dev *d)
{
    for (int i = 0; i < POLL_MAX; i++) {
        u8 st = inb(d->io + REG_STATUS);

        if (!(st & ST_BSY))
            return st;
    }
    return -1;
}

/* Attend DRQ (données prêtes). E_IO si erreur de disque ou délai dépassé. */
static err_t wait_drq(const struct ata_dev *d)
{
    for (int i = 0; i < POLL_MAX; i++) {
        u8 st = inb(d->io + REG_STATUS);

        if (st & ST_BSY)
            continue;
        if (st & (ST_ERR | ST_DF))
            return E_IO;
        if (st & ST_DRQ)
            return E_OK;
    }
    return E_TIMEOUT;
}

static void select_drive(const struct ata_dev *d, u32 lba)
{
    outb((u16)(d->io + REG_DRIVE), (u8)(0xE0 | (d->drive << 4) | ((lba >> 24) & 0x0F)));
    delay400(d);
}

static void set_model(struct ata_dev *d, const char *text)
{
    usize n = strlen(text);

    if (n > sizeof(d->model) - 1)
        n = sizeof(d->model) - 1;
    memcpy(d->model, text, n);
    d->model[n] = '\0';
}

static void swap_string(char *dst, const u16 *words, int nwords)
{
    for (int i = 0; i < nwords; i++) {              /* chaque mot contient deux caractères dans l'ordre inverse */
        dst[2 * i]     = (char)(words[i] >> 8);
        dst[2 * i + 1] = (char)(words[i] & 0xFF);
    }
    dst[2 * nwords] = '\0';
    for (int i = 2 * nwords - 1; i >= 0 && (dst[i] == ' ' || dst[i] == '\0'); i--)
        dst[i] = '\0';
}

/* Tente d'identifier le périphérique (channel, drive). Renvoie 1 s'il y a quelque chose. */
static int probe(struct ata_dev *d)
{
    u16 id[256];
    int st;
    u8 mid, high;

    d->present = 0;
    d->atapi = 0;
    outb(d->ctrl, 0x02);                            /* nIEN : pas d'interruption (nous interrogeons) */
    select_drive(d, 0);

    outb((u16)(d->io + REG_COUNT), 0);
    outb((u16)(d->io + REG_LBA0), 0);
    outb((u16)(d->io + REG_LBA1), 0);
    outb((u16)(d->io + REG_LBA2), 0);
    outb((u16)(d->io + REG_CMD), CMD_IDENTIFY);
    delay400(d);

    st = inb(d->io + REG_STATUS);
    if (st == 0 || st == 0xFF)
        return 0;                                   /* rien sur ce canal (ou bus flottant) */
    st = wait_not_busy(d);
    if (st < 0)
        return 0;

    mid  = inb(d->io + REG_LBA1);
    high = inb(d->io + REG_LBA2);
    if (mid != 0 || high != 0) {                    /* signature : ATAPI (0x14,0xEB) ou SATA (0x3C,0xC3) */
        d->present = 1;
        d->atapi = 1;
        d->sectors = 0;
        set_model(d, mid == 0x14 ? "Lecteur ATAPI (CD/DVD)" : "Peripherique non ATA");
        return 1;
    }
    if (wait_drq(d) != E_OK)
        return 0;

    for (int i = 0; i < 256; i++)
        id[i] = inw((u16)(d->io + REG_DATA));

    d->sectors = ((u32)id[61] << 16) | id[60];      /* nombre de secteurs LBA28 */
    if (d->sectors > 0x0FFFFFFFu)
        d->sectors = 0x0FFFFFFFu;
    swap_string(d->model, id + 27, 20);
    if (d->model[0] == '\0')
        set_model(d, "Disque ATA");
    d->present = 1;
    return d->sectors != 0;
}

void ata_init(void)
{
    static const u16 io[2]   = { 0x1F0, 0x170 };
    static const u16 ctrl[2] = { 0x3F6, 0x376 };

    memset(devs, 0, sizeof(devs));
    ndevs = 0;

    for (int ch = 0; ch < 2; ch++) {
        for (int dr = 0; dr < 2; dr++) {
            struct ata_dev d;

            memset(&d, 0, sizeof(d));
            d.channel = (u8)ch;
            d.drive = (u8)dr;
            d.io = io[ch];
            d.ctrl = ctrl[ch];
            if (probe(&d))
                devs[ndevs++] = d;
        }
    }
}

int ata_count(void)
{
    return ndevs;
}

const struct ata_dev *ata_dev(int i)
{
    return (i >= 0 && i < ndevs) ? &devs[i] : NULL;
}

const struct ata_dev *ata_first_disk(void)
{
    for (int i = 0; i < ndevs; i++)
        if (!devs[i].atapi && devs[i].sectors)
            return &devs[i];
    return NULL;
}

static err_t check_range(const struct ata_dev *d, u32 lba, u32 count)
{
    if (!d || !d->present || d->atapi)
        return E_INVAL;
    if (count == 0)
        return E_OK;
    if (lba >= d->sectors || count > d->sectors - lba)
        return E_INVAL;
    return E_OK;
}

static void issue(const struct ata_dev *d, u32 lba, u8 cmd)
{
    select_drive(d, lba);
    outb((u16)(d->io + REG_COUNT), 1);
    outb((u16)(d->io + REG_LBA0), (u8)lba);
    outb((u16)(d->io + REG_LBA1), (u8)(lba >> 8));
    outb((u16)(d->io + REG_LBA2), (u8)(lba >> 16));
    outb((u16)(d->io + REG_CMD), cmd);
    delay400(d);
}

err_t ata_read(const struct ata_dev *d, u32 lba, u32 count, void *buf)
{
    u16 *w = buf;
    err_t e = check_range(d, lba, count);

    if (e != E_OK)
        return e;
    for (u32 s = 0; s < count; s++) {
        issue(d, lba + s, CMD_READ);
        e = wait_drq(d);
        if (e != E_OK)
            return e == E_TIMEOUT ? E_TIMEOUT : E_IO;
        for (int i = 0; i < 256; i++)
            *w++ = inw((u16)(d->io + REG_DATA));
    }
    return E_OK;
}

err_t ata_write(const struct ata_dev *d, u32 lba, u32 count, const void *buf)
{
    const u16 *w = buf;
    err_t e = check_range(d, lba, count);

    if (e != E_OK)
        return e;
    for (u32 s = 0; s < count; s++) {
        int st;

        issue(d, lba + s, CMD_WRITE);
        e = wait_drq(d);
        if (e != E_OK)
            return e == E_TIMEOUT ? E_TIMEOUT : E_IO;
        for (int i = 0; i < 256; i++)
            outw((u16)(d->io + REG_DATA), *w++);
        st = wait_not_busy(d);                      /* le disque range le secteur */
        if (st < 0)
            return E_TIMEOUT;
        if (st & (ST_ERR | ST_DF))
            return E_IO;
    }
    return E_OK;
}

err_t ata_flush(const struct ata_dev *d)
{
    int st;

    if (!d || !d->present || d->atapi)
        return E_INVAL;
    select_drive(d, 0);
    outb((u16)(d->io + REG_CMD), CMD_FLUSH);
    delay400(d);
    st = wait_not_busy(d);
    if (st < 0)
        return E_TIMEOUT;
    return (st & (ST_ERR | ST_DF)) ? E_IO : E_OK;
}
