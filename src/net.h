#ifndef PHOENOS_NET_H
#define PHOENOS_NET_H

#include "types.h"
#include "error.h"
#include "netdev.h"

/*
 * Pile reseau (0.9.0) : Ethernet, ARP, IPv4, ICMP (ping), UDP, client DHCP et resolveur DNS (enregistrements A).
 * Pas encore de TCP : il viendra avec les mises a jour automatiques. Les adresses IPv4 sont des u32 "a.b.c.d" = a<<24|b<<16|c<<8|d.
 * Tout est interroge (pas d'interruption) : la tache noyau "net" appelle net_poll() toutes les 10 ms, et les commandes qui
 * attendent une reponse (ping, dns, net up) l'appellent aussi elles-memes.
 */
#define NET_ARP_MAX 8

struct net_status {
    int  present;                       /* une carte est enregistree */
    int  configured;                    /* adresse IP obtenue */
    u32  ip, mask, gw, dns;
    u32  lease_s;
    u32  arp_requests, arp_replies, icmp_in, icmp_out, udp_in, udp_out, ip_bad;
};

/* Choisit la premiere carte enregistree (a appeler apres les pilotes). E_NOENT : aucune. */
err_t net_init(void);
void  net_reset(void);                  /* tests */
struct netdev *net_dev(void);
void  net_get_status(struct net_status *s);

/* Traite les trames recues (au plus un lot). */
void  net_poll(void);

/* DHCP : demande une adresse (E_TIMEOUT : pas de serveur ; E_BUSY : deja en cours ; E_NOENT : pas de carte ; E_IO : pas de lien). */
err_t net_dhcp(u32 timeout_ms);

/* ping : *rtt_ms = temps aller-retour (E_TIMEOUT : pas de reponse). */
err_t net_ping(u32 ip, u32 timeout_ms, u32 *rtt_ms);

/* DNS : nom -> adresse IPv4 (un nom deja numerique est accepte). E_NOENT : nom inconnu ; E_TIMEOUT ; E_INVAL : nom invalide ; E_IO : pas de DNS. */
err_t net_resolve(const char *name, u32 *ip);

/* "10.0.2.15" -> u32 (E_INVAL si mal forme) ; u32 -> texte (au moins 16 octets). */
err_t net_parse_ip(const char *s, u32 *ip);
void  net_ip_str(u32 ip, char *out);

/* Tache noyau "net" : DHCP au demarrage puis interrogation reguliere, sans bloquer le demarrage. */
void  net_start(void);

/* Crochets de la plateforme (net_platform.c ; remplaces par les tests). */
u32  net_now_ms(void);
void net_delay_ms(u32 ms);

#endif
