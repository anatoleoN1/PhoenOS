/*
 * PhoenOS - mouse.c : decodeur de paquets de souris PS/2 (voir mouse.h). Aucun acces materiel ici (voir mouse_ps2.c).
 *
 * Paquet : octet 0 = YO XO YS XS 1 M R L (bit 3 toujours a 1 : sert a se resynchroniser), octet 1 = dX, octet 2 = dY
 * (negatif = bas, signe etendu par XS / YS), octet 3 (molette) = -8..7 sur 4 bits.
 */
#include "mouse.h"

static struct mouse_state st;
static i32  bw = 640, bh = 480;
static u8   pkt[4];
static int  pkt_len, wheel_mode;

void mouse_reset_decoder(int wheel)
{
    pkt_len = 0;
    wheel_mode = wheel != 0;
}

static int present;

int mouse_present(void)
{
    return present;
}

void mouse_set_present(int on)
{
    present = on;
}

int mouse_has_wheel(void)
{
    return wheel_mode;
}

static i32 clamp(i32 v, i32 lo, i32 hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void mouse_set_bounds(i32 w, i32 h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (st.x == 0 && st.y == 0 && st.events == 0) {
        st.x = w / 2;                           /* premiere fois : au milieu de l'ecran */
        st.y = h / 2;
    }
    bw = w; bh = h;
    st.x = clamp(st.x, 0, bw - 1);
    st.y = clamp(st.y, 0, bh - 1);
}

void mouse_get(struct mouse_state *out)
{
    *out = st;
    st.wheel = 0;
}

int mouse_feed(u8 b)
{
    int need = wheel_mode ? 4 : 3;

    if (pkt_len == 0 && !(b & 8))
        return 0;                               /* pas un debut de paquet : on attend le suivant */
    pkt[pkt_len++] = b;
    if (pkt_len < need)
        return 0;
    pkt_len = 0;

    if (!(pkt[0] & 0xC0)) {                     /* debordement (bits 6 et 7) : paquet ignore */
        i32 dx = pkt[1], dy = pkt[2];

        if (pkt[0] & 0x10) dx -= 256;
        if (pkt[0] & 0x20) dy -= 256;
        st.x = clamp(st.x + dx, 0, bw - 1);
        st.y = clamp(st.y - dy, 0, bh - 1);     /* l'axe Y de la souris monte, celui de l'ecran descend */
    }
    st.buttons = (u32)(pkt[0] & 7);
    if (wheel_mode) {
        i32 w = pkt[3] & 15;

        if (w & 8) w -= 16;
        st.wheel += w;
    }
    st.events++;
    return 1;
}
