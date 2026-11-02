/* Certificats X.509 : DER, chaine, nom. Voir x509.h. */
#include "x509.h"
#include "pubkey.h"
#include "crypto.h"
#include "string.h"

extern const u8 *const roots_der[];
extern const u32 roots_len[];
extern const int roots_count;

static const u8 *const *g_roots;
static const u32 *g_roots_len;
static int g_roots_n = -1;

void x509_set_roots(const u8 *const *der, const u32 *len, int n)
{
    g_roots = der; g_roots_len = len; g_roots_n = n;
    if (!der) g_roots_n = -1;
}

static int roots_n(void) { return g_roots_n >= 0 ? g_roots_n : roots_count; }
static const u8 *root_der(int i) { return g_roots_n >= 0 ? g_roots[i] : roots_der[i]; }
static u32 root_len(int i) { return g_roots_n >= 0 ? g_roots_len[i] : roots_len[i]; }

/* ---- DER ---- */
/* Lit un element : tag, debut du contenu, longueur. -1 si mal forme. Avance *p apres l'element. */
static int tlv(const u8 **p, const u8 *end, u8 *tag, const u8 **val, u32 *vlen)
{
    u32 l;
    if (end - *p < 2) return -1;
    *tag = *(*p)++;
    if ((*tag & 0x1f) == 0x1f) return -1;                   /* tags longs : inutiles ici */
    l = *(*p)++;
    if (l & 0x80) {
        u32 k = l & 0x7f;
        if (k == 0 || k > 3 || (u32)(end - *p) < k) return -1;
        l = 0;
        while (k--) l = l << 8 | *(*p)++;
    }
    if (l > (u32)(end - *p)) return -1;
    *val = *p; *vlen = l;
    *p += l;
    return 0;
}

/* Comme tlv mais exige un tag donne ; start = debut de l'element complet (en-tete compris). */
static int expect(const u8 **p, const u8 *end, u8 want, const u8 **val, u32 *vlen, const u8 **start)
{
    u8 tag;
    const u8 *s = *p;
    if (tlv(p, end, &tag, val, vlen) || tag != want) return -1;
    if (start) *start = s;
    return 0;
}

static int oid_is(const u8 *v, u32 vl, const u8 *oid, u32 ol) { return vl == ol && memcmp(v, oid, ol) == 0; }
#define OIDEQ(v, vl, arr) oid_is(v, vl, arr, sizeof(arr))

static const u8 O_RSA[]       = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x01};
static const u8 O_EC[]        = {0x2a,0x86,0x48,0xce,0x3d,0x02,0x01};
static const u8 O_P256[]      = {0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07};
static const u8 O_P384[]      = {0x2b,0x81,0x04,0x00,0x22};
static const u8 O_ED25519[]   = {0x2b,0x65,0x70};
static const u8 O_RSA256[]    = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0b};
static const u8 O_RSA384[]    = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0c};
static const u8 O_RSA512[]    = {0x2a,0x86,0x48,0x86,0xf7,0x0d,0x01,0x01,0x0d};
static const u8 O_ECDSA256[]  = {0x2a,0x86,0x48,0xce,0x3d,0x04,0x03,0x02};
static const u8 O_ECDSA384[]  = {0x2a,0x86,0x48,0xce,0x3d,0x04,0x03,0x03};
static const u8 O_ECDSA512[]  = {0x2a,0x86,0x48,0xce,0x3d,0x04,0x03,0x04};
static const u8 O_BC[]        = {0x55,0x1d,0x13};
static const u8 O_KU[]        = {0x55,0x1d,0x0f};
static const u8 O_SAN[]       = {0x55,0x1d,0x11};
static const u8 O_EKU[]       = {0x55,0x1d,0x25};
static const u8 O_SERVERAUTH[] = {0x2b,0x06,0x01,0x05,0x05,0x07,0x03,0x01};
static const u8 O_ANYEKU[]    = {0x55,0x1d,0x25,0x00};
/* extensions connues et sans effet sur nos controles */
static const u8 O_SKI[]  = {0x55,0x1d,0x0e}, O_AKI[] = {0x55,0x1d,0x23}, O_POL[] = {0x55,0x1d,0x20}, O_CRLDP[] = {0x55,0x1d,0x1f},
                O_AIA[]  = {0x2b,0x06,0x01,0x05,0x05,0x07,0x01,0x01}, O_SCT[] = {0x2b,0x06,0x01,0x04,0x01,0xd6,0x79,0x02,0x04,0x02},
                O_IAN[]  = {0x55,0x1d,0x12}, O_POLC[] = {0x55,0x1d,0x24}, O_INHIBIT[] = {0x55,0x1d,0x36}, O_SIA[] = {0x2b,0x06,0x01,0x05,0x05,0x07,0x01,0x0b},
                O_FCRL[] = {0x55,0x1d,0x2e}, O_POLM[] = {0x55,0x1d,0x21}, O_PRECERT_POISON[] = {0x2b,0x06,0x01,0x04,0x01,0xd6,0x79,0x02,0x04,0x03};

