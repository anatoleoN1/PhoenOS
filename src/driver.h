#ifndef PHOENOS_DRIVER_H
#define PHOENOS_DRIVER_H

#include "types.h"
#include "error.h"
#include "pci.h"

/*
 * Cadre des pilotes (0.9.0). Un pilote dit quels peripheriques PCI il sait piloter (vendeur + modele) et fournit une fonction
 * d'attachement. Au demarrage, drivers_attach_all() propose chaque peripherique detecte a chaque pilote connu.
 *   - pilotes OFFICIELS : ecrits et publies par PhoenOS (champ official = 1), integres au noyau ;
 *   - pilotes de la communaute : a leurs risques et perils (voir la licence, article 5) ; ils ne sont pas encore
 *     chargeables (prevu plus tard) mais passeront par ce meme cadre.
 * Un pilote par version : 0.9.0 = e1000 (reseau filaire Intel). Le pilote sans fil viendra apres.
 */
struct pci_id { u16 vendor, device; };

struct driver {
    const char *name;                   /* "e1000" */
    const char *version;
    const char *summary;                /* "Carte reseau Intel PRO/1000" */
    const char *kind;                   /* "ethernet", plus tard "wifi", "graphique" */
    int official;
    const struct pci_id *ids;
    int nids;
    err_t (*attach)(const struct pci_dev *d);
};

struct driver_binding {
    const struct driver *drv;
    int dev;                            /* indice dans pci_get() */
    err_t status;                       /* E_OK : en service ; sinon la raison de l'echec */
};

#define DRIVERS_MAX_BINDINGS 16

/* Pilotes connus (ceux du noyau). */
int  drivers_count(void);
const struct driver *drivers_get(int i);

/* Propose chaque peripherique PCI a chaque pilote connu ; renvoie le nombre de pilotes attaches avec succes. */
int  drivers_attach_all(void);

int  drivers_bindings(void);
const struct driver_binding *drivers_binding(int i);

/* Pilote attache a ce peripherique (NULL : aucun ou echec). */
const struct driver *drivers_for_dev(int dev);

#endif
