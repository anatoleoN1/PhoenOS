/* PhoenOS - updman.c : lecture stricte et verification du manifeste de mise a jour. Voir updman.h. */
#include "updman.h"
#include "crypto.h"
#include "string.h"

static int is_digit(char c) { return c >= '0' && c <= '9'; }

/* Nombre decimal sans zero superflu, au plus `maxd` chiffres ; avance *p. -1 si invalide. */
static i32 num(const char **p, int maxd)
{
    const char *s = *p;
    u32 v = 0;
    int n = 0;

    if (!is_digit(*s))
        return -1;
    if (*s == '0' && is_digit(s[1]))
        return -1;
    while (is_digit(*s) && n < maxd) {
        v = v * 10 + (u32)(*s - '0');
        s++; n++;
    }
    if (is_digit(*s))
        return -1;
    *p = s;
    return (i32)v;
}

u32 upd_build_of(const char *version)
{
    const char *p = version;
    i32 x, y, z;

    x = num(&p, 2); if (x < 0 || *p++ != '.') return 0;
    y = num(&p, 3); if (y < 0 || *p++ != '.') return 0;
    z = num(&p, 3); if (z < 0 || *p) return 0;
    return (u32)x * 1000000u + (u32)y * 1000u + (u32)z;
}

int upd_edition_known(const char *e)
{
    return !strcmp(e, "perf32") || !strcmp(e, "perf64") || !strcmp(e, "pui64");
}

/* Prend la ligne suivante de [*p, end) : la termine par un NUL a la place du '\n'. NULL s'il n'y en a plus. */
static char *next_line(char **p, char *end)
{
    char *s = *p, *q = s;

    while (q < end && *q != '\n')
        q++;
    if (q >= end)
        return NULL;
    *q = 0;
    *p = q + 1;
    return s;
}

static int printable(const char *s)
{
    for (; *s; s++)
        if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7e)
            return 0;
    return 1;
}

static int starts_with(char **s, const char *key)      /* "key " puis avance */
{
    u32 n = (u32)strlen(key);

    for (u32 i = 0; i < n; i++)
        if ((*s)[i] != key[i])
            return 0;
    if ((*s)[n] != ' ')
        return 0;
    *s += n + 1;
    return 1;
}

static int name_ok(const char *s, u32 max)
{
    u32 n = (u32)strlen(s);

    if (n == 0 || n > max)
        return 0;
    for (u32 i = 0; i < n; i++) {
        char c = s[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || is_digit(c) || c == '.' || c == '-' || c == '_'))
            return 0;
    }
    return s[0] != '.';
}

static int hex64(const char *s, u8 out[32])
{
    for (int i = 0; i < 32; i++) {
        u8 v = 0;

        for (int k = 0; k < 2; k++) {
            char c = s[i * 2 + k];

            v = (u8)(v << 4);
            if (c >= '0' && c <= '9') v |= (u8)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (u8)(c - 'a' + 10);
            else return 0;
        }
        out[i] = v;
    }
    return s[64] == 0;
}

/* "<nom> <taille> <hash>" */
static int file_line(char *s, u32 maxsize, char *name, u32 namecap, u32 *size, u8 hash[32])
{
    char *sp1 = strchr(s, ' '), *sp2;
    const char *q;
    i32 sz;

    if (!sp1)
        return UPD_M_FILE;
    *sp1 = 0;
    if (!name_ok(s, namecap - 1))
        return UPD_M_FILE;
    memcpy(name, s, strlen(s) + 1);
    s = sp1 + 1;
    sp2 = strchr(s, ' ');
    if (!sp2)
        return UPD_M_FILE;
    *sp2 = 0;
    q = s;
    sz = num(&q, 9);
    if (sz <= 0 || *q || (u32)sz > maxsize)
        return UPD_M_FILE;
    *size = (u32)sz;
    return hex64(sp2 + 1, hash) ? UPD_M_OK : UPD_M_HASH;
}

int upd_man_parse(const char *text, u32 len, struct upd_manifest *m)
{
    static char buf[UPD_MAN_MAX + 1];
    char *p = buf, *end, *l;
    const char *q;
    int r;

    if (len == 0 || len > UPD_MAN_MAX)
        return UPD_M_SIZE;
    memcpy(buf, text, len);
    buf[len] = 0;
    if ((u32)strlen(buf) != len)                         /* NUL cache */
        return UPD_M_FORMAT;
    end = buf + len;
    memset(m, 0, sizeof *m);

    l = next_line(&p, end);
    if (!l || strcmp(l, "PHOENOS-MANIFEST 1") != 0)
        return UPD_M_FORMAT;

    l = next_line(&p, end);
    if (!l || !printable(l) || !starts_with(&l, "edition"))
        return UPD_M_FORMAT;
    if (strlen(l) >= sizeof m->edition || !upd_edition_known(l))
        return UPD_M_EDITION;
    memcpy(m->edition, l, strlen(l) + 1);

    l = next_line(&p, end);
    if (!l || !printable(l) || !starts_with(&l, "version"))
        return UPD_M_FORMAT;
    if (strlen(l) >= sizeof m->version || upd_build_of(l) == 0)
        return UPD_M_VERSION;
    memcpy(m->version, l, strlen(l) + 1);

    l = next_line(&p, end);
    if (!l || !printable(l) || !starts_with(&l, "build"))
        return UPD_M_FORMAT;
    q = l;
    {
        i32 b = num(&q, 9);

        if (b <= 0 || *q || (u32)b != upd_build_of(m->version))
            return UPD_M_BUILD;
        m->build = (u32)b;
    }

    l = next_line(&p, end);
    if (!l || !printable(l) || !starts_with(&l, "kernel"))
        return UPD_M_FORMAT;
    r = file_line(l, UPD_KERNEL_MAX_BYTES, m->kname, sizeof m->kname, &m->ksize, m->khash);
    if (r != UPD_M_OK)
        return r;

    l = next_line(&p, end);
    if (!l || !printable(l) || !starts_with(&l, "initrd"))
        return UPD_M_FORMAT;
    r = file_line(l, UPD_INITRD_MAX_BYTES, m->iname, sizeof m->iname, &m->isize, m->ihash);
    if (r != UPD_M_OK)
        return r;

    if (p < end) {                                       /* ligne facultative : notes */
        l = next_line(&p, end);
        if (!l || !printable(l) || !starts_with(&l, "notes") || strlen(l) >= sizeof m->notes || strlen(l) > 80)
            return UPD_M_FORMAT;
        memcpy(m->notes, l, strlen(l) + 1);
    }
    if (p != end)
        return UPD_M_FORMAT;                             /* rien apres, et la derniere ligne finit par \n */
    return UPD_M_OK;
}

const char *upd_man_strerror(int c)
{
    switch (c) {
    case UPD_M_OK:      return "manifeste correct";
    case UPD_M_SIZE:    return "manifeste vide ou trop long";
    case UPD_M_FORMAT:  return "manifeste mal forme";
    case UPD_M_EDITION: return "edition inconnue dans le manifeste";
    case UPD_M_VERSION: return "numero de version invalide dans le manifeste";
    case UPD_M_BUILD:   return "build incoherent avec la version";
    case UPD_M_FILE:    return "nom ou taille de fichier invalide dans le manifeste";
    case UPD_M_HASH:    return "empreinte SHA-256 invalide dans le manifeste";
    }
    return "erreur de manifeste";
}

int upd_man_verify(const u8 pk[32], const char *text, u32 len, const u8 sig[64])
{
    return ed25519_verify(sig, (const u8 *)text, len, pk) == 0;
}
