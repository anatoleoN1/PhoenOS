/*
 * PhoenOS - tcp.c : client TCP (voir tcp.h). Tout ce qui vient du fil est hostile : chaque segment est verifie
 * (somme de controle, longueurs) avant d'etre accepte, et les numeros de sequence sont compares modulo 2^32.
 */
#include "tcp.h"
#include "net.h"
#include "dlog.h"
#include "string.h"

#define PROTO_TCP 6
#define TCP_HDR   20
#define F_FIN 0x01
#define F_SYN 0x02
#define F_RST 0x04
#define F_PSH 0x08
#define F_ACK 0x10

enum { ST_FREE, ST_SYN_SENT, ST_ESTAB, ST_FIN_WAIT, ST_DEAD };

struct sock {
    int state;
    u32 owner;                          /* programme qui a ouvert la connexion */
    u32 rip;                            /* adresse du serveur */
    u16 lport, rport;
    u32 iss;                            /* numero de sequence initial (le notre) */
    u32 snd_una;                        /* plus ancien octet envoye non acquitte */
    u32 snd_nxt;                        /* prochain octet a envoyer */
    u32 rcv_nxt;                        /* prochain octet attendu du serveur */
    int peer_fin;                       /* le serveur a ferme son cote */
    int err;                            /* ST_DEAD : pourquoi */
    u32 rx_len;
    u8  rx[TCP_RXBUF];
};

static struct sock S[TCP_SOCK_MAX];
static u16 next_port = 49152;
static u32 isn_count;

#define SEQ_LT(a, b) ((i32)((u32)(a) - (u32)(b)) < 0)

static u16 get16(const u8 *p) { return (u16)((p[0] << 8) | p[1]); }
static u32 get32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static void put16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

/* Laisse tourner la pile (reception) et les autres programmes pendant une milliseconde. */
static void pump(void)
{
    net_poll();
    net_delay_ms(1);
}

static int valid(u32 owner, int h, struct sock **out)
{
    if (h < 0 || h >= TCP_SOCK_MAX)
        return 0;
    if (S[h].state == ST_FREE || S[h].owner != owner)
        return 0;
    *out = &S[h];
    return 1;
}

static struct sock *find(u32 rip, u16 rport, u16 lport)
{
    for (int i = 0; i < TCP_SOCK_MAX; i++)
        if (S[i].state != ST_FREE && S[i].rip == rip && S[i].rport == rport && S[i].lport == lport)
            return &S[i];
    return NULL;
}

static u32 new_isn(void)
{
    return net_now_ms() * 64000u + (++isn_count) * 0x9E37u;
}

/* Envoie un segment de la connexion `s` (en-tete de 20 octets, ou 24 avec la taille maximale pour un SYN). */
static err_t seg_send(const struct sock *s, u8 flags, u32 seq, const u8 *data, u32 len)
{
    u8 seg[24 + TCP_MSS];
    u32 hl = (flags & F_SYN) ? 24 : TCP_HDR;
    u32 ack = (flags & F_ACK) ? s->rcv_nxt : 0;
    u32 local = net_local_ip();

    if (len > TCP_MSS)
        return E_INVAL;
    put16(seg, s->lport); put16(seg + 2, s->rport);
    put32(seg + 4, seq); put32(seg + 8, ack);
    seg[12] = (u8)((hl / 4) << 4);
    seg[13] = flags;
    put16(seg + 14, (u16)(TCP_RXBUF - s->rx_len));
    put16(seg + 16, 0); put16(seg + 18, 0);
    if (hl == 24) {                     /* option : taille maximale de segment */
        seg[20] = 2; seg[21] = 4; put16(seg + 22, TCP_MSS);
    }
    if (len)
        memcpy(seg + hl, data, len);
    put16(seg + 16, net_pseudo_checksum(local, s->rip, PROTO_TCP, seg, hl + len));
    return net_ip_send(s->rip, PROTO_TCP, seg, hl + len, 2000);
}

void tcp_reset(void)
{
    memset(S, 0, sizeof S);
    next_port = 49152;
    isn_count = 0;
}

