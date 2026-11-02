/* Conversion Multiboot2 -> Multiboot 1 (x64/mb2.c) : memoire, carte, module, ecran ; entrees tronquees ignorees. */
#include "testlib.h"
#include "multiboot.h"
#include "bootinfo.h"

u8 test_scancode;

static u8 blob[512] __attribute__((aligned(8)));
static u32 n;

static void w32(u32 v) { blob[n++] = (u8)v; blob[n++] = (u8)(v >> 8); blob[n++] = (u8)(v >> 16); blob[n++] = (u8)(v >> 24); }
static void w64(u64 v) { w32((u32)v); w32((u32)(v >> 32)); }
static void pad(void) { while (n & 7) blob[n++] = 0; }

int test_main(void)
{
    u8 raw[128] __attribute__((aligned(16)));
    struct multiboot_info *mb;
    struct multiboot_mmap_entry *e;
    struct multiboot_module *m;

    n = 8;                                       /* total_size + reserved : remplis a la fin */
    w32(4); w32(16); w32(639); w32(129024); pad();                                   /* memoire de base */
    w32(6); w32(16 + 2 * 24); w32(24); w32(0);                                        /* carte : 2 entrees */
    w64(0); w64(0x9F000); w32(1); w32(0);
    w64(0x100000); w64(0x7F00000); w32(1); w32(0); pad();
    w32(3); w32(16 + 2); w32(0x2000000); w32(0x2004000); blob[n++] = 'i'; blob[n++] = 0; blob[n++] = 0; blob[n++] = 0; blob[n++] = 0; blob[n++] = 0; blob[n++] = 0; blob[n++] = 0; pad();
    w32(8); w32(38); w64(0xFD000000ull); w32(5120); w32(1280); w32(800); blob[n++] = 32; blob[n++] = 1; blob[n++] = 0; blob[n++] = 0;
    blob[n++] = 16; blob[n++] = 8; blob[n++] = 8; blob[n++] = 8; blob[n++] = 0; blob[n++] = 8; pad();
    w32(0); w32(8);
    blob[0] = (u8)n; blob[1] = (u8)(n >> 8); blob[2] = blob[3] = 0;

    mb = mb2_convert_into(blob, raw);
    CHECK((mb->flags & (MB_FLAG_MEM | MB_FLAG_MODS | MB_FLAG_MMAP)) == (MB_FLAG_MEM | MB_FLAG_MODS | MB_FLAG_MMAP));
    CHECK(mb->mem_lower == 639 && mb->mem_upper == 129024);
    CHECK(mb->mmap_length == 2 * sizeof(struct multiboot_mmap_entry));
    e = (struct multiboot_mmap_entry *)(usize)mb->mmap_addr;
    CHECK(e[0].size == 20 && e[0].addr_lo == 0 && e[0].len_lo == 0x9F000 && e[0].type == 1);
    CHECK(e[1].addr_lo == 0x100000 && e[1].len_lo == 0x7F00000 && e[1].addr_hi == 0 && e[1].len_hi == 0);
    CHECK(mb->mods_count == 1);
    m = (struct multiboot_module *)(usize)mb->mods_addr;
    CHECK(m->mod_start == 0x2000000 && m->mod_end == 0x2004000);
    CHECK(boot_fb.type == 1 && boot_fb.addr == 0xFD000000ull && boot_fb.pitch == 5120 && boot_fb.width == 1280 &&
          boot_fb.height == 800 && boot_fb.bpp == 32);
    CHECK(boot_fb.red_pos == 16 && boot_fb.red_size == 8 && boot_fb.green_pos == 8 && boot_fb.blue_pos == 0);

    /* aller-retour : les champs d'ecran ecrits par la conversion sont repris par mb1_adopt (noyau relance par une mise a jour) */
    mb2_convert_into(blob, raw);
    {
        struct boot_fb saved = boot_fb;

        CHECK(((struct multiboot_info *)raw)->flags & (1u << 12));
        memset(&boot_fb, 0, sizeof boot_fb);
        CHECK(mb1_adopt((usize)raw) == (struct multiboot_info *)raw);
        CHECK(!memcmp(&boot_fb, &saved, sizeof boot_fb) && boot_fb.addr == 0xFD000000ull && boot_fb.type == 1);
        ((struct multiboot_info *)raw)->flags &= ~(1u << 12);
        mb1_adopt((usize)raw);
        CHECK(boot_fb.type == 0 && boot_fb.addr == 0);
    }

    /* infos illisibles : structure vide, sans carte memoire */
    mb = mb2_convert_into(0, raw);
    CHECK(!(mb->flags & (MB_FLAG_MEM | MB_FLAG_MMAP | MB_FLAG_MODS)));
    blob[0] = 4; blob[1] = 0;
    mb = mb2_convert_into(blob, raw);
    CHECK(!(mb->flags & MB_FLAG_MMAP) && boot_fb.type == 0);
    return 0;
}
