/* PhoenOS - httpc.c : client HTTPS minimal (GET, redirections, chunked). Voir httpc.h. */
#include "httpc.h"
#include "string.h"

static char hdr[HTTP_HEADER_MAX + 1];
static char cur_host[HTTP_HOST_MAX];
static char cur_path[HTTP_PATH_MAX];
static char req[HTTP_PATH_MAX + HTTP_HOST_MAX + 200];
static u8 rbuf[2048];

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* Debut de ligne `s` = "nom:" (sans casse) ? renvoie le texte de la valeur, ou NULL. */
static const char *header_value(const char *s, const char *name)
{
    u32 n = (u32)strlen(name);

    for (u32 i = 0; i < n; i++)
        if (lower((unsigned char)s[i]) != name[i])
            return NULL;
    if (s[n] != ':')
        return NULL;
    s += n + 1;
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

struct body_state {
    int chunked;
    u32 clen;               /* Content-Length, ou 0xFFFFFFFF si inconnu */
    u32 got;                /* octets de corps deja remis (ou comptes pour clen) */
    u32 max;
    int cs;                 /* 0 taille, 1 fin de ligne de taille, 2 donnees, 3 CR apres donnees, 4 LF apres donnees, 5 fini */
    u32 csize, cleft;
    int digits;
    http_sink sink;
    void *sctx;
    struct http_result *r;
    int done;
};

/* Remet des octets de corps a l'appelant ; renvoie 0 ou un code d'erreur. */
static int deliver(struct body_state *b, const u8 *d, u32 n)
{
    if (!n)
        return 0;
    if (b->r->body + n < b->r->body || b->r->body + n > b->max)
        return HTTP_E_BIG;
    if (b->sink(b->sctx, d, n) != 0)
        return HTTP_E_SINK;
    b->r->body += n;
    return 0;
}

static int feed(struct body_state *b, const u8 *d, u32 n)
{
    int e;

    if (b->done)
        return 0;
    if (!b->chunked) {
        if (b->clen != 0xFFFFFFFFu && n > b->clen - b->got)
            n = b->clen - b->got;                       /* octets en trop ignores */
        e = deliver(b, d, n);
        b->got += n;
        if (b->clen != 0xFFFFFFFFu && b->got >= b->clen)
            b->done = 1;
        return e;
    }
    for (u32 i = 0; i < n;) {
        u8 c = d[i];

        switch (b->cs) {
        case 0:
            if ((c >= '0' && c <= '9') || (lower(c) >= 'a' && lower(c) <= 'f')) {
                if (++b->digits > 8)
                    return HTTP_E_CHUNK;
                b->csize = b->csize * 16 + (u32)((c <= '9') ? c - '0' : lower(c) - 'a' + 10);
            } else if (c == ';' || c == '\r' || c == ' ') {
                if (!b->digits)
                    return HTTP_E_CHUNK;
                b->cs = 1;
            } else if (c == '\n') {
                if (!b->digits)
                    return HTTP_E_CHUNK;
                goto size_done;
            } else {
                return HTTP_E_CHUNK;
            }
            i++;
            break;
        case 1:
            if (c == '\n') {
size_done:
                b->cleft = b->csize;
                b->csize = 0; b->digits = 0;
                if (b->cleft == 0) { b->cs = 5; b->done = 1; return 0; }
                b->cs = 2;
            }
            i++;
            break;
        case 2: {
            u32 take = n - i < b->cleft ? n - i : b->cleft;

            e = deliver(b, d + i, take);
            if (e)
                return e;
            i += take;
            b->cleft -= take;
            if (b->cleft == 0)
                b->cs = 3;
            break;
        }
        case 3:
            if (c != '\r')
                return HTTP_E_CHUNK;
            b->cs = 4; i++;
            break;
        case 4:
            if (c != '\n')
                return HTTP_E_CHUNK;
            b->cs = 0; i++;
            break;
        default:
            return 0;
        }
    }
    return 0;
}

static int put_str(char *out, u32 *pos, u32 cap, const char *s)
{
    u32 n = (u32)strlen(s);

    if (*pos + n >= cap)
        return -1;
    memcpy(out + *pos, s, n);
    *pos += n;
    out[*pos] = 0;
    return 0;
}

static int clean(const char *s)         /* aucun espace ni caractere de controle (pas d'injection d'en-tetes) */
{
    for (; *s; s++)
        if ((unsigned char)*s <= 0x20 || (unsigned char)*s > 0x7e)
            return 0;
    return 1;
}

/* Analyse une adresse de redirection. 0 : cur_host/cur_path (et *port) mis a jour. */
static int follow(const char *loc, u16 *port)
{
    const char *p = loc;
    char nh[HTTP_HOST_MAX];
    u32 hl = 0;
    u32 prt = 443;

    if (!*loc || !clean(loc))
        return -1;
    if (loc[0] == '/' && loc[1] != '/') {
        if (strlen(loc) >= sizeof cur_path)
            return -1;
        memcpy(cur_path, loc, strlen(loc) + 1);
        return 0;
    }
    if (!(p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p' && p[4] == 's' && p[5] == ':' && p[6] == '/' && p[7] == '/'))
        return -1;                                      /* http:// (retour en arriere) ou autre chose : refuse */
    p += 8;
    while (*p && *p != '/' && *p != ':' && *p != '?' && *p != '#') {
        if (hl + 1 >= sizeof nh)
            return -1;
        nh[hl++] = *p++;
    }
    nh[hl] = 0;
    if (!hl)
        return -1;
    if (*p == ':') {
        prt = 0;
        p++;
        if (*p < '0' || *p > '9')
            return -1;
        while (*p >= '0' && *p <= '9') {
            prt = prt * 10 + (u32)(*p++ - '0');
            if (prt > 65535)
                return -1;
        }
        if (!prt)
            return -1;
    }
    if (*p == '/') {
        if (strlen(p) >= sizeof cur_path)
            return -1;
        memcpy(cur_path, p, strlen(p) + 1);
    } else if (*p == 0) {
        cur_path[0] = '/'; cur_path[1] = 0;
    } else {
        return -1;                                      /* "?..." ou "#..." sans chemin : non gere */
    }
    memcpy(cur_host, nh, hl + 1);
    *port = (u16)prt;
    return 0;
}

static int u32_str(char *out, u32 v)
{
    char t[11];
    int n = 0, k = 0;

    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n)
        out[k++] = t[--n];
    out[k] = 0;
    return k;
}

int http_get(const struct http_net *n, const char *host, const char *path, http_sink sink, void *sctx,
             u32 max_body, struct http_result *r)
{
    u16 port = 443;

    memset(r, 0, sizeof *r);
    if (!n || !host || !path || path[0] != '/' || strlen(host) >= sizeof cur_host || strlen(path) >= sizeof cur_path ||
        !clean(host) || !clean(path))
        return HTTP_E_REDIRECT;
    memcpy(cur_host, host, strlen(host) + 1);
    memcpy(cur_path, path, strlen(path) + 1);

    for (;;) {
        u32 rq = 0, hlen = 0, hend = 0, tail;
        int rc, status, redirect, found = 0;
        struct body_state b;
        char *line;
        char loc[HTTP_PATH_MAX];
        int have_loc = 0;

        req[0] = 0;
        if (put_str(req, &rq, sizeof req, "GET ") || put_str(req, &rq, sizeof req, cur_path) ||
            put_str(req, &rq, sizeof req, " HTTP/1.1\r\nHost: ") || put_str(req, &rq, sizeof req, cur_host))
            return HTTP_E_REDIRECT;
        if (port != 443) {
            char num[11];

            u32_str(num, port);
            if (put_str(req, &rq, sizeof req, ":") || put_str(req, &rq, sizeof req, num))
                return HTTP_E_REDIRECT;
        }
        if (put_str(req, &rq, sizeof req, "\r\nUser-Agent: PhoenOS-maj\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n"))
            return HTTP_E_REDIRECT;

        rc = n->open(n->ctx, cur_host, port);
        if (rc != 0) {
            r->net_err = rc;
            return HTTP_E_OPEN;
        }
        for (u32 sent = 0; sent < rq;) {
            int k = n->send(n->ctx, (const u8 *)req + sent, rq - sent);

            if (k <= 0) {
                n->close(n->ctx);
                return HTTP_E_SEND;
            }
            sent += (u32)k;
        }

        /* en-tetes */
        while (!found) {
            int k;

            if (hlen >= HTTP_HEADER_MAX) {
                n->close(n->ctx);
                return HTTP_E_HEADER;
            }
            k = n->recv(n->ctx, (u8 *)hdr + hlen, HTTP_HEADER_MAX - hlen);
            if (k < 0) { n->close(n->ctx); return HTTP_E_RECV; }
            if (k == 0) { n->close(n->ctx); return hlen ? HTTP_E_HEADER : HTTP_E_RECV; }
            for (u32 i = 0; i < (u32)k; i++) {
                u32 at = hlen + i;

                if (at >= 3 && hdr[at - 3] == '\r' && hdr[at - 2] == '\n' && hdr[at - 1] == '\r' && hdr[at] == '\n') {
                    hend = at + 1;
                    found = 1;
                    break;
                }
            }
            hlen += (u32)k;
        }
        tail = hlen - hend;                              /* debut du corps deja recu */

        /* ligne d'etat : HTTP/1.x SSS */
        if (hend < 12 || hdr[0] != 'H' || hdr[1] != 'T' || hdr[2] != 'T' || hdr[3] != 'P' || hdr[4] != '/' ||
            hdr[5] != '1' || hdr[6] != '.' || (hdr[7] != '0' && hdr[7] != '1') || hdr[8] != ' ' ||
            hdr[9] < '0' || hdr[9] > '9' || hdr[10] < '0' || hdr[10] > '9' || hdr[11] < '0' || hdr[11] > '9') {
            n->close(n->ctx);
            return HTTP_E_HEADER;
        }
        status = (hdr[9] - '0') * 100 + (hdr[10] - '0') * 10 + (hdr[11] - '0');
        r->status = status;
        redirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;

        memset(&b, 0, sizeof b);
        b.clen = 0xFFFFFFFFu;
        b.max = max_body;
        b.sink = sink; b.sctx = sctx; b.r = r;

        /* en-tetes utiles : Content-Length, Transfer-Encoding, Location (lignes terminees par NUL sur place) */
        {
            u32 pos = 0;

            while (pos < hend - 2) {
                u32 e = pos;
                const char *v;

                while (e + 1 < hend && !(hdr[e] == '\r' && hdr[e + 1] == '\n'))
                    e++;
                hdr[e] = 0;
                line = hdr + pos;
                if ((v = header_value(line, "content-length")) != NULL) {
                    u32 val = 0;
                    int digits = 0;

                    while (*v >= '0' && *v <= '9' && digits < 10) { val = val * 10 + (u32)(*v - '0'); v++; digits++; }
                    if (!digits || *v) { n->close(n->ctx); return HTTP_E_HEADER; }
                    b.clen = val;
                } else if ((v = header_value(line, "transfer-encoding")) != NULL) {
                    const char *t = "chunked";
                    u32 k = 0;

                    while (t[k] && lower((unsigned char)v[k]) == t[k])
                        k++;
                    if (!t[k] && !v[k])
                        b.chunked = 1;
                    else if (lower((unsigned char)v[0]) != 'i') { n->close(n->ctx); return HTTP_E_HEADER; }   /* autre codage : non gere */
                } else if ((v = header_value(line, "location")) != NULL) {
                    if (strlen(v) >= sizeof loc) { n->close(n->ctx); return HTTP_E_REDIRECT; }
                    memcpy(loc, v, strlen(v) + 1);
                    have_loc = 1;
                }
                pos = e + 2;
            }
        }

        if (redirect) {
            n->close(n->ctx);
            if (!have_loc || ++r->redirects > HTTP_MAX_REDIRECTS)
                return HTTP_E_REDIRECT;
            if (follow(loc, &port) != 0)
                return HTTP_E_REDIRECT;
            continue;
        }
        if (status != 200) {
            n->close(n->ctx);
            return HTTP_E_STATUS;
        }

        /* corps */
        {
            int e = 0;

            if (tail)
                e = feed(&b, (const u8 *)hdr + hend, tail);
            while (!e && !b.done) {
                int k = n->recv(n->ctx, rbuf, sizeof rbuf);

                if (k < 0) { e = HTTP_E_RECV; break; }
                if (k == 0) {
                    if (b.chunked || (b.clen != 0xFFFFFFFFu && b.got < b.clen))
                        e = HTTP_E_TRUNC;
                    break;
                }
                e = feed(&b, rbuf, (u32)k);
            }
            n->close(n->ctx);
            return e;
        }
    }
}

const char *http_strerror(int c)
{
    switch (c) {
    case HTTP_OK:         return "ok";
    case HTTP_E_OPEN:     return "connexion impossible";
    case HTTP_E_SEND:     return "envoi de la requete impossible";
    case HTTP_E_RECV:     return "reception interrompue";
    case HTTP_E_HEADER:   return "reponse du serveur incomprehensible";
    case HTTP_E_STATUS:   return "le serveur n'a pas la page demandee";
    case HTTP_E_REDIRECT: return "redirection refusee";
    case HTTP_E_TRUNC:    return "reponse coupee avant la fin";
    case HTTP_E_BIG:      return "fichier plus gros qu'annonce";
    case HTTP_E_SINK:     return "transfert arrete";
    case HTTP_E_CHUNK:    return "decoupage de la reponse invalide";
    }
    return "erreur de telechargement";
}
