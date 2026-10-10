/*
 * tests/fakeata.h - faux contrôleur IDE pour les tests.
 *
 * Simule, au niveau des ports d'entrée-sortie, ce que fait un vrai disque ATA :
 *   - canal primaire, maître : disque de `fake_disk_sectors` secteurs (contenu dans fake_disk[]) ;
 *   - canal secondaire, maître : lecteur de CD (ATAPI) ;
 *   - esclaves : absents (l'état lu vaut 0) ; si fake_no_hardware : tout le bus « flotte » (0xFF).
 * Pannes simulables : fake_dead_after_writes (coupure de courant après N secteurs écrits),
 * fake_fail_reads / fake_fail_writes (le disque signale une erreur).
 * Attention : c'est notre COMPRÉHENSION du protocole ATA, pas du vrai matériel.
 */
#ifndef TEST_FAKEATA_H
#define TEST_FAKEATA_H
#define TEST_HAS_PORTS

#include "types.h"

#define FAKE_MAX_SECTORS 40000u

extern u8 test_scancode;
static u8 fake_disk[FAKE_MAX_SECTORS * 512];
static u32 fake_disk_sectors = 32768;           /* 16 Mio */
static int fake_no_hardware;
static int fake_dead;                           /* après la coupure : plus rien n'est écrit, les écritures échouent */
static long fake_dead_after_writes = -1;        /* -1 : jamais */
static int fake_fail_reads, fake_fail_writes;
static u32 fake_sector_writes;                  /* nombre total de secteurs écrits */
static u32 fake_flushes;

/* état interne */
static u8  f_sel[2];                            /* disque choisi par canal (0/1) */
static u8  f_lba0[2], f_lba1[2], f_lba2[2], f_lba3[2], f_cnt[2];
static u8  f_status[2];
static u8  f_error[2];
static u16 f_buf[2][256];
static int f_pos[2];
static int f_mode[2];                           /* 0 repos, 1 lecture de données, 2 réception de données, 3 identify */
static u32 f_cur_lba[2];
static u8  f_sig_mid[2], f_sig_hi[2];

static const char FAKE_MODEL[] = "FAKE DISK 9000";

static void fake_identify(int ch)
{
    for (int i = 0; i < 256; i++)
        f_buf[ch][i] = 0;
    f_buf[ch][60] = (u16)(fake_disk_sectors & 0xFFFF);
    f_buf[ch][61] = (u16)(fake_disk_sectors >> 16);
    for (int i = 0; i < 20; i++) {                  /* modèle : deux caractères par mot, octet de poids fort d'abord */
        char a = (2 * i < (int)sizeof(FAKE_MODEL) - 1) ? FAKE_MODEL[2 * i] : ' ';
        char b = (2 * i + 1 < (int)sizeof(FAKE_MODEL) - 1) ? FAKE_MODEL[2 * i + 1] : ' ';

        f_buf[ch][27 + i] = (u16)(((u8)a << 8) | (u8)b);
    }
    f_pos[ch] = 0;
    f_mode[ch] = 1;
    f_status[ch] = 0x50 | 0x08;                     /* DRDY + DSC + DRQ */
}

static int fake_present(int ch, int drv)
{
    (void)ch;
    return drv == 0;                                /* maîtres seulement */
}

static void fake_command(int ch, u8 cmd)
{
    int drv = f_sel[ch];
    u32 lba = ((u32)f_lba3[ch] << 24) | ((u32)f_lba2[ch] << 16) | ((u32)f_lba1[ch] << 8) | f_lba0[ch];

    f_error[ch] = 0;
    if (fake_no_hardware || !fake_present(ch, drv)) {
        f_status[ch] = fake_no_hardware ? 0xFF : 0;
        return;
    }
    if (ch == 1) {                                  /* lecteur de CD : IDENTIFY ATA est refusé, signature ATAPI */
        f_status[ch] = 0x51;                        /* DRDY + ERR */
        f_sig_mid[ch] = 0x14;
        f_sig_hi[ch] = 0xEB;
        return;
    }
    switch (cmd) {
    case 0xEC:
        fake_identify(ch);
        break;
    case 0x20:                                      /* lecture d'un secteur */
        if (fake_fail_reads || lba >= fake_disk_sectors) {
            f_status[ch] = 0x51;
            f_error[ch] = 0x04;
            break;
        }
        for (int i = 0; i < 256; i++)
            f_buf[ch][i] = (u16)(fake_disk[lba * 512 + 2 * i] | (fake_disk[lba * 512 + 2 * i + 1] << 8));
        f_pos[ch] = 0;
        f_mode[ch] = 1;
        f_status[ch] = 0x58;
        break;
    case 0x30:                                      /* écriture d'un secteur : attend 256 mots */
        if (fake_fail_writes || fake_dead || lba >= fake_disk_sectors) {
            f_status[ch] = 0x51;
            break;
        }
        f_cur_lba[ch] = lba;
        f_pos[ch] = 0;
        f_mode[ch] = 2;
        f_status[ch] = 0x58;
        break;
    case 0xE7:
        fake_flushes++;
        f_status[ch] = fake_dead ? 0x51 : 0x50;
        break;
    default:
        f_status[ch] = 0x51;
        break;
    }
}

