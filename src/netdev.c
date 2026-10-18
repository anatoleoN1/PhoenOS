/* PhoenOS - netdev.c : table des interfaces reseau (voir netdev.h). */
#include "netdev.h"
#include "string.h"

static struct netdev *devs[NETDEV_MAX];
static int ndevs;
static char names[NETDEV_MAX][8];

err_t netdev_register(struct netdev *d)
{
    if (ndevs >= NETDEV_MAX)
        return E_BUSY;
    strcpy(names[ndevs], "eth0");
    names[ndevs][3] = (char)('0' + ndevs);
    d->name = names[ndevs];
    devs[ndevs++] = d;
    return E_OK;
}

int netdev_count(void) { return ndevs; }

struct netdev *netdev_get(int i)
{
    return i >= 0 && i < ndevs ? devs[i] : NULL;
}

void netdev_reset(void)
{
    ndevs = 0;
}
