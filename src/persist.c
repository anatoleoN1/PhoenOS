/*
 * PhoenOS - persist.c
 * Sauvegarde / restauration du système de fichiers sur disque (voir persist.h).
 *
 * En-tête d'un emplacement (1 secteur, entiers 32 bits petit-boutiste) :
 *    0  signature "PHOENFS1"      8  génération      12  taille des données (octets)
 *   16  CRC32 des données        20  CRC32 de l'en-tête (octets 0..19)
 * Les données (archive tar) commencent au secteur suivant.
 */
#include "persist.h"
#include "crc32.h"
#include "fs.h"
#include "tarw.h"
#include "kheap.h"
#include "string.h"

#define MAGIC "PHOENFS1"

struct header {
    u8  magic[8];
    u32 generation;
    u32 data_bytes;
    u32 data_crc;
    u32 header_crc;
};

struct slot {
    int valid;
    struct header h;
};

static struct persist_info info;
static const struct ata_dev *disk;
static struct slot slots[2];
static u32 saved_version;

static u32 slot_lba(int s)
{
    return s ? info.slot_sectors : 0;
}

static u32 slot_capacity_bytes(void)
{
    return (info.slot_sectors - 1) * ATA_SECTOR;
}

/* ---------------- en-têtes ---------------- */

static void encode_header(u8 *sector, u32 gen, u32 bytes, u32 crc)
{
    struct header h;

    memset(sector, 0, ATA_SECTOR);
    memcpy(h.magic, MAGIC, 8);
    h.generation = gen;
    h.data_bytes = bytes;
    h.data_crc = crc;
    h.header_crc = 0;
    memcpy(sector, &h, sizeof(h));
    h.header_crc = crc32_update(0, sector, 20);
    memcpy(sector + 20, &h.header_crc, 4);
}

static int decode_header(const u8 *sector, struct header *out)
{
    struct header h;

    memcpy(&h, sector, sizeof(h));
    if (memcmp(h.magic, MAGIC, 8) != 0)
        return 0;
    if (crc32_update(0, sector, 20) != h.header_crc)
        return 0;
    if (h.data_bytes > slot_capacity_bytes())
        return 0;
    *out = h;
    return 1;
}

static int sector_is_zero(const u8 *s)
{
    for (usize i = 0; i < ATA_SECTOR; i++)
        if (s[i])
            return 0;
    return 1;
}

/* ---------------- initialisation ---------------- */

static void pick_active(void)
{
    info.active = -1;
    info.generation = 0;
    info.snapshot_bytes = 0;
    for (int s = 0; s < 2; s++) {
        if (slots[s].valid && (info.active < 0 || slots[s].h.generation > info.generation)) {
            info.active = s;
            info.generation = slots[s].h.generation;
            info.snapshot_bytes = slots[s].h.data_bytes;
        }
    }
}

static void read_headers(void)
{
    u8 sector[ATA_SECTOR];

    for (int s = 0; s < 2; s++) {
        slots[s].valid = 0;
        if (ata_read(disk, slot_lba(s), 1, sector) == E_OK)
            slots[s].valid = decode_header(sector, &slots[s].h);
    }
    pick_active();
    info.state = info.active >= 0 ? PERSIST_READY : PERSIST_UNFORMATTED;
}

err_t persist_init(void)
{
    memset(&info, 0, sizeof(info));
    memset(slots, 0, sizeof(slots));
    info.active = -1;
    disk = ata_first_disk();
    saved_version = fs_version();
    if (!disk) {
        info.state = PERSIST_NO_DISK;
        return E_OK;
    }

    memcpy(info.model, disk->model, sizeof(info.model));
    info.sectors = disk->sectors;
    info.slot_sectors = disk->sectors / 2;
    if (info.slot_sectors > PERSIST_MAX_SLOT_SECTORS)
        info.slot_sectors = PERSIST_MAX_SLOT_SECTORS;
    if (info.slot_sectors < PERSIST_MIN_SLOT_SECTORS) {
        info.state = PERSIST_NO_DISK;                   /* trop petit pour être utile */
        disk = NULL;
        return E_NOSYS;
    }
    read_headers();
    return E_OK;
}

int persist_state(void)
{
    return info.state;
}

const struct persist_info *persist_get_info(void)
{
    return &info;
}

/* ---------------- formatage ---------------- */

err_t persist_format(int force)
{
    u8 sector[ATA_SECTOR];
    err_t e;

    if (!disk)
        return E_NOENT;
    if (!force && info.state != PERSIST_READY) {
        e = ata_read(disk, 0, 1, sector);
        if (e != E_OK)
            return e;
        if (!sector_is_zero(sector))
            return E_BUSY;                              /* le disque contient autre chose */
    }

    /* slot B (génération 0) d'abord, puis A (génération 1) : A est ainsi le plus récent */
    for (int s = 1; s >= 0; s--) {
        encode_header(sector, (u32)(s ? 0 : 1), 0, 0);
        e = ata_write(disk, slot_lba(s), 1, sector);
        if (e == E_OK)
            e = ata_flush(disk);
        if (e != E_OK)
            return e;
    }
    read_headers();
    saved_version = fs_version();
    return info.state == PERSIST_READY ? E_OK : E_IO;
}

