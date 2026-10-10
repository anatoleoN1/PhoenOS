#ifndef PHOENOS_CRC32_H
#define PHOENOS_CRC32_H

#include "types.h"

/*
 * CRC-32 (le même que zlib / gzip / PNG). Pour un calcul en plusieurs morceaux :
 *   crc = crc32_update(0, a, na); crc = crc32_update(crc, b, nb);
 * Vérification : crc32_update(0, "123456789", 9) == 0xCBF43926.
 */
u32 crc32_update(u32 crc, const void *data, usize len);

#endif