/* ---- dates ---- */
static int leap(u32 y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

u32 x509_time_from_civil(u32 year, u32 mon, u32 day, u32 hour, u32 min, u32 sec)
{
    static const u8 dm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    u32 days = 0;
    if (year < 1970 || mon < 1 || mon > 12 || day < 1 || hour > 23 || min > 59 || sec > 60)
        return 0;
    if (day > (u32)dm[mon - 1] + (mon == 2 && leap(year)))
        return 0;
    if (year >= 2106)
        return 0xffffffffu;
    for (u32 y = 1970; y < year; y++) days += leap(y) ? 366 : 365;
    for (u32 m = 1; m < mon; m++) days += dm[m - 1] + (m == 2 && leap(year));
    days += day - 1;
    return days * 86400u + hour * 3600u + min * 60u + sec;
}

static int num2(const u8 *p, u32 *v)
{
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return -1;
    *v = (u32)(p[0] - '0') * 10 + (u32)(p[1] - '0');
    return 0;
}

static int parse_time(const u8 **p, const u8 *end, u32 *out)
{
    u8 tag;
    const u8 *v;
    u32 l, y, mo, d, h, mi, s, c = 0, yy;
    if (tlv(p, end, &tag, &v, &l)) return -1;
    if (tag == 0x17 && l == 13) {
        if (num2(v, &yy)) return -1;
        y = yy >= 50 ? 1900 + yy : 2000 + yy;
        v += 2;
    } else if (tag == 0x18 && l == 15) {
        if (num2(v, &yy) || num2(v + 2, &c)) return -1;
        y = yy * 100 + c;
        v += 4;
    } else return -1;
    if (num2(v, &mo) || num2(v + 2, &d) || num2(v + 4, &h) || num2(v + 6, &mi) || num2(v + 8, &s) || v[10] != 'Z')
        return -1;
    *out = x509_time_from_civil(y, mo, d, h, mi, s);
    return *out == 0 ? -1 : 0;
}

static int sigalg_from(const u8 *v, u32 vl)
{
    if (OIDEQ(v, vl, O_RSA256)) return SIG_RSA_SHA256;
    if (OIDEQ(v, vl, O_RSA384)) return SIG_RSA_SHA384;
    if (OIDEQ(v, vl, O_RSA512)) return SIG_RSA_SHA512;
    if (OIDEQ(v, vl, O_ECDSA256)) return SIG_ECDSA_SHA256;
    if (OIDEQ(v, vl, O_ECDSA384)) return SIG_ECDSA_SHA384;
    if (OIDEQ(v, vl, O_ECDSA512)) return SIG_ECDSA_SHA512;
    if (OIDEQ(v, vl, O_ED25519)) return SIG_ED25519;
    return SIG_NONE;
}

static int parse_spki(struct x509 *c, const u8 *v, u32 vl)
{
    const u8 *p = v, *end = v + vl, *alg, *oid, *bits, *q, *qe;
    u32 al, ol, bl, l;
    u8 tag;
    if (expect(&p, end, 0x30, &alg, &al, NULL)) return -1;
    if (expect(&p, end, 0x03, &bits, &bl, NULL) || p != end || bl < 1 || bits[0] != 0) return -1;
    bits++; bl--;
    q = alg; qe = alg + al;
    if (expect(&q, qe, 0x06, &oid, &ol, NULL)) return -1;
    if (OIDEQ(oid, ol, O_RSA)) {
        const u8 *seq, *n, *e;
        u32 sl, nl, el;
        const u8 *r = bits, *re = bits + bl;
        if (expect(&r, re, 0x30, &seq, &sl, NULL) || r != re) return -1;
        r = seq; re = seq + sl;
        if (expect(&r, re, 0x02, &n, &nl, NULL) || expect(&r, re, 0x02, &e, &el, NULL) || r != re) return -1;
        while (nl && *n == 0) { n++; nl--; }
        while (el && *e == 0) { e++; el--; }
        if (nl < 256 || nl > 512 || el == 0 || el > 4) { c->key_type = KEY_NONE; return X509_E_SIGALG; }   /* RSA < 2048 bits refuse */
        c->key_type = KEY_RSA;
        c->rsa_n = n; c->rsa_nlen = nl; c->rsa_e = e; c->rsa_elen = el;
        return 0;
    }
    if (OIDEQ(oid, ol, O_EC)) {
        const u8 *curve;
        u32 cl;
        if (tlv(&q, qe, &tag, &curve, &cl) || tag != 0x06 || q != qe) return -1;
        if (OIDEQ(curve, cl, O_P256)) c->key_type = KEY_EC_P256;
        else if (OIDEQ(curve, cl, O_P384)) c->key_type = KEY_EC_P384;
        else return X509_E_SIGALG;
        l = c->key_type == KEY_EC_P256 ? 65 : 97;
        if (bl != l || bits[0] != 4) return -1;
        c->pub = bits; c->publen = bl;
        return 0;
    }
    if (OIDEQ(oid, ol, O_ED25519)) {
        if (q != qe || bl != 32) return -1;
        c->key_type = KEY_ED25519;
        c->pub = bits; c->publen = 32;
        return 0;
    }
    return X509_E_SIGALG;
}

static int parse_ext(struct x509 *c, const u8 *oid, u32 ol, int critical, const u8 *v, u32 vl)
{
    u8 tag;
    const u8 *p = v, *end = v + vl, *x;
    u32 xl;
    if (OIDEQ(oid, ol, O_BC)) {
        const u8 *seq;
        u32 sl;
        if (expect(&p, end, 0x30, &seq, &sl, NULL) || p != end) return -1;
        c->has_bc = 1; c->is_ca = 0; c->pathlen = -1;
        p = seq; end = seq + sl;
        if (p < end && *p == 0x01) {
            if (tlv(&p, end, &tag, &x, &xl) || xl != 1) return -1;
            c->is_ca = x[0] != 0;
        }
        if (p < end) {
            if (expect(&p, end, 0x02, &x, &xl, NULL) || xl < 1 || xl > 2 || (x[0] & 0x80)) return -1;
            c->pathlen = xl == 1 ? x[0] : (int)(x[0] << 8 | x[1]);
        }
        return p == end ? 0 : -1;
    }
    if (OIDEQ(oid, ol, O_KU)) {
        if (expect(&p, end, 0x03, &x, &xl, NULL) || p != end || xl < 2) return -1;
        c->has_ku = 1;
        c->ku = 0;
        for (int i = 0; i < 8; i++)                  /* bit i de la RFC : poids fort d'abord -> bit logique i */
            if (x[1] & (0x80 >> i)) c->ku |= 1u << i;
        if (xl > 2)
            for (int i = 0; i < 8; i++)
                if (x[2] & (0x80 >> i)) c->ku |= 1u << (8 + i);
        return 0;
    }
    if (OIDEQ(oid, ol, O_SAN)) {
        if (expect(&p, end, 0x30, &x, &xl, NULL) || p != end) return -1;
        c->san = x; c->sanlen = xl;
        return 0;
    }
    if (OIDEQ(oid, ol, O_EKU)) {
        const u8 *seq;
        u32 sl;
        if (expect(&p, end, 0x30, &seq, &sl, NULL) || p != end) return -1;
        c->has_eku = 1;
        p = seq; end = seq + sl;
        while (p < end) {
            if (expect(&p, end, 0x06, &x, &xl, NULL)) return -1;
            if (OIDEQ(x, xl, O_SERVERAUTH) || OIDEQ(x, xl, O_ANYEKU)) c->eku_server = 1;
        }
        return 0;
    }
    if (OIDEQ(oid, ol, O_SKI) || OIDEQ(oid, ol, O_AKI) || OIDEQ(oid, ol, O_POL) || OIDEQ(oid, ol, O_CRLDP) || OIDEQ(oid, ol, O_AIA) ||
        OIDEQ(oid, ol, O_SCT) || OIDEQ(oid, ol, O_IAN) || OIDEQ(oid, ol, O_POLC) || OIDEQ(oid, ol, O_INHIBIT) || OIDEQ(oid, ol, O_SIA) ||
        OIDEQ(oid, ol, O_FCRL) || OIDEQ(oid, ol, O_POLM) || OIDEQ(oid, ol, O_PRECERT_POISON))
        return 0;
    return critical ? X509_E_EXT : 0;                /* inconnue : ignoree si non critique */
}

int x509_parse(struct x509 *c, const u8 *der, u32 len)
{
    const u8 *p = der, *end = der + len, *cert, *tbs, *tbsstart, *q, *qe, *v, *sa1, *sa2, *bits, *start;
    u32 cl, tl, vl, sl1, sl2, bl;
    u8 tag;
    int r;
    memset(c, 0, sizeof *c);
    c->pathlen = -1;
    c->der = der; c->derlen = len;
    if (expect(&p, end, 0x30, &cert, &cl, NULL) || p != end) return X509_E_PARSE;
    p = cert; end = cert + cl;
    if (expect(&p, end, 0x30, &tbs, &tl, &tbsstart)) return X509_E_PARSE;
    c->tbs = tbsstart; c->tbslen = (u32)(p - tbsstart);
    if (expect(&p, end, 0x30, &sa2, &sl2, NULL)) return X509_E_PARSE;
    if (expect(&p, end, 0x03, &bits, &bl, NULL) || p != end || bl < 1 || bits[0] != 0) return X509_E_PARSE;
    c->sig = bits + 1; c->siglen = bl - 1;
    /* algorithme de signature */
    q = sa2; qe = sa2 + sl2;
    if (tlv(&q, qe, &tag, &v, &vl) || tag != 0x06) return X509_E_PARSE;
    c->sig_alg = sigalg_from(v, vl);
    /* contenu du tbs */
    q = tbs; qe = tbs + tl;
    if (q < qe && *q == 0xa0) {                      /* version */
        const u8 *vv;
        u32 vvl;
        if (tlv(&q, qe, &tag, &vv, &vvl)) return X509_E_PARSE;
    }
    if (expect(&q, qe, 0x02, &v, &vl, NULL)) return X509_E_PARSE;           /* numero de serie */
    if (expect(&q, qe, 0x30, &sa1, &sl1, NULL)) return X509_E_PARSE;        /* algorithme (doit egaler l'externe) */
    if (sl1 != sl2 || memcmp(sa1, sa2, sl1) != 0) return X509_E_PARSE;
    if (expect(&q, qe, 0x30, &v, &vl, &start)) return X509_E_PARSE;         /* emetteur */
    c->issuer = start; c->issuerlen = (u32)(q - start);
    {
        const u8 *vp = NULL;
        u32 vpl;
        if (expect(&q, qe, 0x30, &vp, &vpl, NULL)) return X509_E_PARSE;     /* validite */
        {
            const u8 *t = vp, *te = vp + vpl;
            if (parse_time(&t, te, &c->not_before) || parse_time(&t, te, &c->not_after) || t != te) return X509_E_PARSE;
        }
    }
    if (expect(&q, qe, 0x30, &v, &vl, &start)) return X509_E_PARSE;         /* sujet */
    c->subject = start; c->subjectlen = (u32)(q - start);
    if (expect(&q, qe, 0x30, &v, &vl, NULL)) return X509_E_PARSE;           /* cle publique */
    r = parse_spki(c, v, vl);
    if (r) return r < 0 && r != X509_E_SIGALG ? X509_E_PARSE : r;
    while (q < qe) {
        const u8 *ev;
        u32 evl;
        if (tlv(&q, qe, &tag, &ev, &evl)) return X509_E_PARSE;
        if (tag == 0xa3) {                                                   /* extensions */
            const u8 *seq = ev, *se = ev + evl, *ee;
            u32 el2;
            if (expect(&seq, se, 0x30, &ee, &el2, NULL) || seq != se) return X509_E_PARSE;
            seq = ee; se = ee + el2;
            while (seq < se) {
                const u8 *ext, *oid, *val;
                u32 extl, ol, valn;
                const u8 *e2;
                int critical = 0;
                if (expect(&seq, se, 0x30, &ext, &extl, NULL)) return X509_E_PARSE;
                e2 = ext;
                if (expect(&e2, ext + extl, 0x06, &oid, &ol, NULL)) return X509_E_PARSE;
                if (e2 < ext + extl && *e2 == 0x01) {
                    const u8 *bv;
                    u32 bvl;
                    if (tlv(&e2, ext + extl, &tag, &bv, &bvl) || bvl != 1) return X509_E_PARSE;
                    critical = bv[0] != 0;
                }
                if (expect(&e2, ext + extl, 0x04, &val, &valn, NULL) || e2 != ext + extl) return X509_E_PARSE;
                r = parse_ext(c, oid, ol, critical, val, valn);
                if (r) return r < 0 && r != X509_E_EXT ? X509_E_PARSE : r;
            }
        }
    }
    return X509_OK;
}

/* ---- nom d'hote ---- */
static int lower(int ch) { return ch >= 'A' && ch <= 'Z' ? ch + 32 : ch; }

static int dns_match(const u8 *pat, u32 pl, const char *host)
{
    u32 hl = (u32)strlen(host);
    if (pl == 0 || hl == 0) return 0;
    if (host[hl - 1] == '.') hl--;
    if (pl > 2 && pat[0] == '*' && pat[1] == '.') {
        u32 i = 0;
        while (i < hl && host[i] != '.') i++;                 /* premiere etiquette du nom */
        if (i == 0 || i >= hl) return 0;
        pat += 1; pl -= 1;                                    /* ".example.com" */
        host += i; hl -= i;
        for (u32 k = 1; k < pl; k++) if (pat[k] == '*') return 0;
    }
    if (pl != hl) return 0;
    for (u32 k = 0; k < pl; k++) {
        if (pat[k] == '*') return 0;
        if (lower(pat[k]) != lower((u8)host[k])) return 0;
    }
    return 1;
}

static int parse_ipv4(const char *s, u8 out[4])
{
    for (int i = 0; i < 4; i++) {
        u32 v = 0, digits = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (u32)(*s++ - '0'); digits++; if (v > 255) return -1; }
        if (!digits) return -1;
        out[i] = (u8)v;
        if (i < 3) { if (*s++ != '.') return -1; }
    }
    return *s == 0 ? 0 : -1;
}

