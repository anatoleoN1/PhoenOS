/* PhoenOS - updstore.c : etat et emplacements des mises a jour sur le disque. Voir updstore.h. */
#include "updstore.h"
#include "crc32.h"
#include "string.h"

#define CTL_MAGIC  0x43555048u          /* "PHUC" */
#define SLOT_MAGIC 0x53555048u          /* "PHUS" */

static u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static void wr32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

static u32 slot_lba(int slot) { return UPD_SLOT0_LBA + (u32)slot * UPD_SLOT_SECTORS; }

err_t upd_area_check(const struct upd_blk *b)
{
    if (!b || !b->rd || !b->wr)
        return E_INVAL;
    return b->sectors >= UPD_BASE_LBA + UPD_AREA_SECTORS ? E_OK : E_NOSYS;
}

static int ctl_parse(const u8 *s, struct upd_ctl *c)
{
    if (rd32(s) != CTL_MAGIC || crc32_update(0, s, 508) != rd32(s + 508))
        return 0;
    c->gen = rd32(s + 4);
    c->active = (i32)rd32(s + 8);
    c->tries = rd32(s + 12);
    c->trial = rd32(s + 16);
    c->min_build = rd32(s + 20);
    c->autom = rd32(s + 24);
    if (c->active < -1 || c->active > 1 || c->autom > 2 || c->trial > 1)
        return 0;
    return 1;
}

static int blank(const u8 *s)
{
    for (u32 i = 0; i < UPD_SECTOR; i++)
        if (s[i])
            return 0;
    return 1;
}

err_t upd_ctl_load(const struct upd_blk *b, struct upd_ctl *c)
{
    static u8 sec[2][UPD_SECTOR];
    struct upd_ctl t[2];
    int ok[2];
    err_t e = upd_area_check(b);

    if (e != E_OK)
        return e;
    for (int i = 0; i < 2; i++) {
        e = b->rd(b->ctx, UPD_BASE_LBA + (u32)i, 1, sec[i]);
        if (e != E_OK)
            return E_IO;
        ok[i] = ctl_parse(sec[i], &t[i]);
    }
    if (!ok[0] && !ok[1])
        return (blank(sec[0]) && blank(sec[1])) ? E_NOENT : E_INVAL;
    if (ok[0] && ok[1])
        *c = ((i32)(t[1].gen - t[0].gen) > 0) ? t[1] : t[0];
    else
        *c = ok[0] ? t[0] : t[1];
    return E_OK;
}

err_t upd_ctl_store(const struct upd_blk *b, struct upd_ctl *c)
{
    static u8 sec[UPD_SECTOR];
    u32 gen = c->gen + 1;
    err_t e = upd_area_check(b);

    if (e != E_OK)
        return e;
    if (c->active < -1 || c->active > 1 || c->autom > 2 || c->trial > 1)
        return E_INVAL;
    memset(sec, 0, sizeof sec);
    wr32(sec, CTL_MAGIC);
    wr32(sec + 4, gen);
    wr32(sec + 8, (u32)c->active);
    wr32(sec + 12, c->tries);
    wr32(sec + 16, c->trial);
    wr32(sec + 20, c->min_build);
    wr32(sec + 24, c->autom);
    wr32(sec + 508, crc32_update(0, sec, 508));
    e = b->wr(b->ctx, UPD_BASE_LBA + (gen & 1u), 1, sec);      /* la copie qui n'est pas la derniere valide */
    if (e == E_OK && b->flush)
        e = b->flush(b->ctx);
    if (e != E_OK)
        return E_IO;
    c->gen = gen;
    return E_OK;
}

err_t upd_area_prepare(const struct upd_blk *b, int force)
{
    static u8 sec[2][UPD_SECTOR];
    struct upd_ctl c;
    err_t e = upd_area_check(b);

    if (e != E_OK)
        return e;
    e = upd_ctl_load(b, &c);
    if (e == E_OK)
        return E_OK;                                   /* deja prete */
    if (e == E_IO)
        return e;
    if (e == E_INVAL && !force)
        return E_BUSY;                                 /* quelque chose d'autre occupe ces secteurs */
    for (int i = 0; i < 2; i++)
        if (b->rd(b->ctx, UPD_BASE_LBA + (u32)i, 1, sec[i]) != E_OK)
            return E_IO;
    if (!force && !(blank(sec[0]) && blank(sec[1])))
        return E_BUSY;
    memset(&c, 0, sizeof c);
    c.active = -1;
    c.autom = 1;
    c.gen = 0;
    memset(sec, 0, sizeof sec);
    if (b->wr(b->ctx, UPD_BASE_LBA + 0, 1, sec[0]) != E_OK || b->wr(b->ctx, UPD_BASE_LBA + 1, 1, sec[1]) != E_OK)
        return E_IO;
    for (u32 s = 0; s < 2; s++) {                       /* les deux emplacements sont declares vides */
        if (upd_slot_invalidate(b, (int)s) != E_OK)
            return E_IO;
    }
    return upd_ctl_store(b, &c);
}