int tcp_open_count(void)
{
    int n = 0;

    for (int i = 0; i < TCP_SOCK_MAX; i++)
        if (S[i].state != ST_FREE)
            n++;
    return n;
}

err_t tcp_connect(u32 owner, u32 ip, u16 port, int *h)
{
    struct sock *s = NULL;
    u32 t0, last = 0;
    int first = 1;
    err_t e = E_TIMEOUT;
    char a[16];

    if (!net_local_ip() || !h)
        return E_IO;
    if (port == 0)
        return E_INVAL;
    for (int i = 0; i < TCP_SOCK_MAX; i++)
        if (S[i].state == ST_FREE) {
            s = &S[i];
            *h = i;
            break;
        }
    if (!s)
        return E_BUSY;
    memset(s, 0, sizeof *s);
    s->owner = owner;
    s->rip = ip;
    s->rport = port;
    s->lport = next_port;
    next_port = next_port >= 65535 ? 49152 : (u16)(next_port + 1);
    s->iss = new_isn();
    s->snd_una = s->iss;
    s->snd_nxt = s->iss + 1;
    s->state = ST_SYN_SENT;
    t0 = net_now_ms();
    for (;;) {
        u32 now = net_now_ms();

        if (now - t0 >= TCP_CONNECT_TIMEOUT_MS)
            break;
        if (first || now - last >= TCP_RTO_MS) {
            first = 0;
            last = now;
            if (seg_send(s, F_SYN, s->iss, NULL, 0) == E_IO) {
                e = E_IO;
                break;
            }
        }
        pump();
        if (s->state == ST_ESTAB) {
            net_ip_str(ip, a);
            DINFO("tcp", "connexion ouverte vers %s:%u", a, (unsigned)port);
            return E_OK;
        }
        if (s->state == ST_DEAD) {
            e = s->err;
            break;
        }
    }
    net_ip_str(ip, a);
    DWARN("tcp", "connexion vers %s:%u : %s", a, (unsigned)port, err_str(e));
    memset(s, 0, sizeof *s);
    return e;
}

err_t tcp_send(u32 owner, int h, const u8 *data, u32 len, u32 *sent)
{
    struct sock *s;
    u32 done = 0;

    if (sent)
        *sent = 0;
    if (!valid(owner, h, &s))
        return E_INVAL;
    if (s->state == ST_DEAD)
        return s->err;
    if (s->state != ST_ESTAB)
        return E_IO;
    while (done < len) {
        u32 n = len - done, base, t0, last;

        if (n > TCP_MSS)
            n = TCP_MSS;
        base = s->snd_nxt;
        s->snd_nxt += n;
        seg_send(s, F_ACK | F_PSH, base, data + done, n);
        t0 = last = net_now_ms();
        while (SEQ_LT(s->snd_una, s->snd_nxt)) {       /* pas encore acquitte */
            u32 now = net_now_ms();

            if (s->state == ST_DEAD)
                return s->err;
            if (now - t0 >= TCP_SEND_TIMEOUT_MS) {
                s->state = ST_DEAD;
                s->err = E_TIMEOUT;
                return E_TIMEOUT;
            }
            if (now - last >= TCP_RTO_MS) {
                last = now;
                DTRACE("tcp", "reemission de %u octets", (unsigned)n);
                seg_send(s, F_ACK | F_PSH, base, data + done, n);
            }
            pump();
        }
        done += n;
        if (sent)
            *sent = done;
    }
    return E_OK;
}

i32 tcp_recv(u32 owner, int h, u8 *buf, u32 cap)
{
    struct sock *s;
    u32 t0;

    if (!valid(owner, h, &s) || cap == 0)
        return E_INVAL;
    t0 = net_now_ms();
    for (;;) {
        if (s->rx_len) {
            u32 n = s->rx_len < cap ? s->rx_len : cap;

            memcpy(buf, s->rx, n);
            memmove(s->rx, s->rx + n, s->rx_len - n);
            s->rx_len -= n;
            return (i32)n;
        }
        if (s->state == ST_DEAD)
            return s->err;
        if (s->peer_fin)
            return 0;
        if (s->state != ST_ESTAB && s->state != ST_FIN_WAIT)
            return E_IO;
        if (net_now_ms() - t0 >= TCP_RECV_TIMEOUT_MS)
            return E_TIMEOUT;
        pump();
    }
}

