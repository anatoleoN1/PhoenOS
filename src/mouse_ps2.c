/*
 * PhoenOS - mouse_ps2.c : controleur 8042 et IRQ 12 pour la souris PS/2 (decodage : mouse.c).
 * Sequence : activer le port auxiliaire, autoriser son interruption, parler a la souris (0xD4) : valeurs par defaut,
 * (essai molette : debit 200, 100, 80 puis lecture de l'identite : 3 = IntelliMouse), debit 100, envoi des paquets.
 */
#include "mouse.h"
#include "io.h"
#include "isr.h"
#include "dlog.h"

#define P_DATA   0x60
#define P_STATUS 0x64
#define P_CMD    0x64


static int wait_in_empty(void)
{
    for (int i = 0; i < 100000; i++)
        if (!(inb(P_STATUS) & 2))
            return 1;
    return 0;
}

static int wait_out_full(void)
{
    for (int i = 0; i < 100000; i++)
        if (inb(P_STATUS) & 1)
            return 1;
    return 0;
}

static int send_mouse(u8 b)                     /* 0xFA = accuse de reception attendu */
{
    if (!wait_in_empty())
        return 0;
    outb(P_CMD, 0xD4);
    if (!wait_in_empty())
        return 0;
    outb(P_DATA, b);
    if (!wait_out_full())
        return 0;
    return inb(P_DATA) == 0xFA;
}

static void mouse_irq(struct regs *r)
{
    (void)r;
    for (int n = 0; n < 16 && (inb(P_STATUS) & 1); n++) {
        u8 st = inb(P_STATUS), b = inb(P_DATA);

        if (st & 0x20)                          /* bit 5 : octet venant de la souris (et non du clavier) */
            mouse_feed(b);
    }
}

err_t mouse_init(void)
{
    u8 cfg;
    int wheel = 0;

    mouse_set_present(0);
    if (!wait_in_empty())
        return E_NOENT;
    outb(P_CMD, 0xA8);                          /* active le port auxiliaire */
    if (!wait_in_empty())
        return E_NOENT;
    outb(P_CMD, 0x20);                          /* lit l'octet de configuration */
    if (!wait_out_full())
        return E_NOENT;
    cfg = inb(P_DATA);
    cfg |= 2;                                   /* interruption du port auxiliaire */
    cfg &= (u8)~0x20;                           /* horloge du port auxiliaire active */
    if (!wait_in_empty())
        return E_NOENT;
    outb(P_CMD, 0x60);
    if (!wait_in_empty())
        return E_NOENT;
    outb(P_DATA, cfg);

    if (!send_mouse(0xF6))                      /* valeurs par defaut */
        return E_NOENT;
    if (send_mouse(0xF3) && send_mouse(200) && send_mouse(0xF3) && send_mouse(100) && send_mouse(0xF3) && send_mouse(80) &&
        send_mouse(0xF2) && wait_out_full()) {   /* identite */
        u8 id = inb(P_DATA);

        wheel = id == 3;
    }
    (void)send_mouse(0xF3); (void)send_mouse(100);
    if (!send_mouse(0xF4))                      /* envoi des paquets */
        return E_NOENT;
    mouse_reset_decoder(wheel);
    mouse_set_present(1);
    DINFO("mouse", "souris PS/2 detectee (%s)", wheel ? "avec molette" : "3 boutons");
    return irq_install_handler(12, mouse_irq);
}