err_t upd_slot_load(const struct upd_blk *b, int slot, struct upd_slot *s)
{
    static u8 h[UPD_SECTOR], m[2 * UPD_SECTOR];

    if (slot < 0 || slot > 1 || upd_area_check(b) != E_OK)
        return E_INVAL;
    if (b->rd(b->ctx, slot_lba(slot), 1, h) != E_OK)
        return E_IO;
    if (rd32(h) != SLOT_MAGIC || rd32(h + 4) != 1 || crc32_update(0, h, 508) != rd32(h + 508))
        return E_NOENT;
    s->build = rd32(h + 8);
    s->kernel_bytes = rd32(h + 12);
    s->initrd_bytes = rd32(h + 16);
    s->man_len = (u32)h[20] | ((u32)h[21] << 8);
    if (s->man_len == 0 || s->man_len > UPD_MAN_MAX || s->kernel_bytes == 0 || s->kernel_bytes > UPD_KERNEL_MAX_BYTES ||
        s->initrd_bytes == 0 || s->initrd_bytes > UPD_INITRD_MAX_BYTES)
        return E_NOENT;
    if (b->rd(b->ctx, slot_lba(slot) + UPD_MAN_OFF, 2, m) != E_OK)
        return E_IO;
    memcpy(s->sig, m, 64);
    memcpy(s->man, m + 64, s->man_len);
    s->man[s->man_len] = 0;
    return E_OK;
}

err_t upd_slot_invalidate(const struct upd_blk *b, int slot)
{
    static u8 z[UPD_SECTOR];

    if (slot < 0 || slot > 1 || upd_area_check(b) != E_OK)
        return E_INVAL;
    memset(z, 0, sizeof z);
    if (b->wr(b->ctx, slot_lba(slot), 1, z) != E_OK)
        return E_IO;
    if (b->flush && b->flush(b->ctx) != E_OK)
        return E_IO;
    return E_OK;
}

static err_t slot_range(const struct upd_blk *b, int slot, u32 off, u32 count)
{
    if (slot < 0 || slot > 1 || upd_area_check(b) != E_OK || count == 0)
        return E_INVAL;
    if (off < UPD_KERNEL_OFF || off + count < off || off + count > UPD_SLOT_SECTORS)
        return E_INVAL;
    if (off < UPD_INITRD_OFF && off + count > UPD_INITRD_OFF)      /* une ecriture ne enjambe pas deux zones */
        return E_INVAL;
    if (off < UPD_INITRD_OFF && off + count > UPD_KERNEL_OFF + UPD_KERNEL_MAX)
        return E_INVAL;
    return E_OK;
}

err_t upd_slot_write(const struct upd_blk *b, int slot, u32 off, u32 count, const void *buf)
{
    err_t e = slot_range(b, slot, off, count);

    if (e != E_OK)
        return e;
    return b->wr(b->ctx, slot_lba(slot) + off, count, buf) == E_OK ? E_OK : E_IO;
}

err_t upd_slot_read(const struct upd_blk *b, int slot, u32 off, u32 count, void *buf)
{
    err_t e = slot_range(b, slot, off, count);

    if (e != E_OK)
        return e;
    return b->rd(b->ctx, slot_lba(slot) + off, count, buf) == E_OK ? E_OK : E_IO;
}

err_t upd_slot_commit(const struct upd_blk *b, int slot, u32 build, u32 kernel_bytes, u32 initrd_bytes,
                      const char *man, u32 man_len, const u8 sig[64])
{
    static u8 h[UPD_SECTOR], m[2 * UPD_SECTOR];

    if (slot < 0 || slot > 1 || upd_area_check(b) != E_OK || !man || man_len == 0 || man_len > UPD_MAN_MAX ||
        kernel_bytes == 0 || kernel_bytes > UPD_KERNEL_MAX_BYTES || initrd_bytes == 0 || initrd_bytes > UPD_INITRD_MAX_BYTES)
        return E_INVAL;
    memset(m, 0, sizeof m);
    memcpy(m, sig, 64);
    memcpy(m + 64, man, man_len);
    if (b->wr(b->ctx, slot_lba(slot) + UPD_MAN_OFF, 2, m) != E_OK)
        return E_IO;
    if (b->flush && b->flush(b->ctx) != E_OK)
        return E_IO;
    memset(h, 0, sizeof h);
    wr32(h, SLOT_MAGIC);
    wr32(h + 4, 1);
    wr32(h + 8, build);
    wr32(h + 12, kernel_bytes);
    wr32(h + 16, initrd_bytes);
    h[20] = (u8)man_len;
    h[21] = (u8)(man_len >> 8);
    wr32(h + 508, crc32_update(0, h, 508));
    if (b->wr(b->ctx, slot_lba(slot), 1, h) != E_OK)
        return E_IO;
    if (b->flush && b->flush(b->ctx) != E_OK)
        return E_IO;
    return E_OK;
}
