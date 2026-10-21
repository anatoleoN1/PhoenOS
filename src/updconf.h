#ifndef PHOENOS_UPDCONF_H
#define PHOENOS_UPDCONF_H

/*
 * Reglages des mises a jour (0.10.4). Source : les releases GitHub du depot ci-dessous.
 *   manifeste : https://<HOST>/<REPO>/releases/latest/download/manifest-<edition>.txt  (+ .sig)
 *   fichiers  : https://<HOST>/<REPO>/releases/download/<version>/<fichier du manifeste>
 */
#define UPDATE_HOST        "github.com"
#define UPDATE_REPO        "anatoleoN1/PhoenOS"

#define UPDATE_MAX_TRIES   2u           /* essais de demarrage d'une nouvelle version avant retour arriere */
#define UPDATE_CONFIRM_S   30u          /* secondes de fonctionnement avant de declarer une version "bonne" */
#define UPDATE_FIRST_CHECK_S 90u        /* premiere verification automatique apres le demarrage */
#define UPDATE_CHECK_EVERY_S (6u * 3600u)

#endif
