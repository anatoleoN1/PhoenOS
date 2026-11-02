/* PhoenOS - updboot.c : etape 0 des mises a jour (choix, essais, retour arriere, relance). Voir updboot.h. */
#include "updboot.h"
#include "updconf.h"
#include "crypto.h"
#include "multiboot.h"
#include "string.h"

#ifdef UPD_SIM_PHYS                     /* tests : la "memoire physique" est un tableau */
extern u8 *upd_sim_ram;
#define PHYS(a) ((void *)(upd_sim_ram + (a)))
#else
#define PHYS(a) ((void *)(usize)(a))
#endif

const char *upd_last_failure = "";

/* ------------------------------------------------------------------ choix de demarrage */

int upd_slot_verify(const struct upd_blk *b, int slot, const u8 *pk, const char *edition,
                    struct upd_slot *s, struct upd_manifest *m)
{
    if (!pk || upd_slot_load(b, slot, s) != E_OK)
        return -1;
    if (!upd_man_verify(pk, s->man, s->man_len, s->sig))
        return -2;
    if (upd_man_parse(s->man, s->man_len, m) != UPD_M_OK)
        return -3;
    if (strcmp(m->edition, edition) != 0)
        return -4;
    if (m->build != s->build || m->ksize != s->kernel_bytes || m->isize != s->initrd_bytes)
        return -5;
    return 0;
}

err_t upd_rollback(const struct upd_blk *b, struct upd_ctl *c, const u8 *pk, const char *edition, int *now_active)
{
    struct upd_slot s;
    struct upd_manifest m;
    int bad = c->active, other;
    err_t e;

    if (bad < 0)
        return E_NOENT;
    e = upd_slot_invalidate(b, bad);
    if (e != E_OK)
        return e;
    other = 1 - bad;
    if (upd_slot_verify(b, other, pk, edition, &s, &m) == 0) {
        c->active = other;
        if (s.build < c->min_build)
            c->min_build = s.build;                     /* retour volontaire a une version plus ancienne */
    } else {
        c->active = -1;
    }
    c->trial = 0;
    c->tries = 0;
    e = upd_ctl_store(b, c);
    if (now_active)
        *now_active = c->active;
    return e;
}

err_t upd_confirm(const struct upd_blk *b, u32 running_build)
{
    struct upd_ctl c;
    err_t e = upd_ctl_load(b, &c);

    if (e != E_OK)
        return e;
    if (!c.trial && !c.tries && c.min_build >= running_build)
        return E_OK;
    c.trial = 0;
    c.tries = 0;
    if (running_build > c.min_build)
        c.min_build = running_build;
    return upd_ctl_store(b, &c);
}

int upd_boot_select(const struct upd_boot_env *env, struct upd_boot_plan *plan)
{
    struct upd_ctl c;

    memset(plan, 0, sizeof *plan);
    plan->action = UPB_CONTINUE;
    plan->slot = -1;
    plan->why = "";
    if (!env->pubkey) { plan->why = "pas de cle de signature : mises a jour desactivees"; return UPB_CONTINUE; }
    if (env->relaunched) { plan->why = "version deja lancee par l'etape 0"; return UPB_CONTINUE; }
    if (!env->blk || upd_area_check(env->blk) != E_OK) { plan->why = "pas de zone de mise a jour"; return UPB_CONTINUE; }
    if (upd_ctl_load(env->blk, &c) != E_OK) { plan->why = "zone de mise a jour non preparee"; return UPB_CONTINUE; }

    for (int pass = 0; pass < 2; pass++) {
        int v;

        if (c.active < 0) {
            plan->why = "version du CD";
            return UPB_CONTINUE;
        }
        v = upd_slot_verify(env->blk, c.active, env->pubkey, env->running_edition, &plan->s, &plan->m);
        if (v != 0) {
            plan->why = "emplacement actif invalide : retour arriere";
        } else if (plan->s.build <= env->running_build) {
            plan->why = "la version du CD est aussi recente";
            return UPB_CONTINUE;
        } else if (c.tries >= UPDATE_MAX_TRIES) {
            plan->why = "nouvelle version jamais confirmee : retour arriere";
        } else if (plan->s.build < c.min_build) {
            plan->why = "version plus ancienne que la derniere confirmee : retour arriere";
        } else {
            c.tries++;
            if (upd_ctl_store(env->blk, &c) != E_OK) {
                plan->why = "etat illisible ou disque en erreur : on garde la version du CD";
                return UPB_CONTINUE;
            }
            plan->action = UPB_RELAUNCH;
            plan->slot = c.active;
            plan->why = c.trial ? "version a l'essai" : "version installee";
            return UPB_RELAUNCH;
        }
        if (upd_rollback(env->blk, &c, env->pubkey, env->running_edition, NULL) != E_OK) {
            plan->why = "retour arriere impossible (disque) : on garde la version du CD";
            return UPB_CONTINUE;
        }
        plan->rolled_back = 1;
    }
    plan->why = "etat incoherent : on garde la version du CD";
    return UPB_CONTINUE;
}

