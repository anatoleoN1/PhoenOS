/*
 * Test du client HTTPS du noyau (httpc.c) avec un FAUX serveur : reponses decoupees en morceaux de 1 a n octets,
 * Content-Length, "chunked", redirections (relatives, absolues, chaines, boucles, retour en HTTP refuse), erreurs.
 */
#include "testlib.h"
#include "httpc.h"

u8 test_scancode;
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }

struct route { const char *host, *path; const char *resp; };
static const struct route *routes;
static int nroutes;
static char req[4096];
static u32 req_len;
static char open_host[HTTP_HOST_MAX];
static u16 open_port;
static int opens, closes, open_fail, send_fail, recv_err_at;
static u32 chunk_size = 1000000;
static const char *cur;
static u32 cur_len, cur_pos;
static char big[20000];
static char hugehdr[HTTP_HEADER_MAX + 2000];
static char last_reqs[8][256];
static int nreqs;

static int strncmp_(const char *a, const char *b, u32 n) { for (u32 i = 0; i < n; i++) if (a[i] != b[i]) return 1; return 0; }
static const char *strstr_(const char *h, const char *n)
{
    u32 nl = (u32)strlen(n);
    for (u32 i = 0; h[i]; i++) if (!strncmp_(h + i, n, nl)) return h + i;
    return 0;
}

static int f_open(void *c, const char *host, u16 port)
{
    (void)c;
    opens++;
    if (open_fail) return -24;
    {
        u32 i = 0;
        while (host[i] && i < sizeof open_host - 1) { open_host[i] = host[i]; i++; }
        open_host[i] = 0;
    }
    open_port = port;
    req_len = 0; cur = 0; cur_len = 0; cur_pos = 0;
    return 0;
}

static int f_send(void *c, const u8 *d, u32 n)
{
    (void)c;
    if (send_fail) return -1;
    if (req_len + n >= sizeof req) return -1;
    memcpy(req + req_len, d, n); req_len += n; req[req_len] = 0;
    if (req_len >= 4 && !memcmp(req + req_len - 4, "\r\n\r\n", 4)) {
        char path[2048];
        u32 i = 4, k = 0;

        while (req[i] && req[i] != ' ' && k < sizeof path - 1) path[k++] = req[i++];
        path[k] = 0;
        if (nreqs < 8) { u32 j = 0; while (j < 255 && req[j] && req[j] != '\r') { last_reqs[nreqs][j] = req[j]; j++; } last_reqs[nreqs][j] = 0; }
        nreqs++;
        cur = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        for (int r = 0; r < nroutes; r++)
            if (!strcmp(routes[r].path, path) && !strcmp(routes[r].host, open_host))
                cur = routes[r].resp;
        cur_len = (u32)strlen(cur); cur_pos = 0;
    }
    return (int)n;
}

static int f_recv(void *c, u8 *b, u32 cap)
{
    u32 n;
    (void)c;
    if (recv_err_at >= 0 && (int)cur_pos >= recv_err_at) return -1;
    if (!cur || cur_pos >= cur_len) return 0;
    n = cur_len - cur_pos;
    if (n > cap) n = cap;
    if (n > chunk_size) n = chunk_size;
    memcpy(b, cur + cur_pos, n); cur_pos += n;
    return (int)n;
}
static void f_close(void *c) { (void)c; closes++; }

static struct http_net NET = { 0, f_open, f_send, f_recv, f_close };

static char got[30000];
static u32 got_len;
static u32 sink_calls, abort_after = 0xFFFFFFFFu;
static int sink(void *c, const u8 *d, u32 n)
{
    (void)c;
    sink_calls++;
    if (sink_calls > abort_after) return 7;
    if (got_len + n > sizeof got) return 1;
    memcpy(got + got_len, d, n); got_len += n;
    return 0;
}

static struct http_result R;
static int get(const char *host, const char *path, u32 max)
{
    got_len = 0; sink_calls = 0; opens = closes = 0; nreqs = 0; recv_err_at = -1;
    return http_get(&NET, host, path, sink, 0, max, &R);
}
static int same(const char *s) { return got_len == (u32)strlen(s) && !memcmp(got, s, got_len); }