int x509_check_host(const struct x509 *c, const char *host)
{
    const u8 *p, *end, *v;
    u32 vl;
    u8 tag, ip[4];
    int is_ip = parse_ipv4(host, ip) == 0;
    if (!c->san || !host || !*host) return -1;
    p = c->san; end = c->san + c->sanlen;
    while (p < end) {
        if (tlv(&p, end, &tag, &v, &vl)) return -1;
        if (tag == 0x82 && !is_ip && dns_match(v, vl, host)) return 0;
        if (tag == 0x87 && is_ip && vl == 4 && memcmp(v, ip, 4) == 0) return 0;
    }
    return -1;
}

/* ---- signatures ---- */
static int hash_of_sig(int alg)
{
    switch (alg) {
    case SIG_RSA_SHA256: case SIG_ECDSA_SHA256: return HASH_SHA256;
    case SIG_RSA_SHA384: case SIG_ECDSA_SHA384: return HASH_SHA384;
    case SIG_RSA_SHA512: case SIG_ECDSA_SHA512: return HASH_SHA512;
    }
    return -1;
}

int x509_check_sig(const struct x509 *c, const struct x509 *issuer)
{
    u8 h[HASH_MAX];
    hash_ctx hc;
    int ha;
    if (c->sig_alg == SIG_NONE) return X509_E_SIGALG;
    if (c->sig_alg == SIG_ED25519) {
        if (issuer->key_type != KEY_ED25519 || c->siglen != 64) return X509_E_SIG;
        return ed25519_verify(c->sig, c->tbs, c->tbslen, issuer->pub) == 0 ? X509_OK : X509_E_SIG;
    }
    ha = hash_of_sig(c->sig_alg);
    hash_init(&hc, ha); hash_update(&hc, c->tbs, c->tbslen); hash_final(&hc, h);
    if (c->sig_alg <= SIG_RSA_SHA512) {
        if (issuer->key_type != KEY_RSA) return X509_E_SIG;
        return rsa_verify_pkcs1(issuer->rsa_n, issuer->rsa_nlen, issuer->rsa_e, issuer->rsa_elen, ha, h, c->sig, c->siglen) == 0 ? X509_OK : X509_E_SIG;
    }
    if (issuer->key_type != KEY_EC_P256 && issuer->key_type != KEY_EC_P384) return X509_E_SIG;
    return ecdsa_verify_der(issuer->key_type == KEY_EC_P256 ? CURVE_P256 : CURVE_P384, issuer->pub, issuer->publen, h, hash_len(ha), c->sig, c->siglen) == 0 ? X509_OK : X509_E_SIG;
}

