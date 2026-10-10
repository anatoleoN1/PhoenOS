#ifndef PHOENOS_ATA_H
#define PHOENOS_ATA_H

#include "types.h"
#include "error.h"

/*
 * Pilote de disques ATA/IDE en mode PIO (le plus simple : le processeur copie lui-même
 * chaque secteur de 512 octets). Quatre emplacements sont explorés : maître et esclave du
 * canal primaire et du canal secondaire. Les lecteurs de CD (ATAPI) sont détectés mais pas utilisés.
 * Limite : adressage LBA28, soit 128 Gio par disque.
 */
#define ATA_SECTOR 512

struct ata_dev {
    u8   present;           /* 1 si un périphérique répond            */
    u8   atapi;             /* 1 : lecteur de CD/DVD (non utilisable) */
    u8   channel;           /* 0 = primaire, 1 = secondaire           */
    u8   drive;             /* 0 = maître, 1 = esclave                */
    u16  io, ctrl;          /* ports d'entrée-sortie du canal         */
    u32  sectors;           /* capacité en secteurs de 512 octets     */
    char model[41];         /* nom donné par le constructeur          */
};

/* Détecte les périphériques (à appeler une fois au démarrage ; peut être rappelé). */
void ata_init(void);

/* Nombre d'emplacements occupés, et accès à l'un d'eux (0 <= i < ata_count()). */
int ata_count(void);
const struct ata_dev *ata_dev(int i);

/* Premier vrai disque (pas un lecteur de CD), ou NULL. */
const struct ata_dev *ata_first_disk(void);

/* Lit / écrit `count` secteurs à partir du secteur `lba`. E_INVAL hors du disque, E_IO si le disque échoue. */
err_t ata_read(const struct ata_dev *d, u32 lba, u32 count, void *buf);
err_t ata_write(const struct ata_dev *d, u32 lba, u32 count, const void *buf);

/* Force l'écriture du cache du disque sur le support. */
err_t ata_flush(const struct ata_dev *d);

#endif
