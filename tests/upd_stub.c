/* Remplace update.c dans les tests du shell (le vrai est teste par maj-complet). */
#include "update.h"
err_t update_command(int argc, char **argv) { (void)argc; (void)argv; return E_OK; }
const char *update_take_notice(void) { return 0; }
int update_relaunched(void) { return 0; }
