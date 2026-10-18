#ifndef PHOENOS_NETDEV_H
#define PHOENOS_NETDEV_H

#include "types.h"
#include "error.h"

/*
 * Interface reseau (0.9.0) : ce que la pile reseau (net.h) attend d'un pilote, filaire ou (plus tard) sans fil.
 * Le pilote remplit la structure et l'enregistre ; la pile n'a jamais besoin de savoir de quelle carte il s'agit.
 */
#define NETDEV_MAX   4
#define NET_MTU      1500u
#define NET_FRAME_MAX 1600u             /* trame Ethernet complete (14 + 1500 + marge) */

struct netdev {
    const char *name;                   /* "eth0" */
    const char *driver;                 /* "e1000" */
    u8   mac[6];
    int  (*link_up)(struct netdev *d);                          /* cable branche / associe ? */
    err_t (*send)(struct netdev *d, const u8 *frame, u32 len);  /* trame Ethernet complete, sans CRC */
    int  (*recv)(struct netdev *d, u8 *buf, u32 cap);           /* une trame recue (longueur) ou 0 s'il n'y en a pas */
    void *priv;
    u32  tx_frames, rx_frames, tx_bytes, rx_bytes, tx_errors, rx_dropped;
};

/* E_BUSY : table pleine. Le nom est attribue par le noyau (eth0, eth1...). */
err_t netdev_register(struct netdev *d);
int   netdev_count(void);
struct netdev *netdev_get(int i);
void  netdev_reset(void);               /* tests */

#endif
