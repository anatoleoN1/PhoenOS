#ifndef PHOENOS_GFXSYS_H
#define PHOENOS_GFXSYS_H

#include "types.h"
#include "error.h"

/*
 * Ecran graphique offert aux programmes (0.12.0). Un seul programme a la fois "reserve" l'ecran (claim) : la console
 * texte se tait, il envoie des rectangles de pixels 0x00RRGGBB (blit) et lit la souris. Quand il se termine, ou rend
 * l'ecran, la console reprend. Le materiel (framebuffer) est branche par gfx_dev_hook : le noyau x64 le fait au demarrage,
 * les tests branchent un faux ecran.
 */
struct gfx_mode { u16 w, h; };

struct gfx_dev {
    int  (*screen)(u32 *w, u32 *h);                                                  /* taille en pixels, 0 si aucun ecran */
    void (*present)(const u32 *src, u32 stride, u32 x, u32 y, u32 w, u32 h);        /* stride en pixels */
    void (*claim)(int on);                                                           /* 1 : la console texte se tait ; 0 : elle reprend */
    err_t (*set_mode)(u32 w, u32 h);                                                 /* NULL : la resolution ne se change pas a chaud */
    int  (*modes)(struct gfx_mode *out, int max);                                    /* modes proposes (NULL : aucun) */
    void (*desktop)(void);                                                           /* le bureau, rend la main quand on le quitte (NULL : absent) */
};
extern const struct gfx_dev *gfx_dev_hook;

#define GFX_MAX_SCALE 8

/* Rectangle envoye par SYS_GFX_BLIT : pixels source a l'adresse src (stride en pixels), agrandis `scale` fois, poses en (x, y). */
struct gfx_blit_req { u32 x, y, w, h, src, stride, scale; };

/* Etat de la souris tel que le voit un programme (SYS_MOUSE). */
struct gfx_mouse { i32 x, y; u32 buttons; i32 wheel; };

err_t gfx_info(u32 *w, u32 *h);                                  /* E_NOENT : pas d'ecran graphique */
err_t gfx_claim(u32 pid, int on);                                /* E_BUSY : un autre programme l'a deja ; E_INVAL : pas le sien */
/* Octets de source necessaires pour la requete (0 si invalide) : sert au noyau a verifier la memoire du programme. */
u32   gfxsys_blit_span(const struct gfx_blit_req *r);
err_t gfxsys_blit(u32 pid, const struct gfx_blit_req *r);          /* E_INVAL : pas reserve par ce programme / requete invalide */
err_t gfx_mouse(struct gfx_mouse *out);                          /* E_NOENT : pas de souris */
void  gfx_proc_exit(u32 pid);                                    /* rend l'ecran s'il etait a ce programme */
u32   gfx_owner(void);                                           /* pid du programme qui a l'ecran (0 : personne) */

#endif
