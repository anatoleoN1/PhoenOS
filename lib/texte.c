/* PhoenOS - texte.psl : fonctions sur les textes (exemple de bibliotheque partagee). Base 0x710000. */
typedef unsigned int uint;

uint texte_longueur(const char *s)
{
    uint n = 0;

    while (s[n])
        n++;
    return n;
}

void texte_maj(char *s)
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z')
            *s = (char)(*s - 32);
}

void texte_inverser(char *s)
{
    uint n = texte_longueur(s);

    for (uint i = 0; i < n / 2; i++) {
        char t = s[i];

        s[i] = s[n - 1 - i];
        s[n - 1 - i] = t;
    }
}

uint texte_mots(const char *s)
{
    uint m = 0;
    int dans = 0;

    for (; *s; s++) {
        int espace = (*s == ' ' || *s == '\t' || *s == '\n');

        if (!espace && !dans)
            m++;
        dans = !espace;
    }
    return m;
}
