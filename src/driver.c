/* PhoenOS - driver.c : cadre des pilotes (voir driver.h). */
#include "driver.h"
#include "dlog.h"
#include "string.h"

extern const struct driver e1000_driver;

static const struct driver *const known[] = { &e1000_driver };
#define NKNOWN ((int)(sizeof known / sizeof known[0]))

static struct driver_binding binds[DRIVERS_MAX_BINDINGS];
static int nbinds;

int drivers_count(void) { return NKNOWN; }

const struct driver *drivers_get(int i)
{
    return i >= 0 && i < NKNOWN ? known[i] : NULL;
}

static int matches(const struct driver *d, const struct pci_dev *p)
{
    for (int i = 0; i < d->nids; i++)
        if (d->ids[i].vendor == p->vendor && d->ids[i].device == p->device)
            return 1;
    return 0;
}

int drivers_attach_all(void)
{
    int ok = 0;

    nbinds = 0;
    for (int i = 0; i < pci_count(); i++) {
        const struct pci_dev *p = pci_get(i);

        for (int k = 0; k < NKNOWN && nbinds < DRIVERS_MAX_BINDINGS; k++) {
            err_t e;

            if (!matches(known[k], p))
                continue;
            e = known[k]->attach(p);
            binds[nbinds].drv = known[k];
            binds[nbinds].dev = i;
            binds[nbinds].status = e;
            nbinds++;
            if (e == E_OK) {
                ok++;
                DINFO("drv", "pilote %s attache a %02x:%02x.%u (%04x:%04x)", known[k]->name, p->bus, p->dev, p->fn, p->vendor, p->device);
            } else {
                DWARN("drv", "pilote %s : echec sur %02x:%02x.%u (%s)", known[k]->name, p->bus, p->dev, p->fn, err_str(e));
            }
            break;                                      /* un seul pilote par peripherique */
        }
    }
    return ok;
}

int drivers_bindings(void) { return nbinds; }

const struct driver_binding *drivers_binding(int i)
{
    return i >= 0 && i < nbinds ? &binds[i] : NULL;
}

const struct driver *drivers_for_dev(int dev)
{
    for (int i = 0; i < nbinds; i++)
        if (binds[i].dev == dev && binds[i].status == E_OK)
            return binds[i].drv;
    return NULL;
}