/* ------------------------------------------------------------------ relance */

#define META_BYTES  8192u
#define OFF_ARGS    512u
#define OFF_MBI     576u
#define OFF_MOD     704u
#define OFF_NAME    720u
#define OFF_MMAP    1024u
#define MMAP_MAX    4096u
#define PBI_HEAD    32u
#define KERNEL_MIN_LOAD 0x100000u
#define KERNEL_MAX_MEM  (8u << 20)

static u32 align_up(u32 v, u32 a) { return (v + a - 1) & ~(a - 1); }

static u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

/* Les deux plages [a, a+n) et [b, b+m) se chevauchent-elles ? (en 64 bits pour eviter tout debordement) */
static int overlap(u32 a, u32 an, u32 b, u32 bn)
{
    u64 a0 = a, a1 = (u64)a + an, b0 = b, b1 = (u64)b + bn;

    return a0 < b1 && b0 < a1;
}

/* Une plage entiere est-elle dans une zone "disponible" de la carte memoire ? */
static int in_available(const u8 *mmap, u32 len, u32 start, u32 size)
{
    u32 off = 0;

    while (off + sizeof(struct multiboot_mmap_entry) <= len) {
        const struct multiboot_mmap_entry *e = (const struct multiboot_mmap_entry *)(mmap + off);
        u64 a = ((u64)e->addr_hi << 32) | e->addr_lo, l = ((u64)e->len_hi << 32) | e->len_lo;

        if (e->type == MB_MMAP_AVAILABLE && (u64)start >= a && (u64)start + size <= a + l)
            return 1;
        off += e->size + 4;
        if (e->size < 20)
            break;
    }
    return 0;
}

static int read_region(const struct upd_blk *b, int slot, u32 off, u32 bytes, u32 dst)
{
    u32 sectors = (bytes + UPD_SECTOR - 1) / UPD_SECTOR, done = 0;

    while (done < sectors) {
        u32 n = sectors - done > 128 ? 128 : sectors - done;

        if (upd_slot_read(b, slot, off + done, n, (u8 *)PHYS(dst) + done * UPD_SECTOR) != E_OK)
            return -1;
        done += n;
    }
    return 0;
}

