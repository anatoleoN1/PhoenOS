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
    E_NOSYS   = -7    /* non implémenté               */
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
    default:        return "erreur inconnue";
    }
}

/* Propage l'erreur à l'appelant : TRY(fonction_qui_peut_echouer()); */
#define TRY(expr) \
    do { err_t _e = (expr); if (_e != E_OK) return _e; } while (0)

#endif