/* ---------------- sauvegarde ---------------- */

struct save_ctx {
    u32   lba;                  /* prochain secteur à écrire          */
    u32   crc;
    u32   bytes;
    err_t err;
};

static err_t save_emit(const u8 *block, void *ctx)
{
    struct save_ctx *c = ctx;
    err_t e = ata_write(disk, c->lba, 1, block);

    if (e != E_OK)
        return e;
    c->lba++;
    c->bytes += ATA_SECTOR;
    c->crc = crc32_update(c->crc, block, ATA_SECTOR);
    return E_OK;
}

err_t persist_save(void)
{
    u8 sector[ATA_SECTOR];
    struct save_ctx c;
    int target;
    u32 gen, need, version = fs_version();
    err_t e;

    if (info.state != PERSIST_READY)
        return E_NOENT;

    need = tarw_size();
    if (need > slot_capacity_bytes())
        return E_FBIG;                                  /* plus gros que l'emplacement */

    target = (info.active == 0) ? 1 : 0;
    gen = info.generation + 1;

    /* 1. invalide l'emplacement cible : une écriture interrompue ne ressemblera jamais à une sauvegarde valide */
    memset(sector, 0, sizeof(sector));
    e = ata_write(disk, slot_lba(target), 1, sector);
    if (e != E_OK)
        return e;

    /* 2. écrit les données */
    c.lba = slot_lba(target) + 1;
    c.crc = 0;
    c.bytes = 0;
    c.err = E_OK;
    e = tarw_write(save_emit, &c);
    if (e == E_OK)
        e = ata_flush(disk);
    if (e != E_OK) {
        read_headers();                                 /* l'état du disque a peut-être changé : on relit */
        return e;
    }

    /* 3. écrit l'en-tête EN DERNIER : c'est lui qui « valide » la sauvegarde */
    encode_header(sector, gen, c.bytes, c.crc);
    e = ata_write(disk, slot_lba(target), 1, sector);
    if (e == E_OK)
        e = ata_flush(disk);
    read_headers();
    if (e != E_OK)
        return e;
    saved_version = version;
    return (info.active == target && info.generation == gen) ? E_OK : E_IO;
}

/* ---------------- chargement ---------------- */

/* Lit l'emplacement s et vérifie son CRC. Renvoie le tampon (à libérer avec kfree) ou NULL + *err. */
static u8 *read_slot(int s, err_t *err)
{
    u32 bytes = slots[s].h.data_bytes;
    u8 *buf;

    if (bytes == 0) {
        *err = E_OK;
        return NULL;
    }
    buf = kmalloc(bytes);
    if (!buf) {
        *err = E_NOMEM;
        return NULL;
    }
    *err = ata_read(disk, slot_lba(s) + 1, bytes / ATA_SECTOR, buf);
    if (*err == E_OK && crc32_update(0, buf, bytes) != slots[s].h.data_crc)
        *err = E_IO;                                    /* données abîmées */
    if (*err != E_OK) {
        kfree(buf);
        return NULL;
    }
    return buf;
}

err_t persist_load(struct initrd_stats *st)
{
    struct initrd_stats local;
    int order[2], n = 0;
    err_t last = E_IO;

    if (!st)
        st = &local;
    memset(st, 0, sizeof(*st));
    if (info.state != PERSIST_READY)
        return E_NOENT;

    /* essaie d'abord la génération la plus récente, puis l'autre emplacement valide */
    if (info.active >= 0)
        order[n++] = info.active;
    if (slots[1 - info.active].valid)
        order[n++] = 1 - info.active;

    for (int i = 0; i < n; i++) {
        err_t e;
        u8 *buf = read_slot(order[i], &e);

        if (e == E_OK) {
            if (buf) {
                e = initrd_load(buf, slots[order[i]].h.data_bytes, st);
                kfree(buf);
            }
            if (e == E_OK) {
                if (order[i] != info.active) {          /* repli sur l'ancienne sauvegarde */
                    info.active = order[i];
                    info.generation = slots[order[i]].h.generation;
                    info.snapshot_bytes = slots[order[i]].h.data_bytes;
                }
                return E_OK;
            }
        }
        last = e;
        if (e == E_NOMEM)
            return e;                                   /* inutile d'essayer l'autre : même taille */
    }
    return last;
}

/* ---------------- état ---------------- */

int persist_dirty(void)
{
    return fs_version() != saved_version;
}

void persist_mark_clean(void)
{
    saved_version = fs_version();
}

err_t persist_autosave(void)
{
    if (info.state != PERSIST_READY || !persist_dirty())
        return E_OK;
    return persist_save();
}