int upd_relaunch(const struct upd_blk *b, const struct upd_boot_plan *plan, u32 mbi_phys, u32 old_kernel_end,
                 const void *stub, u32 stub_len, upd_jump_fn jump)
{
    const struct multiboot_info *mbi = (const struct multiboot_info *)PHYS(mbi_phys);
    const u8 *mmap;
    u32 mmap_len, ksz = plan->s.kernel_bytes, isz = plan->s.initrd_bytes;
    u32 kpad = align_up(ksz, 4096), ipad = align_up(isz, 4096), need = META_BYTES + kpad + ipad;
    u32 floor, best = 0, S, kaddr, iaddr;
    u8 digest[32];
    const u8 *img;
    u32 load, entry, filesz, memsz, edition;
    struct relaunch_args *a;

    upd_last_failure = "";
    if (!(mbi->flags & MB_FLAG_MMAP) || mbi->mmap_length == 0 || mbi->mmap_length > MMAP_MAX || stub_len > OFF_ARGS) {
        upd_last_failure = "carte memoire absente ou trop grande";
        return UPB_E_MEM;
    }
    mmap = (const u8 *)PHYS(mbi->mmap_addr);
    mmap_len = mbi->mmap_length;

    /* le noyau lance sera copie a son adresse de chargement : on lit d'abord son en-tete pour connaitre sa taille */
    {
        static u8 head[UPD_SECTOR];

        if (upd_slot_read(b, plan->slot, UPD_KERNEL_OFF, 1, head) != E_OK) {
            upd_last_failure = "lecture du disque impossible";
            return UPB_E_DISK;
        }
        if (head[0] != 'P' || head[1] != 'B' || head[2] != 'I' || head[3] != '1' || rd32(head + 4) != 1) {
            upd_last_failure = "image du noyau invalide";
            return UPB_E_IMAGE;
        }
        load = rd32(head + 8); entry = rd32(head + 12); filesz = rd32(head + 16); memsz = rd32(head + 20);
        edition = rd32(head + 24);
    }
    if (filesz + PBI_HEAD != ksz || (filesz & 3) || load < KERNEL_MIN_LOAD || (load & 0xFFF) || memsz < filesz ||
        memsz > KERNEL_MAX_MEM || entry < load || entry >= load + filesz ||
        edition == 0 || edition > 3) {
        upd_last_failure = "image du noyau invalide (en-tete)";
        return UPB_E_IMAGE;
    }
    if (!in_available(mmap, mmap_len, load, memsz)) {
        upd_last_failure = "l'adresse de chargement n'est pas dans la memoire disponible";
        return UPB_E_IMAGE;
    }

    /* zone de preparation : la plus haute possible, hors du noyau actuel et du noyau lance */
    floor = align_up((old_kernel_end > load + memsz ? old_kernel_end : load + memsz) + 0x10000u, 4096);
    for (u32 o = 0; o + sizeof(struct multiboot_mmap_entry) <= mmap_len;) {
        const struct multiboot_mmap_entry *e = (const struct multiboot_mmap_entry *)(mmap + o);
        u64 a0 = ((u64)e->addr_hi << 32) | e->addr_lo, l = ((u64)e->len_hi << 32) | e->len_lo, end = a0 + l;

        if (e->type == MB_MMAP_AVAILABLE && a0 < 0xFFFFF000ull) {
            u32 top, start;

            if (end > 0xFFFFF000ull)
                end = 0xFFFFF000ull;
            top = (u32)end & ~0xFFFu;
            if (top >= need) {
                start = top - need;
                if (start >= floor && start >= (u32)((a0 + 4095) & ~4095ull) && start > best)
                    best = start;
            }
        }
        if (e->size < 20)
            break;
        o += e->size + 4;
    }
    S = best;
    if (!S) {
        upd_last_failure = "pas assez de memoire pour preparer la mise a jour";
        return UPB_E_MEM;
    }
    /* ne pas ecraser ce que GRUB a donne : infos, carte memoire, modules (copies plus bas) */
    if (overlap(S, need, mbi_phys, 128) || overlap(S, need, mbi->mmap_addr, mmap_len) ||
        ((mbi->flags & MB_FLAG_MODS) && mbi->mods_count && overlap(S, need, mbi->mods_addr, mbi->mods_count * 16u))) {
        upd_last_failure = "la zone de preparation chevauche les informations de GRUB";
        return UPB_E_MEM;
    }

    kaddr = S + META_BYTES;
    iaddr = kaddr + kpad;
    memset(PHYS(S), 0, META_BYTES);
    if (read_region(b, plan->slot, UPD_KERNEL_OFF, ksz, kaddr) != 0 ||
        read_region(b, plan->slot, UPD_INITRD_OFF, isz, iaddr) != 0) {
        upd_last_failure = "lecture du disque impossible";
        return UPB_E_DISK;
    }
    memset((u8 *)PHYS(kaddr) + ksz, 0, kpad - ksz);
    memset((u8 *)PHYS(iaddr) + isz, 0, ipad - isz);

    sha256(digest, PHYS(kaddr), ksz);
    if (crypto_memeq(digest, plan->m.khash, 32) != 1) {
        upd_last_failure = "empreinte du noyau incorrecte";
        return UPB_E_HASH;
    }
    sha256(digest, PHYS(iaddr), isz);
    if (crypto_memeq(digest, plan->m.ihash, 32) != 1) {
        upd_last_failure = "empreinte de l'initrd incorrecte";
        return UPB_E_HASH;
    }
    img = (const u8 *)PHYS(kaddr);
    if (rd32(img + 8) != load || rd32(img + 12) != entry || rd32(img + 16) != filesz || rd32(img + 20) != memsz) {
        upd_last_failure = "en-tete du noyau modifie pendant la lecture";
        return UPB_E_IMAGE;
    }

    /* informations Multiboot propres pour le noyau lance */
    {
        u32 *m = (u32 *)PHYS(S + OFF_MBI);                 /* alignement : S est aligne sur 4 Kio */
        u32 flags = (mbi->flags & MB_FLAG_MEM) | MB_FLAG_MODS | MB_FLAG_MMAP | (1u << 9);
        int fb = (mbi->flags & (1u << 12)) != 0;               /* infos d'ecran (perf64 : le noyau lance les reprend) */

        m[0] = flags;
        m[1] = mbi->mem_lower;
        m[2] = mbi->mem_upper;
        m[5] = 1;                                           /* mods_count */
        m[6] = S + OFF_MOD;                                 /* mods_addr */
        m[11] = mmap_len;
        m[12] = S + OFF_MMAP;
        m[16] = S + OFF_NAME;                               /* boot_loader_name */
        if (fb) {
            m[0] |= 1u << 12;
            memcpy((u8 *)m + 88, (const u8 *)mbi + 88, 28);     /* framebuffer_addr .. color_info */
        }
        memcpy(PHYS(S + OFF_MMAP), mmap, mmap_len);
        {
            u32 *mod = (u32 *)PHYS(S + OFF_MOD);

            mod[0] = iaddr;
            mod[1] = iaddr + isz;
        }
        memcpy(PHYS(S + OFF_NAME), UPD_RELAUNCH_NAME, sizeof UPD_RELAUNCH_NAME);
    }

    a = (struct relaunch_args *)PHYS(S + OFF_ARGS);
    a->src = kaddr + PBI_HEAD;
    a->dst = load;
    a->copy_len = filesz;
    a->zero_dst = load + filesz;
    a->zero_len = memsz - filesz;
    a->entry = entry;
    a->mbi = S + OFF_MBI;
    a->magic = 0x2BADB002u;
    a->sp = S + META_BYTES - 16;
    if (stub_len)
        memcpy(PHYS(S), stub, stub_len);
    jump(PHYS(S), (const struct relaunch_args *)a);                  /* dans le noyau : ne revient pas */
    return UPB_OK;
}