/* ---- chaine ---- */
static int same_name(const u8 *a, u32 al, const u8 *b, u32 bl) { return al == bl && memcmp(a, b, al) == 0; }

static int can_issue(const struct x509 *ca, int below)       /* below = nombre d'intermediaires entre ca et le certificat final */
{
    if (!ca->has_bc || !ca->is_ca) return 0;
    if (ca->has_ku && !(ca->ku & KU_KEY_CERT_SIGN)) return 0;
    if (ca->pathlen >= 0 && below > ca->pathlen) return 0;
    return 1;
}

static int time_ok(const struct x509 *c, u32 now)
{
    if (now < c->not_before) return X509_E_NOTYET;
    if (now > c->not_after) return X509_E_EXPIRED;
    return X509_OK;
}

int x509_verify_chain(const u8 *const *der, const u32 *len, int n, const char *host, u32 now)
{
    struct x509 leaf, cur, cand, root;
    int used[8] = {0}, depth = 0, r, last_err = X509_E_UNTRUSTED;
    if (n < 1 || n > 8) return X509_E_PARSE;
    if (now < x509_time_from_civil(2025, 1, 1, 0, 0, 0)) return X509_E_CLOCK;
    r = x509_parse(&leaf, der[0], len[0]);
    if (r) return r;
    if (x509_check_host(&leaf, host)) return X509_E_HOST;
    if (leaf.has_eku && !leaf.eku_server) return X509_E_KEYUSE;
    if (leaf.has_ku && !(leaf.ku & KU_DIGITAL_SIGNATURE)) return X509_E_KEYUSE;
    r = time_ok(&leaf, now);
    if (r) return r;
    cur = leaf;
    used[0] = 1;
    for (;;) {
        /* 1. une racine connue a-t-elle emis (ou est-elle) ce certificat ? */
        for (int i = 0; i < roots_n(); i++) {
            if (x509_parse(&root, root_der(i), root_len(i))) continue;
            if (!same_name(cur.issuer, cur.issuerlen, root.subject, root.subjectlen)) continue;
            if (time_ok(&root, now)) { last_err = X509_E_EXPIRED; continue; }
            if (!can_issue(&root, depth)) { last_err = X509_E_CA; continue; }
            if (x509_check_sig(&cur, &root) == X509_OK) return X509_OK;
            last_err = X509_E_SIG;
        }
        /* 2. sinon, un intermediaire envoye par le serveur */
        if (depth >= 6) return X509_E_UNTRUSTED;
        {
            int found = 0;
            for (int i = 1; i < n; i++) {
                if (used[i]) continue;
                if (x509_parse(&cand, der[i], len[i])) continue;
                if (!same_name(cur.issuer, cur.issuerlen, cand.subject, cand.subjectlen)) continue;
                if (time_ok(&cand, now)) { last_err = X509_E_EXPIRED; continue; }
                if (!can_issue(&cand, depth)) { last_err = X509_E_CA; continue; }
                if (x509_check_sig(&cur, &cand) != X509_OK) { last_err = X509_E_SIG; continue; }
                used[i] = 1;
                cur = cand;
                depth++;
                found = 1;
                break;
            }
            if (!found) return last_err;
        }
    }
}

const char *x509_strerror(int e)
{
    switch (e) {
    case X509_OK: return "ok";
    case X509_E_PARSE: return "certificat mal forme";
    case X509_E_SIGALG: return "algorithme non pris en charge ou trop faible";
    case X509_E_SIG: return "signature de certificat fausse";
    case X509_E_EXPIRED: return "certificat expire";
    case X509_E_NOTYET: return "certificat pas encore valide";
    case X509_E_HOST: return "le nom du serveur ne correspond pas au certificat";
    case X509_E_UNTRUSTED: return "autorite de certification inconnue";
    case X509_E_CA: return "un certificat de la chaine n'est pas une autorite";
    case X509_E_EXT: return "extension critique inconnue";
    case X509_E_CLOCK: return "horloge du systeme incorrecte";
    case X509_E_KEYUSE: return "certificat non destine a un serveur TLS";
    }
    return "erreur de certificat inconnue";
}
