/*
 * PhoenOS - crc32.c
 * Somme de contrôle des données écrites sur disque (détecte les secteurs abîmés
 * ou une écriture interrompue).
 */
#include "crc32.h"

u32 crc32_update(u32 crc, const void *data, usize len)
{
    const u8 *p = data;

    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
