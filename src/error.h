#ifndef PHOENOS_ERROR_H
#define PHOENOS_ERROR_H

/*
 * Codes d'erreur du noyau. Convention : les fonctions qui peuvent échouer
 * renvoient un err_t (E_OK = 0, négatif = erreur). L'appelant doit tester.
 */
typedef int err_t;

enum {
    E_OK      =  0,
    E_NOMEM   = -1,   /* plus de mémoire              */
    E_INVAL   = -2,   /* paramètre invalide           */
    E_NOENT   = -3,   /* élément introuvable          */
    E_IO      = -4,   /* erreur d'entrée/sortie       */
    E_BUSY    = -5,   /* ressource déjà utilisée      */
    E_TIMEOUT = -6,   /* délai dépassé                */
    E_NOSYS   = -7,   /* non implémenté               */
    E_2BIG    = -8,   /* trop d'arguments             */
    E_REPORTED = -9,  /* erreur déjà expliquée à l'écran */
    E_EXIST   = -10,  /* existe déjà                  */
    E_NOTDIR  = -11,  /* n'est pas un dossier         */
    E_ISDIR   = -12,  /* est un dossier               */
    E_NOTEMPTY = -13, /* dossier non vide             */
    E_NAMETOOLONG = -14, /* nom ou chemin trop long   */
    E_FBIG    = -15   /* fichier trop grand           */
};

static inline const char *err_str(err_t e)
{
    switch (e) {
    case E_OK:      return "succes";
    case E_NOMEM:   return "memoire insuffisante";
    case E_INVAL:   return "parametre invalide";
    case E_NOENT:   return "introuvable";
    case E_IO:      return "erreur d'entree/sortie";
    case E_BUSY:    return "ressource occupee";
    case E_TIMEOUT: return "delai depasse";
    case E_NOSYS:   return "non implemente";
    case E_2BIG:    return "trop d'arguments";
    case E_REPORTED: return "deja signale";
    case E_EXIST:   return "existe deja";
    case E_NOTDIR:  return "n'est pas un dossier";
    case E_ISDIR:   return "est un dossier";
    case E_NOTEMPTY: return "dossier non vide";
    case E_NAMETOOLONG: return "nom ou chemin trop long";
    case E_FBIG:    return "fichier trop grand";
    default:        return "erreur inconnue";
    }
}

/* Propage l'erreur à l'appelant : TRY(fonction_qui_peut_echouer()); */
#define TRY(expr) \
    do { err_t _e = (expr); if (_e != E_OK) return _e; } while (0)

#endif
