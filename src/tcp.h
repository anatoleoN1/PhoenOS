#ifndef PHOENOS_TCP_H
#define PHOENOS_TCP_H

#include "types.h"
#include "error.h"

/*
 * TCP client (0.10.0) : ouverture de connexions vers un serveur, envoi, reception, fermeture.
 * Limites volontaires de cette version :
 *  - pas de serveur (pas d'ecoute) : seulement des connexions sortantes ;
 *  - 4 connexions a la fois dans tout le systeme, chacune reservee au programme qui l'a ouverte ;
 *  - un segment envoye a la fois (attente de son accuse de reception, reemission si besoin) : simple et sur, moins rapide ;
 *  - tampon de reception de 8 Kio par connexion ; les donnees hors ordre sont ignorees (le pair les renvoie) ;
 *  - pas de TIME_WAIT ni d'options autres que la taille maximale de segment.
 */
#define TCP_SOCK_MAX        4
#define TCP_RXBUF           8192u
#define TCP_MSS             1460u
#define TCP_CONNECT_TIMEOUT_MS 10000u
#define TCP_SEND_TIMEOUT_MS    8000u
#define TCP_RECV_TIMEOUT_MS    15000u
#define TCP_CLOSE_TIMEOUT_MS   3000u
#define TCP_RTO_MS             1000u   /* delai avant de renvoyer un segment sans reponse */

/* Remise a zero (tests et demarrage). */
void tcp_reset(void);

/* Nombre de connexions ouvertes (tests, diagnostic). */
int  tcp_open_count(void);

/* Ouvre une connexion vers ip:port pour le programme `owner`. *h = numero (0..TCP_SOCK_MAX-1).
 * E_IO : pas de reseau configure ; E_REFUSED : le serveur refuse ; E_TIMEOUT ; E_BUSY : plus de connexion libre. */
err_t tcp_connect(u32 owner, u32 ip, u16 port, int *h);

/* Envoie `len` octets. *sent = octets acquittes (en cas d'erreur, ceux deja envoyes). */
err_t tcp_send(u32 owner, int h, const u8 *data, u32 len, u32 *sent);

/* Attend des donnees (15 s au plus). Renvoie le nombre d'octets copies (> 0), 0 si le serveur a ferme la connexion
 * (fin normale), ou E_TIMEOUT / E_RESET / E_INVAL (mauvais proprietaire ou numero). */
i32  tcp_recv(u32 owner, int h, u8 *buf, u32 cap);

/* Ferme la connexion (envoie FIN, attend son accuse de reception) et libere le numero. */
err_t tcp_close(u32 owner, int h);

/* Libere toutes les connexions de `owner` (appele quand le programme se termine). */
void tcp_release_owner(u32 owner);

/* Appele par la pile IP pour chaque segment recu (src/dst : adresses IPv4 de l'en-tete). */
void tcp_rx(u32 src, u32 dst, const u8 *seg, u32 len);

#endif