int test_main(void)
{
    static const struct route T1[] = {
        { "h.org", "/plain", "HTTP/1.1 200 OK\r\nServer: x\r\nContent-Length: 11\r\n\r\nhello world" },
        { "h.org", "/lower", "HTTP/1.0 200 OK\r\ncontent-length: 5\r\n\r\nabcdeEXTRA-IGNORED" },
        { "h.org", "/upper", "HTTP/1.1 200 OK\r\nCONTENT-LENGTH:    3\r\n\r\nxyz" },
        { "h.org", "/close", "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nno length here" },
        { "h.org", "/chunked", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n8;ext=1\r\n, world!\r\nA\r\n0123456789\r\n0\r\n\r\n" },
        { "h.org", "/chunkedCAPS", "HTTP/1.1 200 OK\r\ntransfer-encoding: Chunked\r\n\r\nB\r\nabcdefghijk\r\n0\r\n\r\n" },
        { "h.org", "/empty", "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n" },
        { "h.org", "/r1", "HTTP/1.1 302 Found\r\nLocation: /plain\r\nContent-Length: 0\r\n\r\n" },
        { "h.org", "/r2", "HTTP/1.1 301 Moved\r\nLocation: https://cdn.org/files/a.bin?sig=abc&x=1\r\n\r\n" },
        { "cdn.org", "/files/a.bin?sig=abc&x=1", "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nDATA" },
        { "h.org", "/chain", "HTTP/1.1 307 T\r\nLocation: /r2\r\n\r\n" },
        { "h.org", "/loop", "HTTP/1.1 302 Found\r\nLocation: /loop\r\n\r\n" },
        { "h.org", "/down", "HTTP/1.1 302 Found\r\nLocation: http://h.org/plain\r\n\r\n" },
        { "h.org", "/noloc", "HTTP/1.1 302 Found\r\nContent-Length: 0\r\n\r\n" },
        { "h.org", "/port", "HTTP/1.1 302 Found\r\nLocation: https://alt.org:8443/p\r\n\r\n" },
        { "alt.org", "/p", "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok" },
        { "h.org", "/space", "HTTP/1.1 302 Found\r\nLocation: /a b\r\n\r\n" },
        { "h.org", "/hostonly", "HTTP/1.1 302 Found\r\nLocation: https://cdn.org\r\n\r\n" },
        { "cdn.org", "/", "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nroot" },
        { "h.org", "/schemerel", "HTTP/1.1 302 Found\r\nLocation: //evil.org/x\r\n\r\n" },
        { "h.org", "/ftp", "HTTP/1.1 302 Found\r\nLocation: ftp://h.org/x\r\n\r\n" },
        { "h.org", "/badport", "HTTP/1.1 302 Found\r\nLocation: https://h.org:99999/x\r\n\r\n" },
        { "h.org", "/trunc", "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nshort" },
        { "h.org", "/chunktrunc", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhel" },
        { "h.org", "/badchunk", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZZ\r\nhello\r\n0\r\n\r\n" },
        { "h.org", "/hugechunk", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n123456789\r\nx\r\n" },
        { "h.org", "/nocrlf", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhelloXX0\r\n\r\n" },
        { "h.org", "/garbage", "SSH-2.0-OpenSSH\r\n\r\n" },
        { "h.org", "/http2", "HTTP/2 200\r\n\r\nx" },
        { "h.org", "/gz", "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nTransfer-Encoding: gzip\r\n\r\nx" },
        { "h.org", "/badlen", "HTTP/1.1 200 OK\r\nContent-Length: 12x\r\n\r\nx" },
        { "h.org", "/500", "HTTP/1.1 500 Oops\r\nContent-Length: 0\r\n\r\n" },
        { "h.org", "/204", "HTTP/1.1 204 No Content\r\n\r\n" },
    };
    int rc;

    routes = T1; nroutes = (int)(sizeof T1 / sizeof T1[0]);

    /* reponse simple, quelle que soit la taille des morceaux recus */
    for (u32 ch = 1; ch <= 64; ch = ch < 8 ? ch + 1 : ch * 2) {
        chunk_size = ch;
        rc = get("h.org", "/plain", 1000);
        CHECK(rc == HTTP_OK && same("hello world") && R.status == 200 && R.body == 11 && opens == 1 && closes == 1);
        rc = get("h.org", "/chunked", 1000);
        CHECK(rc == HTTP_OK && same("hello, world!0123456789"));
        rc = get("h.org", "/chunkedCAPS", 1000);
        CHECK(rc == HTTP_OK && same("abcdefghijk"));
        rc = get("h.org", "/r2", 1000);
        CHECK(rc == HTTP_OK && same("DATA") && R.redirects == 1 && opens == 2 && closes == 2);
        rc = get("h.org", "/close", 1000);
        CHECK(rc == HTTP_OK && same("no length here"));
    }
    chunk_size = 1000000;
    CHECK(get("h.org", "/lower", 1000) == HTTP_OK && same("abcde"));                    /* octets en trop apres Content-Length : ignores */
    CHECK(get("h.org", "/upper", 1000) == HTTP_OK && same("xyz"));
    CHECK(get("h.org", "/empty", 1000) == HTTP_OK && got_len == 0 && sink_calls == 0);

    /* la requete envoyee */
    CHECK(get("h.org", "/plain", 1000) == HTTP_OK);
    CHECK(!strncmp_(req, "GET /plain HTTP/1.1\r\nHost: h.org\r\n", 33));
    CHECK(strstr_(req, "Connection: close\r\n") && strstr_(req, "Accept-Encoding: identity\r\n") && strstr_(req, "User-Agent: PhoenOS-maj\r\n"));
    CHECK(open_port == 443);

    /* redirections */
    CHECK(get("h.org", "/r1", 1000) == HTTP_OK && same("hello world") && R.redirects == 1);       /* relative, meme serveur */
    CHECK(!strcmp(open_host, "h.org"));
    CHECK(get("h.org", "/chain", 1000) == HTTP_OK && same("DATA") && R.redirects == 2 && !strcmp(open_host, "cdn.org"));
    CHECK(get("h.org", "/loop", 1000) == HTTP_E_REDIRECT && R.redirects == HTTP_MAX_REDIRECTS + 1 && closes == opens);
    CHECK(get("h.org", "/down", 1000) == HTTP_E_REDIRECT && opens == 1);                          /* retour a http:// : refuse */
    CHECK(get("h.org", "/noloc", 1000) == HTTP_E_REDIRECT);
    CHECK(get("h.org", "/port", 1000) == HTTP_OK && same("ok") && open_port == 8443 && strstr_(last_reqs[1], "GET /p HTTP/1.1"));
    CHECK(strstr_(req, "Host: alt.org:8443\r\n"));
    CHECK(get("h.org", "/space", 1000) == HTTP_E_REDIRECT);                                       /* espace dans l'adresse */
    CHECK(get("h.org", "/hostonly", 1000) == HTTP_OK && same("root"));                            /* https://cdn.org sans chemin */
    CHECK(get("h.org", "/schemerel", 1000) == HTTP_E_REDIRECT);
    CHECK(get("h.org", "/ftp", 1000) == HTTP_E_REDIRECT);
    CHECK(get("h.org", "/badport", 1000) == HTTP_E_REDIRECT);

    /* reponses incorrectes ou coupees */
    CHECK(get("h.org", "/trunc", 1000) == HTTP_E_TRUNC && got_len == 5);
    CHECK(get("h.org", "/chunktrunc", 1000) == HTTP_E_TRUNC);
    CHECK(get("h.org", "/badchunk", 1000) == HTTP_E_CHUNK);
    CHECK(get("h.org", "/hugechunk", 1000) == HTTP_E_CHUNK);
    CHECK(get("h.org", "/nocrlf", 1000) == HTTP_E_CHUNK);
    CHECK(get("h.org", "/garbage", 1000) == HTTP_E_HEADER);
    CHECK(get("h.org", "/http2", 1000) == HTTP_E_HEADER);
    CHECK(get("h.org", "/gz", 1000) == HTTP_E_HEADER);
    CHECK(get("h.org", "/badlen", 1000) == HTTP_E_HEADER);
    CHECK(get("h.org", "/500", 1000) == HTTP_E_STATUS && R.status == 500 && got_len == 0);
    CHECK(get("h.org", "/204", 1000) == HTTP_E_STATUS && R.status == 204);
    CHECK(get("h.org", "/nothere", 1000) == HTTP_E_STATUS && R.status == 404);
    CHECK(closes == opens);

    /* limites */
    CHECK(get("h.org", "/plain", 10) == HTTP_E_BIG);                                              /* plus gros que prevu */
    CHECK(get("h.org", "/chunked", 10) == HTTP_E_BIG);
    abort_after = 0;
    CHECK(get("h.org", "/plain", 1000) == HTTP_E_SINK);
    abort_after = 0xFFFFFFFFu;
    CHECK(get("h.org", "plain", 1000) == HTTP_E_REDIRECT);                                        /* chemin sans / */
    CHECK(get("h.org", "/a b", 1000) == HTTP_E_REDIRECT);
    CHECK(get("bad host", "/", 1000) == HTTP_E_REDIRECT);

    /* en-tetes trop gros */
    {
        static struct route T2[1];
        u32 n = 0;

        n = (u32)strlen("HTTP/1.1 200 OK\r\n");
        memcpy(hugehdr, "HTTP/1.1 200 OK\r\n", n);
        memcpy(hugehdr + n, "X-Big: ", 7); n += 7;
        for (u32 i = 0; i < HTTP_HEADER_MAX + 100; i++) hugehdr[n++] = 'a';
        memcpy(hugehdr + n, "\r\nContent-Length: 1\r\n\r\nx", 25); n += 25; hugehdr[n] = 0;
        T2[0].host = "h.org"; T2[0].path = "/huge"; T2[0].resp = hugehdr;
        routes = T2; nroutes = 1;
        CHECK(get("h.org", "/huge", 1000) == HTTP_E_HEADER);
    }
    /* en-tetes volumineux mais acceptes (en-tetes de 6 Kio, comme GitHub) puis corps de 20000 octets en morceaux */
    {
        static struct route T3[1];
        static char resp[HTTP_HEADER_MAX + 21000];
        u32 n = 0;

        memcpy(resp, "HTTP/1.1 200 OK\r\n", 17); n = 17;
        memcpy(resp + n, "X-Big: ", 7); n += 7;
        for (u32 i = 0; i < 6000; i++) resp[n++] = 'b';
        memcpy(resp + n, "\r\nContent-Length: 20000\r\n\r\n", 27); n += 27;
        for (u32 i = 0; i < 20000; i++) big[i] = (char)('a' + i % 26);
        memcpy(resp + n, big, 20000); n += 20000; resp[n] = 0;
        T3[0].host = "h.org"; T3[0].path = "/bigbody"; T3[0].resp = resp;
        routes = T3; nroutes = 1;
        for (u32 ch = 1000; ch <= 100000; ch *= 10) {
            chunk_size = ch;
            CHECK(get("h.org", "/bigbody", 30000) == HTTP_OK && got_len == 20000 && !memcmp(got, big, 20000));
        }
        chunk_size = 1000000;
    }

    /* reseau en panne */
    routes = T1; nroutes = (int)(sizeof T1 / sizeof T1[0]);
    open_fail = 1;
    CHECK(get("h.org", "/plain", 1000) == HTTP_E_OPEN && R.net_err == -24 && closes == 0);
    open_fail = 0;
    send_fail = 1;
    CHECK(get("h.org", "/plain", 1000) == HTTP_E_SEND && closes == 1);
    send_fail = 0;
    {
        got_len = 0; sink_calls = 0; opens = closes = 0; nreqs = 0;
        chunk_size = 10;
        recv_err_at = 40;
        CHECK(http_get(&NET, "h.org", "/plain", sink, 0, 1000, &R) == HTTP_E_RECV && closes == 1);
        recv_err_at = -1;
        chunk_size = 1000000;
    }
    {   /* le serveur ferme sans rien dire */
        static const struct route T4[] = { { "h.org", "/none", "" } };
        routes = T4; nroutes = 1;
        CHECK(get("h.org", "/none", 1000) == HTTP_E_RECV);
    }
    CHECK(http_strerror(HTTP_E_TRUNC)[0] != 0 && http_strerror(-99)[0] != 0);

    out("  Client HTTPS verifie : Content-Length, chunked, morceaux de 1 octet, redirections (chaines, boucle, http refuse), en-tetes de 6 Kio, 30 erreurs\n");
    return 0;
}
