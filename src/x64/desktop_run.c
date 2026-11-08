/*
 * PhoenOS - x64/desktop_run.c : le bureau branche sur l'ecran, la souris et le clavier (la logique est dans desktop.c).
 */
#include "desktop.h"
#include "gfxsys.h"
#include "mouse.h"
#include "keyboard.h"
#include "pit.h"
#include "pmm.h"
#include "rtc.h"
#include "kheap.h"
#include "kprintf.h"

void desktop_run(void)
{
    static struct desk d;
    u32 w, h, pages;
    u32 mem;
    u32 *canvas;
    struct mouse_state m;
    u32 last_ev = ~0u, last_sec = ~0u;
    int dirty = 1;

    if (gfx_info(&w, &h) != E_OK) {
        kprintf("Pas d'ecran graphique.\n");
        return;
    }
    pages = (w * h * 4 + 4095) / 4096;
    mem = pmm_alloc_contiguous(pages);
    canvas = kmalloc(PAINT_W * PAINT_H * 4);
    if (!mem || !canvas) {
        if (mem)
            for (u32 i = 0; i < pages; i++)
                pmm_free_frame(mem + i * 4096);
        kfree(canvas);
        kprintf("Memoire insuffisante pour le bureau.\n");
        return;
    }
    if (gfx_claim(0xFFFFFFFFu, 1) != E_OK) {
        kprintf("L'ecran est utilise par un programme.\n");
        goto done;
    }
    desk_init(&d, (u32 *)(usize)mem, (i32)w, (i32)h, canvas);
    mouse_set_bounds((i32)w, (i32)h);
    while (!d.quit) {
        struct rtc_time t;
        int k = keyboard_try_getc();

        if (k >= 0) {
            desk_key(&d, k);
            dirty = 1;
        }
        mouse_get(&m);
        if (m.events != last_ev) {
            last_ev = m.events;
            desk_mouse(&d, m.x, m.y, m.buttons);
            dirty = 1;
        }
        if (pit_uptime_ms() / 1000 != last_sec) {
            last_sec = pit_uptime_ms() / 1000;
            if (rtc_read(&t) == E_OK) {
                d.hh = t.hour; d.mm = t.min; d.ss = t.sec;
            }
            d.up_s = last_sec;
            d.free_kib = pmm_free_frames() * 4;
            dirty = 1;
        }
        if (dirty) {
            desk_draw(&d);
            gfx_dev_hook->present(d.scr.px, w, 0, 0, w, h);
            dirty = 0;
        }
        pit_sleep_ms(10);
    }
    gfx_claim(0xFFFFFFFFu, 0);
done:
    for (u32 i = 0; i < pages; i++)
        pmm_free_frame(mem + i * 4096);
    kfree(canvas);
}
