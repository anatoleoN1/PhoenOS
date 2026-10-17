/* web : recupere une page web en HTTP (0.10.0) : web <hote> [chemin]. Connexion TCP au port 80, requete HTTP/1.0. */
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

int main(int argc, char **argv)
{
    static char req[512];
    static char buf[1024];
    const char *host, *path = "/";
    int c, pos = 0, n;

    if (argc < 2) {
        puts("usage : web <hote> [chemin]\n");
        return 1;
    }
    host = argv[1];
    if (argc > 2)
        path = argv[2];
    pos = put_text(req, 0, sizeof req, "GET ");
    if (pos >= 0) pos = put_text(req, pos, sizeof req, path);
    if (pos >= 0) pos = put_text(req, pos, sizeof req, " HTTP/1.0\r\nHost: ");
    if (pos >= 0) pos = put_text(req, pos, sizeof req, host);
    if (pos >= 0) pos = put_text(req, pos, sizeof req, "\r\nUser-Agent: PhoenOS-web\r\nConnection: close\r\n\r\n");
    if (pos < 0) {
        puts("web : hote ou chemin trop long\n");
        return 1;
    }

    c = sock_connect(host, 80);
    if (c < 0) {
        puts("web : connexion a "); puts(host); puts(" impossible (code ");
        put_neg(c);
        puts(")\n");
        return 1;
    }
    for (int sent = 0; sent < pos;) {
        n = sock_send(c, req + sent, (uint)(pos - sent));
        if (n <= 0) {
            puts("web : envoi echoue (code "); put_neg(n); puts(")\n");
            sock_close(c);
            return 1;
        }
        sent += n;
    }
    for (;;) {
        n = sock_recv(c, buf, sizeof buf);
        if (n == 0)
            break;                              /* le serveur a ferme : fin de la page */
        if (n < 0) {
            puts("\nweb : reception interrompue (code "); put_neg(n); puts(")\n");
            break;
        }
        write(1, buf, (uint)n);
    }
    sock_close(c);
    return 0;
}
