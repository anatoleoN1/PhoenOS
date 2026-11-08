/*
 * PhoenOS perf64 - x64/bochs.c : changement de resolution par l'interface Bochs VBE (voir bochs.h).
 * Registres (index sur 0x1CE, valeur sur 0x1CF, 16 bits) : 0 identite (0xB0C0..0xB0C5), 1 largeur, 2 hauteur, 3 bits par pixel,
 * 4 activation (bit 0 : actif, bit 6 : memoire video lineaire, bit 7 : ne pas effacer), 6 largeur virtuelle, 8-9 decalage,
 * 10 memoire video en blocs de 64 Kio (identite >= 0xB0C4).
 */
#include "bochs.h"
#include "bootinfo.h"
#include "fbcon.h"
#include "io.h"
#include "mouse.h"
#include "pci.h"
#include "vga.h"
#include "dlog.h"

#define DISPI_INDEX 0x1CE
#define DISPI_DATA  0x1CF

static const struct bochs_mode table[] = {
    { 800, 600 }, { 1024, 768 }, { 1280, 720 }, { 1280, 800 }, { 1366, 768 }, { 1440, 900 },
    { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }
};

static u16 rd(u16 idx)
{
    outw(DISPI_INDEX, idx);
    return inw(DISPI_DATA);
}

static void wr(u16 idx, u16 v)
{
    outw(DISPI_INDEX, idx);
    outw(DISPI_DATA, v);
}

int bochs_mode_ok(u32 w, u32 h, u32 vram)
{
    if (w < 640 || h < 400 || w > 4096 || h > 2304 || (w & 7))
        return 0;
    return (u64)w * h * 4 <= vram;
}

static int found, checked;
static u32 lfb, vram_bytes;

static void detect(void)
{
    if (checked)
        return;
    checked = 1;
    for (int i = 0; i < pci_count(); i++) {
        const struct pci_dev *d = pci_get(i);
        u32 bar;

        if (d->vendor != 0x1234 || d->device != 0x1111 || pci_bar_mem(d, 0, &bar) != E_OK)
            continue;
        if (rd(0) < 0xB0C0 || rd(0) > 0xB0CF)
            continue;
        lfb = bar;
        vram_bytes = rd(0) >= 0xB0C4 ? (u32)rd(10) * 65536u : 0;
        found = vram_bytes != 0;
        return;
    }
}

int bochs_present(void)
{
    detect();
    return found && boot_fb.type == 1 && boot_fb.addr == lfb;      /* l'ecran courant doit etre bien celui de cette carte */
}

u32 bochs_vram(void)
{
    detect();
    return found ? vram_bytes : 0;
}

int bochs_modes(struct bochs_mode *out, int max)
{
    int n = 0;

    detect();
    if (!found)
        return 0;
    for (usize i = 0; i < sizeof table / sizeof table[0] && n < max; i++)
        if (bochs_mode_ok(table[i].w, table[i].h, vram_bytes))
            out[n++] = table[i];
    return n;
}

err_t bochs_set_mode(u32 w, u32 h)
{
    if (!bochs_present())
        return E_NOSYS;
    if (!bochs_mode_ok(w, h, vram_bytes))
        return E_INVAL;
    wr(4, 0);                                   /* desactive pour changer de mode */
    wr(1, (u16)w);
    wr(2, (u16)h);
    wr(3, 32);
    wr(6, (u16)w);                              /* largeur virtuelle = largeur : une ligne fait w * 4 octets */
    wr(8, 0);
    wr(9, 0);
    wr(4, 0x01 | 0x40);                         /* actif + memoire video lineaire (efface l'ecran) */

    boot_fb.width = w; boot_fb.height = h; boot_fb.pitch = w * 4; boot_fb.bpp = 32;
    boot_fb.red_pos = 16; boot_fb.red_size = 8; boot_fb.green_pos = 8; boot_fb.green_size = 8;
    boot_fb.blue_pos = 0; boot_fb.blue_size = 8;
    if (!fbcon_reinit())
        return E_IO;
    mouse_set_bounds((i32)w, (i32)h);
    vga_refresh();
    DINFO("video", "mode %ux%u (32 bits par pixel, %u octets par ligne)", w, h, w * 4);
    return E_OK;
}
