/*
 * calc : calculatrice. Le premier vrai programme de PhoenOS.
 *
 *   calc "2+3*4"        calcule une expression et affiche le resultat (pratique dans un tube)
 *   calc                mode interactif : une expression par ligne, "q" pour quitter
 *
 * Operateurs : + - * / % ^ (puissance entiere), parentheses, moins unaire.
 * Fonctions : sqrt(x) abs(x).  Constantes : pi, ans (dernier resultat).
 * Nombres decimaux avec "." ou "," ; 4 decimales ; valeurs jusqu'a 100 000 000 000.
 * Calcul en virgule fixe (pas de flottants : le processeur n'est pas prepare pour eux ici).
 */
#include "phoenos.h"

typedef long long i64;
typedef unsigned long long u64;

#define SCALE   10000LL                 /* 4 decimales */
#define LIMIT   1000000000000000LL      /* |valeur brute| maximale : 10^11 en vrai */

static const char *src;                 /* texte en cours d'analyse */
static const char *err;                 /* message d'erreur (NULL = tout va bien) */
static i64 last_result;

static void fail(const char *msg) { if (!err) err = msg; }

static i64 check(i64 v)
{
    if (v > LIMIT || v < -LIMIT) { fail("resultat trop grand (maximum 100000000000)"); return 0; }
    return v;
}

static void skip(void) { while (*src == ' ' || *src == '\t') src++; }

/* Division 64 bits par decalages : le programme n'a pas de libgcc (pas de division 64 bits toute faite). */
static u64 udivmod(u64 a, u64 b, u64 *rem)
{
    u64 q = 0, r = 0;

    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1);
        if (r >= b) { r -= b; q |= 1ULL << i; }
    }
    if (rem) *rem = r;
    return q;
}
static u64 udiv(u64 a, u64 b) { return udivmod(a, b, 0); }
static u64 umod(u64 a, u64 b) { u64 r; udivmod(a, b, &r); return r; }

static u64 mag(i64 v) { return v < 0 ? (u64)(-v) : (u64)v; }

static i64 mul(i64 a, i64 b)
{
    u64 ua = mag(a), ub = mag(b), r;
    u64 ah = udiv(ua, SCALE), al = umod(ua, SCALE);
    int neg = (a < 0) != (b < 0);

    if (ub && ah > udiv((u64)LIMIT, ub)) { fail("resultat trop grand (maximum 100000000000)"); return 0; }
    r = ah * ub + udiv(al * ub, SCALE);                    /* (ah*S + al) * b / S, sans depassement */
    if (r > (u64)LIMIT) { fail("resultat trop grand (maximum 100000000000)"); return 0; }
    return neg ? -(i64)r : (i64)r;
}

static i64 divide(i64 a, i64 b)
{
    u64 r;

    if (b == 0) { fail("division par zero"); return 0; }
    r = udiv(mag(a) * SCALE, mag(b));
    if (r > (u64)LIMIT) { fail("resultat trop grand (maximum 100000000000)"); return 0; }
    return ((a < 0) != (b < 0)) ? -(i64)r : (i64)r;
}

static u64 isqrt(u64 n)
{
    u64 bit = 1ULL << 62, res = 0;

    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= res + bit) { n -= res + bit; res = (res >> 1) + bit; }
        else res >>= 1;
        bit >>= 2;
    }
    return res;
}

static i64 expr(void);

static i64 number(void)
{
    i64 whole = 0, frac = 0, div = SCALE;
    int digits = 0;

    while (*src >= '0' && *src <= '9') {
        whole = whole * 10 + (*src++ - '0');
        digits++;
        if (whole > LIMIT / SCALE) { fail("nombre trop grand (maximum 100000000000)"); return 0; }
    }
    if (*src == '.' || *src == ',') {
        src++;
        while (*src >= '0' && *src <= '9') {
            if (div > 1) { div = (i64)udiv((u64)div, 10); frac += (*src - '0') * div; }     /* decimales au-dela de 4 : ignorees */
            src++;
            digits++;
        }
    }
    if (!digits) { fail("nombre attendu"); return 0; }
    return whole * SCALE + frac;
}

static int word(const char *w)
{
    uint n = slen(w);

    for (uint i = 0; i < n; i++)
        if (src[i] != w[i]) return 0;
    if ((src[n] >= 'a' && src[n] <= 'z') || (src[n] >= '0' && src[n] <= '9')) return 0;
    src += n;
    return 1;
}

static i64 atom(void)
{
    i64 v;

    skip();
    if (*src == '(') {
        src++;
        v = expr();
        skip();
        if (*src != ')') { fail("parenthese fermante attendue"); return 0; }
        src++;
        return v;
    }
    if ((*src >= '0' && *src <= '9') || *src == '.' || *src == ',')
        return number();
    if (word("pi")) return 31416;
    if (word("ans")) return last_result;
    if (word("sqrt") || word("abs")) {
        int is_sqrt = (src[-1] == 't');
        skip();
        if (*src != '(') { fail("parenthese ouvrante attendue apres le nom de la fonction"); return 0; }
        src++;
        v = expr();
        skip();
        if (*src != ')') { fail("parenthese fermante attendue"); return 0; }
        src++;
        if (is_sqrt) {
            if (v < 0) { fail("racine carree d'un nombre negatif"); return 0; }
            return (i64)isqrt((u64)v * SCALE);
        }
        return v < 0 ? -v : v;
    }
    fail(*src ? "expression non comprise" : "expression incomplete");
    return 0;
}