err_t tcp_close(u32 owner, int h)
{
    struct sock *s;

    if (!valid(owner, h, &s))
        return E_INVAL;
    if (s->state == ST_ESTAB) {
        u32 fin_seq = s->snd_nxt, t0, last;

        s->snd_nxt++;                   /* le FIN occupe un numero de sequence */
        s->state = ST_FIN_WAIT;
        seg_send(s, F_FIN | F_ACK, fin_seq, NULL, 0);
        t0 = last = net_now_ms();
        while (s->state == ST_FIN_WAIT && SEQ_LT(s->snd_una, s->snd_nxt)) {
            u32 now = net_now_ms();

            if (now - t0 >= TCP_CLOSE_TIMEOUT_MS)
                break;
            if (now - last >= TCP_RTO_MS) {
                last = now;
                seg_send(s, F_FIN | F_ACK, fin_seq, NULL, 0);
            }
            pump();
        }
    }
    memset(s, 0, sizeof *s);
    return E_OK;
}

void tcp_release_owner(u32 owner)
{
    for (int i = 0; i < TCP_SOCK_MAX; i++)
        if (S[i].state != ST_FREE && S[i].owner == owner)
            memset(&S[i], 0, sizeof S[i]);
}

void tcp_rx(u32 src, u32 dst, const u8 *seg, u32 len)
{
    struct sock *s;
    u16 sport, dport;
    u32 hl, seq, ack, plen;
    u8 flags;

    if (len < TCP_HDR || dst != net_local_ip())
        return;
    if (net_pseudo_checksum(src, dst, PROTO_TCP, seg, len) != 0)
        return;                                         /* segment altere : ignore */
    hl = (u32)(seg[12] >> 4) * 4;
    if (hl < TCP_HDR || hl > len)
        return;
    sport = get16(seg);
    dport = get16(seg + 2);
    seq = get32(seg + 4);
    ack = get32(seg + 8);
    flags = seg[13];
    plen = len - hl;
    s = find(src, sport, dport);
    if (!s)
        return;

    if (flags & F_RST) {
        if (s->state == ST_SYN_SENT) {
            if ((flags & F_ACK) && ack == s->iss + 1) {
                s->state = ST_DEAD;
                s->err = E_REFUSED;
            }
        } else if (s->state != ST_DEAD) {
            s->state = ST_DEAD;
            s->err = E_RESET;
        }
        return;
    }
    if (s->state == ST_SYN_SENT) {
        if ((flags & (F_SYN | F_ACK)) == (F_SYN | F_ACK) && ack == s->iss + 1) {
            s->snd_una = ack;
            s->rcv_nxt = seq + 1;
            s->state = ST_ESTAB;
            seg_send(s, F_ACK, s->snd_nxt, NULL, 0);
        }
        return;
    }
    if ((s->state != ST_ESTAB && s->state != ST_FIN_WAIT) || !(flags & F_ACK))
        return;

    /* ce que le serveur acquitte */
    if (SEQ_LT(s->snd_una, ack) && !SEQ_LT(s->snd_nxt, ack))
        s->snd_una = ack;

    /* ses donnees : seulement si elles arrivent dans l'ordre et tiennent dans le tampon */
    if (plen && seq == s->rcv_nxt && !s->peer_fin && s->rx_len + plen <= TCP_RXBUF) {
        memcpy(s->rx + s->rx_len, seg + hl, plen);
        s->rx_len += plen;
        s->rcv_nxt += plen;
    }
    /* sa fermeture */
    if ((flags & F_FIN) && !s->peer_fin && seq + plen == s->rcv_nxt) {
        s->rcv_nxt++;
        s->peer_fin = 1;
    }
    if (plen || (flags & F_FIN))
        seg_send(s, F_ACK, s->snd_nxt, NULL, 0);
}
