/*
 * PhoenOS - cmdline.c
 * Analyse de ligne de commande, conversion de nombres, historique.
 * Aucune dépendance au matériel : testable seul (make test).
 */
#include "cmdline.h"
#include "string.h"

int cmdline_split(char *line, char **argv, int max)
{
    int argc = 0;
    char *p = line;

    for (;;) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;
        if (argc >= max)
            return E_2BIG;

        if (*p == '"') {
            argv[argc++] = ++p;
            while (*p && *p != '"')
                p++;
            if (*p == '\0')
                return E_INVAL;                 /* guillemet non fermé */
            *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t')
                p++;
            if (*p)
                *p++ = '\0';
        }
    }

    argv[argc] = NULL;
    return argc;
}

err_t cmdline_parse_u32(const char *s, u32 *out)
{
    u32 base = 10, value = 0;

    if (!s || !*s)
        return E_INVAL;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
        if (!*s)
            return E_INVAL;
    }

    for (; *s; s++) {
        u32 digit;

        if (*s >= '0' && *s <= '9')
            digit = (u32)(*s - '0');
        else if (base == 16 && *s >= 'a' && *s <= 'f')
            digit = (u32)(*s - 'a') + 10;
        else if (base == 16 && *s >= 'A' && *s <= 'F')
            digit = (u32)(*s - 'A') + 10;
        else
            return E_INVAL;

        if (value > (0xFFFFFFFFu - digit) / base)
            return E_INVAL;                     /* dépassement */
        value = value * base + digit;
    }

    *out = value;
    return E_OK;
}

/* ---------------------------------------------------------------------- */

static int slot(const struct history *h, int i)
{
    return (h->start + i) % HIST_MAX;
}

void history_init(struct history *h)
{
    memset(h, 0, sizeof(*h));
}

void history_add(struct history *h, const char *line)
{
    usize len = strlen(line);

    h->pos = h->count;                          /* fin de navigation, quoi qu'il arrive */

    if (len == 0 || len >= HIST_LEN)
        return;
    if (h->count > 0 && strcmp(h->entry[slot(h, h->count - 1)], line) == 0)
        return;

    if (h->count == HIST_MAX) {                 /* plein : on écrase la plus ancienne */
        h->start = (h->start + 1) % HIST_MAX;
        h->count--;
    }
    memcpy(h->entry[slot(h, h->count)], line, len + 1);
    h->count++;
    h->pos = h->count;
}

const char *history_get(const struct history *h, int i)
{
    if (i < 0 || i >= h->count)
        return NULL;
    return h->entry[slot(h, i)];
}

const char *history_prev(struct history *h)
{
    if (h->pos <= 0)
        return NULL;
    h->pos--;
    return h->entry[slot(h, h->pos)];
}

const char *history_next(struct history *h)
{
    if (h->pos >= h->count)
        return NULL;
    h->pos++;
    if (h->pos == h->count)
        return "";
    return h->entry[slot(h, h->pos)];
}

int history_at_end(const struct history *h)
{
    return h->pos >= h->count;
}
