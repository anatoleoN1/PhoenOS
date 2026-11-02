/* PhoenOS perf64 - x64/mb2.c : informations Multiboot2 -> structure Multiboot 1 (voir bootinfo.h). */
#include "bootinfo.h"
#include "multiboot.h"
#include "string.h"

struct boot_fb boot_fb;

#define MB2_MAX_MODS  16
#define MB2_MAX_MMAP  256

static u8 mb1_raw[128] __attribute__((aligned(16)));
static struct multiboot_module mods[MB2_MAX_MODS];
static struct multiboot_mmap_entry mmap_out[MB2_MAX_MMAP];
static const char loader_name[] = "GRUB (Multiboot2)";

struct mb2_tag { u32 type, size; };

static u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static u64 rd64(const u8 *p) { return (u64)rd32(p) | ((u64)rd32(p + 4) << 32); }
static void wr32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

struct multiboot_info *mb2_convert_into(const void *mb2, u8 *raw)
{
    struct multiboot_info *mb1 = (struct multiboot_info *)raw;
    const u8 *p = mb2;
    u32 total, off = 8, nmods = 0, nmmap = 0;
    u32 mem_lower = 0, mem_upper = 0;

    memset(raw, 0, 128);
    memset(&boot_fb, 0, sizeof boot_fb);
    if (!p)
        return mb1;
    total = rd32(p);
    if (total < 16 || total > (1u << 20))
        return mb1;                                     /* illisible */

    while (off + 8 <= total) {
        const u8 *t = p + off;
        u32 type = rd32(t), size = rd32(t + 4);

        if (type == 0 || size < 8 || off + size > total)
            break;
        switch (type) {
        case 3:                                         /* module : debut, fin, texte */
            if (size >= 16 && nmods < MB2_MAX_MODS) {
                mods[nmods].mod_start = rd32(t + 8);
                mods[nmods].mod_end = rd32(t + 12);
                mods[nmods].string = 0;
                mods[nmods].reserved = 0;
                nmods++;
            }
            break;
        case 4:                                         /* memoire de base */
            if (size >= 16) {
                mem_lower = rd32(t + 8);
                mem_upper = rd32(t + 12);
            }
            break;
        case 6: {                                       /* carte memoire */
            u32 esz = size >= 16 ? rd32(t + 8) : 0;

            if (esz < 24)
                break;
            for (u32 o = 16; o + 24 <= size && nmmap < MB2_MAX_MMAP; o += esz) {
                u64 base = rd64(t + o), len = rd64(t + o + 8);
                u32 ty = rd32(t + o + 16);

                mmap_out[nmmap].size = 20;
                mmap_out[nmmap].addr_lo = (u32)base;
                mmap_out[nmmap].addr_hi = (u32)(base >> 32);
                mmap_out[nmmap].len_lo = (u32)len;
                mmap_out[nmmap].len_hi = (u32)(len >> 32);
                mmap_out[nmmap].type = ty;
                nmmap++;
            }
            break;
        }
        case 8:                                         /* ecran */
            if (size >= 31) {
                boot_fb.addr = rd64(t + 8);
                boot_fb.pitch = rd32(t + 16);
                boot_fb.width = rd32(t + 20);
                boot_fb.height = rd32(t + 24);
                boot_fb.bpp = t[28];
                boot_fb.type = (u8)(t[29] == 1 ? 1 : t[29] == 2 ? 2 : 0);
                if (t[29] == 1 && size >= 38) {
                    boot_fb.red_pos = t[32]; boot_fb.red_size = t[33];
                    boot_fb.green_pos = t[34]; boot_fb.green_size = t[35];
                    boot_fb.blue_pos = t[36]; boot_fb.blue_size = t[37];
                }
            }
            break;
        default:
            break;
        }
        off += (size + 7) & ~7u;
    }

    {   /* flags Multiboot 1 : memoire (1), modules (8), carte (64), nom du bootloader (512) */
        u32 flags = 0;

        if (mem_upper || mem_lower) {
            flags |= MB_FLAG_MEM;
            mb1->mem_lower = mem_lower;
            mb1->mem_upper = mem_upper;
        }
        if (nmods) {
            flags |= MB_FLAG_MODS;
            mb1->mods_count = nmods;
            mb1->mods_addr = (u32)(usize)mods;
        }
        if (nmmap) {
            flags |= MB_FLAG_MMAP;
            mb1->mmap_length = nmmap * (u32)sizeof(struct multiboot_mmap_entry);
            mb1->mmap_addr = (u32)(usize)mmap_out;
        }
        flags |= 1u << 9;
        wr32(raw + BOOT_NAME_OFFSET, (u32)(usize)loader_name);
        if (boot_fb.type) {                              /* champs d'ecran de Multiboot 1 (flag 12), repris par mb1_adopt apres une relance */
            flags |= 1u << 12;
            wr32(raw + 88, (u32)boot_fb.addr); wr32(raw + 92, (u32)(boot_fb.addr >> 32));
            wr32(raw + 96, boot_fb.pitch); wr32(raw + 100, boot_fb.width); wr32(raw + 104, boot_fb.height);
            raw[108] = boot_fb.bpp; raw[109] = boot_fb.type;
            raw[110] = boot_fb.red_pos; raw[111] = boot_fb.red_size; raw[112] = boot_fb.green_pos;
            raw[113] = boot_fb.green_size; raw[114] = boot_fb.blue_pos; raw[115] = boot_fb.blue_size;
        }
        mb1->flags = flags;
    }
    return mb1;
}

/* Noyau relance par l'etape 0 des mises a jour : les infos sont deja au format Multiboot 1 (updboot.c) ; on reprend l'ecran. */
struct multiboot_info *mb1_adopt(usize mb1)
{
    const u8 *raw = (const u8 *)mb1;
    const struct multiboot_info *m = (const struct multiboot_info *)raw;

    memset(&boot_fb, 0, sizeof boot_fb);
    if (m->flags & (1u << 12)) {
        boot_fb.addr = rd64(raw + 88);
        boot_fb.pitch = rd32(raw + 96); boot_fb.width = rd32(raw + 100); boot_fb.height = rd32(raw + 104);
        boot_fb.bpp = raw[108]; boot_fb.type = raw[109];
        boot_fb.red_pos = raw[110]; boot_fb.red_size = raw[111]; boot_fb.green_pos = raw[112];
        boot_fb.green_size = raw[113]; boot_fb.blue_pos = raw[114]; boot_fb.blue_size = raw[115];
    }
    return (struct multiboot_info *)raw;
}

struct multiboot_info *mb2_convert(usize mb2)
{
    return mb2_convert_into((const void *)mb2, mb1_raw);
}
