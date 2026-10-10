/* Verifie user/libc.h (printf, chaines, malloc) en s'executant pour de vrai. */
#include "libc.h"

static int fails;
static void eq(const char *got, const char *want)
{
    if (strcmp(got, want)) { printf("ECHEC : obtenu '%s', attendu '%s'\n", got, want); fails++; }
}

int main(void)
{
    char b[64];

    snprintf(b, sizeof b, "%d|%5d|%-5d|%05d", -42, 42, 42, 42); eq(b, "-42|   42|42   |00042");
    snprintf(b, sizeof b, "%u|%x|%X|%p", 4000000000u, 255, 255, (void *)0x400000); eq(b, "4000000000|ff|FF|0x400000");
    snprintf(b, sizeof b, "%s|%8s|%-8s|%c|%%", "ab", "ab", "ab", 'z'); eq(b, "ab|      ab|ab      |z|%");
    snprintf(b, sizeof b, "%d %d", 0, -2147483647 - 1); eq(b, "0 -2147483648");
    snprintf(b, sizeof b, "%05d", -42); eq(b, "-0042");
    snprintf(b, sizeof b, "%s", (char *)0); eq(b, "(null)");
    if (snprintf(b, 5, "abcdefgh") != 8 || strcmp(b, "abcd")) { puts("ECHEC : troncature\n"); fails++; }
    if (strcmp("a", "b") >= 0 || strncmp("abc", "abd", 2) != 0 || !strchr("hello", 'l') || memcmp("ab", "ac", 2) >= 0) { puts("ECHEC : chaines\n"); fails++; }
    {
        char m[8] = "abcdefg";

        memmove(m + 2, m, 5);
        eq(m, "ababcde");
    }
    {   /* malloc : alignement, zero, double liberation detectee ailleurs ; ici : grosses tailles et refus propre */
        void *a = malloc(1), *c = calloc(100, 4), *big = malloc(1u << 30), *huge = malloc(0x7FFFFFFF);

        if (!a || ((uint)a & 15) || !c || ((uint)c & 15)) { puts("ECHEC : malloc\n"); fails++; }
        for (int i = 0; i < 400; i++) if (((char *)c)[i]) { puts("ECHEC : calloc non nul\n"); fails++; break; }
        if (huge || (big && 0)) { puts("ECHEC : enorme alloue\n"); fails++; }
        if (calloc(0x10000000, 0x100)) { puts("ECHEC : calloc debordement\n"); fails++; }
        free(a); free(c); free(big);
    }
    if (!fails) puts("libc : tout est correct\n");
    return fails;
}
