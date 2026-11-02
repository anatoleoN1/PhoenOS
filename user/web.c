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
    for (;;) {
        n = net_recv(secure, c, buf, sizeof buf);
        if (n == 0)
            break;                              /* le serveur a ferme : fin de la page */
        if (n < 0) {
            puts("\n");
            explain(secure, "reception interrompue", n);
            break;
        }
        write(1, buf, (uint)n);
    }
    if (secure) tls_close(c); else sock_close(c);
    return 0;
}
