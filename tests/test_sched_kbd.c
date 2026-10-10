/* Le shell attend une touche pendant qu'un programme "dort" : la touche doit etre servie sans attendre la fin du programme. */
#include "testlib.h"
#include "sched.h"
#include "kheap.h"
#include "pmm.h"
#include "multiboot.h"
#include "isr.h"
#include "keyboard.h"

u8 test_scancode;
char kernel_start[4096] __attribute__((aligned(4096)));
char kernel_end[1];
void test_panic(const char *fmt, ...) { out("  PANIQUE : "); out(fmt); out("\n"); exit_now(2); }
static char fake_ram[4u << 20] __attribute__((aligned(4096)));
err_t irq_install_handler(u8 irq, irq_handler_t h) { (void)irq; (void)h; return E_OK; }

static u32 ticks, idles;
u32 pit_ticks(void) { return ticks; }
u32 pit_hz(void) { return 100; }

static int serial_q = -1;
int serial_try_getc(void) { int c = serial_q; serial_q = -1; return c; }
static void idle(void) { idles++; ticks++; if (idles == 15) serial_q = 'x'; }

static int key_ready(void *p) { (void)p; return keyboard_peek() >= 0; }
static int wait_key(void) { sched_block(key_ready, NULL); return sched_current()->killed; }

static int printed;
static void ticker(void *a)
{
    (void)a;
    for (int i = 0; i < 5; i++) { printed++; sched_sleep_ms(400); }
}

int test_main(void)
{
    static struct { struct multiboot_info mbi; struct multiboot_mmap_entry e[1]; } M;
    struct task *t;
    int c;

    M.mbi.flags = MB_FLAG_MMAP;
    M.mbi.mmap_addr = (u32)M.e;
    M.mbi.mmap_length = sizeof M.e;
    M.e[0] = (struct multiboot_mmap_entry){ 20, (u32)fake_ram, 0, sizeof fake_ram, 0, MB_MMAP_AVAILABLE };
    pmm_init(&M.mbi);
    kheap_init();
    sched_idle_hook = idle;
    sched_init();
    keyboard_wait_hook = wait_key;

    CHECK(sched_create("ticker", ticker, NULL, NULL, NULL, &t) == E_OK);
    c = keyboard_getc();                                        /* le "shell" attend une touche ; le ticker dort en boucle */
    CHECK(c == 'x');
    CHECK(ticks < 30 && printed >= 1 && t->state != T_ZOMBIE);   /* servie apres ~15 ticks, bien avant les 200 ticks du ticker */
    sched_wait(t);
    sched_reap(t);
    out("  touche servie pendant qu'un programme dort : verifie\n");
    return 0;
}