static i64 unary(void);

static i64 power(void)
{
    i64 base = atom(), e, r = SCALE;
    int neg;

    skip();
    if (*src != '^' || err)
        return base;
    src++;
    e = unary();
    if (err) return 0;
    if (umod(mag(e), SCALE) != 0) { fail("l'exposant doit etre un entier"); return 0; }
    e = (e < 0) ? -(i64)udiv(mag(e), SCALE) : (i64)udiv((u64)e, SCALE);
    neg = e < 0;
    if (neg) e = -e;
    if (e > 64) { fail("exposant trop grand (maximum 64)"); return 0; }
    for (i64 i = 0; i < e && !err; i++)
        r = mul(r, base);
    return neg ? divide(SCALE, r) : r;
}

static i64 unary(void)
{
    skip();
    if (*src == '-') { src++; return -unary(); }
    if (*src == '+') { src++; return unary(); }
    return power();
}

static i64 term(void)
{
    i64 v = unary();

    for (;;) {
        skip();
        if (err) return 0;
        if (*src == '*') { src++; v = mul(v, unary()); }
        else if (*src == '/') { src++; v = divide(v, unary()); }
        else if (*src == '%') {
            i64 b;
            src++;
            b = unary();
            if (b == 0) { fail("division par zero"); return 0; }
            v = (v < 0) ? -(i64)umod(mag(v), mag(b)) : (i64)umod((u64)v, mag(b));
        } else return v;
    }
}

static i64 expr(void)
{
    i64 v = term();

    for (;;) {
        skip();
        if (err) return 0;
        if (*src == '+') { src++; v = check(v + term()); }
        else if (*src == '-') { src++; v = check(v - term()); }
        else return v;
    }
}

/* Evalue `text`. Renvoie 0 et remplit *out, ou -1 (message dans err). */
static int evaluate(const char *text, i64 *out)
{
    i64 v;

    src = text;
    err = 0;
    v = expr();
    skip();
    if (!err && *src) fail(*src == ')' ? "parenthese ouvrante manquante" : "expression non comprise");
    if (err) return -1;
    *out = v;
    return 0;
}

static void print_value(i64 v, int fd)
{
    char b[32];
    int i = 31;
    u64 m = mag(v);
    uint frac = (uint)umod(m, SCALE);
    char digits[24];
    int nd = 0;
    u64 whole = udiv(m, SCALE);

    do { digits[nd++] = (char)('0' + (int)umod(whole, 10)); whole = udiv(whole, 10); } while (whole);
    b[i] = 0;
    if (frac) {
        int len = 4;
        while (len && frac % 10 == 0) { frac /= 10; len--; }
        for (int k = 0; k < len; k++) { b[--i] = (char)('0' + frac % 10); frac /= 10; }
        b[--i] = '.';
    }
    for (int k = 0; k < nd; k++) b[--i] = digits[k];      /* chiffre de poids faible en dernier */
    if (v < 0) b[--i] = '-';
    puts_fd(fd, b + i);
}

/* Une ligne depuis l'entree standard (le noyau en rend une par lecture, un tube peut en rendre plusieurs). */
static int get_line(char *dst, int max)
{
    static char buf[512];
    static int have, pos;
    int n = 0;

    for (;;) {
        while (pos < have) {
            char c = buf[pos++];

            if (c == '\n') { dst[n] = 0; return n; }
            if (n < max - 1) dst[n++] = c;
        }
        pos = have = 0;
        have = read(0, buf, sizeof buf);
        if (have <= 0) {
            have = 0;
            dst[n] = 0;
            return n ? n : -1;                          /* fin de fichier */
        }
    }
}

int main(int argc, char **argv)
{
    char line[256];
    i64 v;

    if (argc > 1) {                                     /* une seule expression : arguments mis bout a bout */
        uint n = 0;

        for (int i = 1; i < argc; i++) {
            for (uint k = 0; argv[i][k] && n < sizeof line - 2; k++) line[n++] = argv[i][k];
            if (n < sizeof line - 2) line[n++] = ' ';
        }
        line[n] = 0;
        if (evaluate(line, &v) < 0) {
            puts_fd(2, "calc : "); puts_fd(2, err); puts_fd(2, "\n");
            return 1;
        }
        print_value(v, 1);
        puts("\n");
        return 0;
    }

    puts("Calculatrice PhoenOS - une expression par ligne, q pour quitter.\n");
    puts("  exemples : 2+3*4   (1+2)^3   sqrt(2)   10/4   ans*2   pi\n");
    for (;;) {
        int n;

        puts("calc> ");
        n = get_line(line, sizeof line);
        if (n < 0) { puts("\n"); return 0; }
        while (n > 0 && line[n - 1] == '\r') n--;
        line[n] = 0;
        if (streq(line, "q") || streq(line, "quit") || streq(line, "exit")) return 0;
        if (!line[0]) continue;
        if (evaluate(line, &v) < 0) {
            puts("  erreur : "); puts(err); puts("\n");
        } else {
            last_result = v;
            puts("  = "); print_value(v, 1); puts("\n");
        }
    }
}
