/* PhoenOS - net_stubs.c (tests) : horloge de la pile reseau pour les tests qui n'attendent pas de reponse. */
#include "net.h"

u32 net_now_ms(void) { return 0; }
void net_delay_ms(u32 ms) { (void)ms; }
