/* web : recupere une page web, en HTTP ou en HTTPS (0.10.2).
 *   web <hote> [chemin]            HTTP, port 80 (comme avant)
 *   web http://hote[:port][/chemin]
 *   web https://hote[:port][/chemin]   TLS 1.3, certificat du serveur verifie
 */
#include "phoenos.h"

static int put_text(char *out, int pos, int max, const char *s)
{
    while (*s) {
        if (pos >= max - 1)
            return -1;
        out[pos++] = *s++;
    }
    out[pos] = 0;
    return pos;
}

static void put_neg(int v)
{
    if (v < 0) {
        puts("-");
        putu((uint)-v);
    } else {
        putu((uint)v);
    }
}

static int starts(const char *s, const char *p)
{
    while (*p)
        if (*s++ != *p++)
            return 0;
    return 1;
}

/* Lecture/ecriture sur la connexion, claire ou securisee. */
static int net_send(int secure, int c, const void *b, uint n) { return secure ? tls_send(c, b, n) : sock_send(c, b, n); }
static int net_recv(int secure, int c, void *b, uint n)       { return secure ? tls_recv(c, b, n) : sock_recv(c, b, n); }

/* Sortie groupee : un appel systeme par bloc, pas par octet. */
static char obuf[1024];
static uint olen;
static void flush_out(void) { if (olen) { write(1, obuf, olen); olen = 0; } }
static void emit(char ch) { obuf[olen++] = ch; if (olen == sizeof obuf) flush_out(); }

static void explain(int secure, const char *step, int code)
{
    static char msg[160];
    puts("web : "); puts(step); puts(" (code "); put_neg(code); puts(")\n");
    if (secure && tls_error(msg, sizeof msg) > 0) {
        puts("      "); puts(msg); puts("\n");
    }
}

int main(int argc, char **argv)
{
    static char req[1024], buf[1024], host[128], path[512];
    const char *arg;
    int c, pos = 0, n, secure = 0;
    uint port = 80;

    if (argc < 2) {
        puts("usage : web <hote> [chemin]   ou   web http(s)://hote[:port][/chemin]\n");
        return 1;
    }
    arg = argv[1];
    path[0] = '/'; path[1] = 0;
    if (starts(arg, "https://")) { secure = 1; port = 443; arg += 8; }
    else if (starts(arg, "http://")) { arg += 7; }
    {   /* hote */
        int h = 0;
        while (*arg && *arg != '/' && *arg != ':' && h < (int)sizeof host - 1)
            host[h++] = *arg++;
        host[h] = 0;
        if (h == 0) { puts("web : nom d'hote manquant\n"); return 1; }
        if (*arg == ':') {
            uint p = 0;
            arg++;
            while (*arg >= '0' && *arg <= '9') p = p * 10 + (uint)(*arg++ - '0');
            if (p == 0 || p > 65535) { puts("web : port invalide\n"); return 1; }
            port = p;
        }
        if (*arg == '/') {
            if (put_text(path, 0, sizeof path, arg) < 0) { puts("web : chemin trop long\n"); return 1; }
        } else if (*arg) {
            puts("web : adresse incorrecte\n");
            return 1;
        }
    }
    if (argc > 2) {                                     /* ancienne forme : web hote chemin */
        if (put_text(path, 0, sizeof path, argv[2]) < 0) { puts("web : chemin trop long\n"); return 1; }
    }
    pos = put_text(req, 0, sizeof req, "GET ");
    if (pos >= 0) pos = put_text(req, pos, sizeof req, path);
    if (pos >= 0) pos = put_text(req, pos, sizeof req, " HTTP/1.0\r\nHost: ");
    if (pos >= 0) pos = put_text(req, pos, sizeof req, host);
    if (pos >= 0) pos = put_text(req, pos, sizeof req, "\r\nUser-Agent: PhoenOS-web\r\nConnection: close\r\n\r\n");
    if (pos < 0) {
        puts("web : hote ou chemin trop long\n");
        return 1;
    }

    c = secure ? tls_connect(host, port) : sock_connect(host, port);
    if (c < 0) {
        puts("web : connexion a "); puts(host); puts(" impossible (code "); put_neg(c); puts(")\n");
        if (secure) {
            static char msg[160];
            if (tls_error(msg, sizeof msg) > 0) { puts("      "); puts(msg); puts("\n"); }
        }
        return 1;
    }
    for (int sent = 0; sent < pos;) {
        n = net_send(secure, c, req + sent, (uint)(pos - sent));
        if (n <= 0) {
            explain(secure, "envoi echoue", n);
            if (secure) tls_close(c); else sock_close(c);
            return 1;
        }
        sent += n;
    }
    {   /* reponse : en-tetes recopies tels quels ; si le serveur repond en morceaux ("chunked", meme a une
         * requete HTTP/1.0, comme GitHub), le corps est reassemble au lieu d'afficher les tailles. */
        int in_body = 0, chunked = 0, km = 0;
        uint last4 = 0;
        int cs = 0;             /* 0 taille, 1 fin de ligne de taille, 2 donnees, 3 fin de morceau, 4 fini */
        uint left = 0, size = 0;

        for (;;) {
            n = net_recv(secure, c, buf, sizeof buf);
            if (n == 0)
                break;                          /* le serveur a ferme : fin de la page */
            if (n < 0) {
                flush_out();
                puts("\n");
                explain(secure, "reception interrompue", n);
                break;
            }
            for (int i = 0; i < n; i++) {
                char ch = buf[i];

                if (!in_body) {
                    static const char key[] = "chunked";

                    emit(ch);
                    /* cherche "chunked" (sans tenir compte de la casse) et la ligne vide qui finit les en-tetes */
                    if ((ch | 32) == key[km]) { if (++km == 7) { chunked = 1; km = 0; } }
                    else km = ((ch | 32) == 'c') ? 1 : 0;
                    last4 = (last4 << 8) | (uint)(unsigned char)ch;
                    if (last4 == 0x0D0A0D0Au)
                        in_body = 1;
                    continue;
                }
                if (!chunked) {
                    emit(ch);
                    continue;
                }
                switch (cs) {
                case 0:
                    if (ch >= '0' && ch <= '9') size = size * 16 + (uint)(ch - '0');
                    else if ((ch | 32) >= 'a' && (ch | 32) <= 'f') size = size * 16 + (uint)((ch | 32) - 'a' + 10);
                    else if (ch == '\n') { left = size; size = 0; cs = left ? 2 : 4; }
                    else cs = 1;                /* extension de morceau : ignoree jusqu'a la fin de ligne */
                    break;
                case 1:
                    if (ch == '\n') { left = size; size = 0; cs = left ? 2 : 4; }
                    break;
                case 2:
                    emit(ch);
                    if (--left == 0) cs = 3;
                    break;
                case 3:
                    if (ch == '\n') cs = 0;
                    break;
                default:
                    break;                      /* apres le dernier morceau : rien d'autre a afficher */
                }
            }
            flush_out();
        }
    }
    flush_out();
    if (secure) tls_close(c); else sock_close(c);
    return 0;
}