u32 test_port_in(u16 port, int size)
{
    int ch, reg;

    if (port == 0x60)
        return test_scancode;
    if (port >= 0x1F0 && port <= 0x1F7) { ch = 0; reg = port - 0x1F0; }
    else if (port == 0x3F6)              { ch = 0; reg = 8; }
    else if (port >= 0x170 && port <= 0x177) { ch = 1; reg = port - 0x170; }
    else if (port == 0x376)              { ch = 1; reg = 8; }
    else return 0;

    if (fake_no_hardware)
        return 0xFF;
    switch (reg) {
    case 0:                                         /* données */
        if (f_mode[ch] == 1 && f_pos[ch] < 256) {
            u16 v = f_buf[ch][f_pos[ch]++];

            if (f_pos[ch] == 256) { f_mode[ch] = 0; f_status[ch] = 0x50; }
            return v;
        }
        return 0xFFFF;
    case 1: return f_error[ch];
    case 2: return f_cnt[ch];
    case 3: return f_lba0[ch];
    case 4: return f_status[ch] == 0x51 && ch == 1 ? f_sig_mid[ch] : (u32)f_lba1[ch];
    case 5: return f_status[ch] == 0x51 && ch == 1 ? f_sig_hi[ch] : (u32)f_lba2[ch];
    case 6: return 0xA0 | (f_sel[ch] << 4);
    case 7:
    case 8:
        if (!fake_present(ch, f_sel[ch]))
            return 0;
        (void)size;
        return f_status[ch];
    }
    return 0;
}

void test_port_out(u16 port, u32 v, int size)
{
    int ch, reg;

    (void)size;
    if (port >= 0x1F0 && port <= 0x1F7) { ch = 0; reg = port - 0x1F0; }
    else if (port >= 0x170 && port <= 0x177) { ch = 1; reg = port - 0x170; }
    else return;

    switch (reg) {
    case 0:                                         /* données écrites */
        if (f_mode[ch] == 2 && f_pos[ch] < 256) {
            f_buf[ch][f_pos[ch]++] = (u16)v;
            if (f_pos[ch] == 256) {
                f_mode[ch] = 0;
                if (fake_dead_after_writes == 0)
                    fake_dead = 1;
                if (fake_dead) {
                    f_status[ch] = 0x51;            /* coupure : l'écriture est perdue */
                } else {
                    u32 lba = f_cur_lba[ch];

                    for (int i = 0; i < 256; i++) {
                        fake_disk[lba * 512 + 2 * i]     = (u8)(f_buf[ch][i] & 0xFF);
                        fake_disk[lba * 512 + 2 * i + 1] = (u8)(f_buf[ch][i] >> 8);
                    }
                    fake_sector_writes++;
                    if (fake_dead_after_writes > 0)
                        fake_dead_after_writes--;
                    f_status[ch] = 0x50;
                }
            }
        }
        break;
    case 2: f_cnt[ch] = (u8)v; break;
    case 3: f_lba0[ch] = (u8)v; break;
    case 4: f_lba1[ch] = (u8)v; break;
    case 5: f_lba2[ch] = (u8)v; break;
    case 6: f_sel[ch] = (u8)((v >> 4) & 1); f_lba3[ch] = (u8)(v & 0x0F); break;
    case 7: fake_command(ch, (u8)v); break;
    }
}

#endif
