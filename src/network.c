/* ---------------------------------------------------------------------- *
 * network.c
 * This file is part of lincity.
 * Lincity is copyright (c) I J Peters 1995-1997, (c) Greg Sharp 1997-2001.
 * ---------------------------------------------------------------------- */
#include "lcconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <zlib.h>
#include "lin-city.h"
#include "engglobs.h"
#include "cliglobs.h"
#include "mapwire.h"
#include "network.h"
#include "stats.h"
#include "validation.h"

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32")
#include <stdarg.h>
static FILE *nlog_fp = NULL;
static void nlog(const char *fmt, ...) {
    if (!nlog_fp) nlog_fp = fopen("lincity_net.log", "a");
    if (nlog_fp) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(nlog_fp, fmt, ap);
        va_end(ap);
        fflush(nlog_fp);
    }
}
#define SOCKCALL(r)    ((int)(r))
#define SOCK_ERRNO     WSAGetLastError()
#define SOCK_AGAIN     WSAEWOULDBLOCK
#define SOCK_EWOULDBLOCK WSAEWOULDBLOCK
#define SEND_FLAGS     0
#define NO_SIGNAL_SEND_FLAGS 0
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#define SOCKCALL(r)    (r)
#define SOCK_ERRNO     errno
#define SOCK_AGAIN     EAGAIN
#define SOCK_EWOULDBLOCK EWOULDBLOCK
#define SEND_FLAGS     MSG_NOSIGNAL
#define NO_SIGNAL_SEND_FLAGS MSG_NOSIGNAL
#endif

#ifdef WIN32
static int winsock_init_count = 0;

static int
network_wsastart(void)
{
    if (winsock_init_count++ == 0) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
            winsock_init_count = 0;
            fprintf(stderr, "network: WSAStartup failed\n"); fflush(stderr);
            return -1;
        }
    }
    return 0;
}

static void
network_wsaclean(void)
{
    if (winsock_init_count <= 0)
        return;
    if (--winsock_init_count == 0)
        WSACleanup();
}
#else
static int network_wsastart(void) { return 0; }
static void network_wsaclean(void) {}
/* nlog is debug logging; only active on Windows */
#define nlog(...) ((void)0)
#endif

/* ---------------------------------------------------------------------- *
 * Raw send / recv
 * ---------------------------------------------------------------------- */
static uint64_t
network_monotonic_ms(void)
{
#ifdef WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000u
        + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

/* Wait for readability up to the caller's absolute deadline.  This also
   makes blocking host sockets honor the deadline before every recv(). */
static int
network_wait_readable(int fd, uint64_t deadline)
{
    for (;;) {
        uint64_t now = network_monotonic_ms();
        uint64_t remaining;
        struct timeval tv;
        fd_set rfds;
        int sr;

        if (now >= deadline)
            return -1;
        remaining = deadline - now;
        tv.tv_sec = (long)(remaining / 1000u);
        tv.tv_usec = (long)((remaining % 1000u) * 1000u);
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
#ifdef WIN32
        sr = select(0, &rfds, NULL, NULL, &tv);
#else
        sr = select(fd + 1, &rfds, NULL, NULL, &tv);
#endif
        if (sr > 0)
            return 0;
        if (sr < 0) {
#ifdef WIN32
            if (SOCK_ERRNO == WSAEINTR)
#else
            if (SOCK_ERRNO == EINTR)
#endif
                continue;
            return -1;
        }
    }
}

static int
network_set_nonblocking(int fd)
{
#ifdef WIN32
    u_long mode = 1;
    return ioctlsocket((SOCKET)fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0)
        return -1;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0 ? 0 : -1;
#endif
}

static int network_host_is_client_fd(int fd);
static int network_host_enqueue(int fd, const char *buf, int len);

int
network_send_raw(int fd, const char *buf, int len)
{
    int sent = 0, n;

    if (network_host_is_client_fd(fd))
        return network_host_enqueue(fd, buf, len);
    while (sent < len) {
        n = SOCKCALL(send(fd, buf + sent, len - sent, NO_SIGNAL_SEND_FLAGS));
        if (n <= 0)
            return -1;
        sent += n;
    }
    return sent;
}

int
network_recv_raw(int fd, char *buf, size_t len)
{
    size_t got = 0;
    int n;
    uint64_t deadline;

    if (len > NETWORK_MAX_MAP_BYTES) {
        fprintf(stderr, "network: raw read too large (%zu > %u)\n",
                len, NETWORK_MAX_MAP_BYTES);
        return -1;
    }
    deadline = network_monotonic_ms() + NETWORK_IO_TIMEOUT_MS;
    while (got < len) {
        if (network_wait_readable(fd, deadline) < 0) {
            fprintf(stderr, "network: raw read timed out (%zu/%zu)\n",
                    got, len);
            return -1;
        }
        n = SOCKCALL(recv(fd, buf + got, (int)(len - got), 0));
        if (n > 0) {
            got += (size_t)n;
            continue;
        }
        if (n == 0)
            return -1;
        /* Non-blocking: retry on EAGAIN/EWOULDBLOCK. */
#ifdef WIN32
        if (WSAGetLastError() == WSAEWOULDBLOCK)
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK)
#endif
            continue;
        return -1;
    }
    return (int)got;
}

int
network_send_text(int fd, const char *fmt, ...)
{
    char buf[NETWORK_BUF_SIZE];
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (len < 0 || len >= (int)sizeof(buf))
        return -1;
    return network_send_raw(fd, buf, len);
}

int
network_recv_line(int fd, char *buf, int bufsize)
{
    int i = 0;
    char c;
    uint64_t deadline;

    if (bufsize <= 0)
        return -1;
    deadline = network_monotonic_ms() + NETWORK_IO_TIMEOUT_MS;
    while (i < bufsize - 1) {
        int n;

        if (network_wait_readable(fd, deadline) < 0) {
            fprintf(stderr, "network: line read timed out (%d bytes)\n", i);
            return -1;
        }
        n = SOCKCALL(recv(fd, &c, 1, 0));
        if (n > 0) {
            if (c == '\n')
                break;
            buf[i++] = c;
            continue;
        }
        if (n == 0) {
            fprintf(stderr, "network: recv_line: connection closed (fd=%d)\n", fd); fflush(stderr);
            return -1;
        }
        /* Non-blocking: retry on EAGAIN/EWOULDBLOCK. */
#ifdef WIN32
        if (WSAGetLastError() == WSAEWOULDBLOCK)
            continue;
        fprintf(stderr, "network: recv_line: error fd=%d err=%d\n", fd, SOCK_ERRNO); fflush(stderr);
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            continue;
        fprintf(stderr, "network: recv_line: error fd=%d err=%d\n", fd, errno); fflush(stderr);
#endif
        return -1;
    }
    buf[i] = '\0';
    return i;
}

/* ---------------------------------------------------------------------- *
 * Compressed map transport
 * ---------------------------------------------------------------------- */
/* The fixed-width array codec lives in mapwire.h; this file adds the
 * existing zlib blob header:
 *   uncomp_size (uint32 LE) + comp_size (uint32 LE) + comp_data */
static int
send_compressed_map(int fd,
    short type[WORLD_SIDE_LEN][WORLD_SIDE_LEN],
    short group[WORLD_SIDE_LEN][WORLD_SIDE_LEN],
    Map_Point_Info info[WORLD_SIDE_LEN][WORLD_SIDE_LEN],
    int pollution[WORLD_SIDE_LEN][WORLD_SIDE_LEN])
{
    size_t usz = MAP_WIRE_BYTES;
    unsigned char *u = (unsigned char *)malloc(usz);
    unsigned char *p = u ? map_wire_encode(WORLD_SIDE_LEN, type, group,
                                          info, pollution, u) : NULL;

    if (!u || !p || (size_t)(p - u) != usz) {
        free(u);
        return -1;
    }

    uLongf csz = compressBound(usz);
    unsigned char *c = (unsigned char *)malloc(csz);
    if (!c) { free(u); return -1; }

    if (compress(c, &csz, u, usz) != Z_OK) {
        free(u); free(c); return -1;
    }
    free(u);

    unsigned char hdr[8];
    map_put_u32le(hdr, (uint32_t)usz);
    map_put_u32le(hdr + 4, (uint32_t)csz);
    int ok = 1;
    if (network_send_raw(fd, (char*)hdr, 8) < 0) ok = 0;
    else if (network_send_raw(fd, (char*)c, csz) < 0) ok = 0;
    free(c);
    return ok ? 0 : -1;
}

/* Inverse — receive compressed blob, decompress, decode fixed-width arrays,
   then validate the type/group pairs before exposing them to the engine. */
static int
recv_compressed_map(int fd,
    short type[WORLD_SIDE_LEN][WORLD_SIDE_LEN],
    short group[WORLD_SIDE_LEN][WORLD_SIDE_LEN],
    Map_Point_Info info[WORLD_SIDE_LEN][WORLD_SIDE_LEN],
    int pollution[WORLD_SIDE_LEN][WORLD_SIDE_LEN])
{
    size_t expected = MAP_WIRE_BYTES;

    unsigned char hdr[8];
    if (network_recv_raw(fd, (char*)hdr, 8) < 0) return -1;
    size_t usz = map_get_u32le(hdr);
    size_t csz = map_get_u32le(hdr + 4);

    if (usz != expected) {
        fprintf(stderr, "network: compressed world size mismatch (%zu != %zu)\n",
                usz, expected);
        return -1;
    }
    if (csz == 0 || csz > NETWORK_MAX_MAP_BYTES
        || csz > (size_t)compressBound((uLong)expected)) {
        fprintf(stderr, "network: compressed world length out of range (%zu)\n",
                csz);
        return -1;
    }

    unsigned char *c = (unsigned char *)malloc(csz);
    if (!c) return -1;
    if (network_recv_raw(fd, (char*)c, csz) < 0) { free(c); return -1; }

    unsigned char *u = (unsigned char *)malloc(usz);
    if (!u) { free(c); return -1; }
    uLongf dec = usz;
    if (uncompress(u, &dec, c, csz) != Z_OK || dec != usz) {
        fprintf(stderr, "network: uncompress failed\n");
        free(c); free(u); return -1;
    }
    free(c);

    const unsigned char *p = map_wire_decode(WORLD_SIDE_LEN, u, type, group,
                                             info, pollution);
    if (!p || (size_t)(p - u) != expected) {
        fprintf(stderr, "network: world decode size mismatch\n");
        free(u);
        return -1;
    }
    free(u);

    if (!map_state_ok(type, group, info, -1, NULL, NULL, -1, NULL, NULL)) {
        fprintf(stderr, "network: received world failed map validation\n");
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------------- *
 * Server (host) implementation
 * ---------------------------------------------------------------------- */
int
network_host_start(int port)
{
    int fd, opt = 1;
    struct sockaddr_in addr;

    if (network_wsastart() < 0) return -1;

    fprintf(stderr, "network_host_start(%d)\n", port); fflush(stderr);
    fd = SOCKCALL(socket(AF_INET, SOCK_STREAM, 0));
    if (fd < 0) {
        fprintf(stderr, "network: socket failed err=%d\n", SOCK_ERRNO); fflush(stderr);
        return -1;
    }
    fprintf(stderr, "network: socket=%d\n", fd); fflush(stderr);

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    fprintf(stderr, "network: binding...\n"); fflush(stderr);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "network: bind failed err=%d\n", SOCK_ERRNO); fflush(stderr);
        CLOSE_SOCK(fd);
        return -1;
    }
    fprintf(stderr, "network: listening...\n"); fflush(stderr);
    if (listen(fd, MAX_CLIENTS) < 0) {
        CLOSE_SOCK(fd);
        return -1;
    }
    fprintf(stderr, "network: host_start OK, fd=%d\n", fd); fflush(stderr);
    return fd;
}

int
network_host_wait_client(int server_fd)
{
    struct sockaddr_in addr;
#ifdef WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    int fd;

    fprintf(stderr, "network: waiting for client on fd=%d...\n", server_fd); fflush(stderr);
    fd = SOCKCALL(accept(server_fd, (struct sockaddr*)&addr, &len));
    if (fd < 0) {
        fprintf(stderr, "network: accept failed err=%d\n", SOCK_ERRNO); fflush(stderr);
        return -1;
    }
    fprintf(stderr, "network: accepted client fd=%d\n", fd); fflush(stderr);

    /* TCP_NODELAY for client sockets from the host side too */
    {
        int opt = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));
    }

    return fd;
}

/* Each accepted client gets its own non-blocking line buffer.  A partial
   line is retained across frames; the simulation never waits for a newline. */
#ifndef NETWORK_HOST_TX_BYTES
#define NETWORK_HOST_TX_BYTES (1024u * 1024u)
#endif

struct network_host_line {
    int fd;
    int dead;
    size_t len;
    size_t tx_len;
    char buf[NETWORK_BUF_SIZE + 1];
    char tx_buf[NETWORK_HOST_TX_BYTES];
};

static struct network_host_line host_lines[MAX_CLIENTS];
static int host_lines_ready = 0;

static void
network_host_lines_init(void)
{
    if (host_lines_ready)
        return;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        host_lines[i].fd = -1;
        host_lines[i].dead = 0;
        host_lines[i].len = 0;
        host_lines[i].tx_len = 0;
    }
    host_lines_ready = 1;
}

static int
network_host_is_client_fd(int fd)
{
    if (!network_is_host || fd < 0)
        return 0;
    if (fd == network_socket)
        return 1;
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
        if (fd == network_extra_fds[i])
            return 1;
    return 0;
}

static struct network_host_line *
network_host_line_slot(int fd, int create)
{
    struct network_host_line *free_slot = NULL;

    network_host_lines_init();
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (host_lines[i].fd == fd)
            return &host_lines[i];
        if (host_lines[i].fd < 0 && !free_slot)
            free_slot = &host_lines[i];
    }
    if (!create || !free_slot)
        return NULL;
#ifdef WIN32
    /* POSIX uses MSG_DONTWAIT per read so host sends stay blocking until
       the separate slow-send queue work. */
    if (network_set_nonblocking(fd) < 0) {
        fprintf(stderr, "network: could not set client fd %d non-blocking\n", fd);
        fflush(stderr);
        return NULL;
    }
#endif
    free_slot->fd = fd;
    free_slot->dead = 0;
    free_slot->len = 0;
    free_slot->tx_len = 0;
    return free_slot;
}

void
network_host_forget_client(int client_fd)
{
    struct network_host_line *slot;

    if (client_fd < 0)
        return;
    slot = network_host_line_slot(client_fd, 0);
    if (slot) {
        slot->fd = -1;
        slot->dead = 0;
        slot->len = 0;
        slot->tx_len = 0;
    }
}

/* Remove one dead client from all host bookkeeping.  The return value is
   true only when the fd was actually tracked, so a duplicate fd in the
   caller's snapshot cannot decrement network_num_clients twice. */
int
network_host_drop_client(int client_fd)
{
    int found = 0;

    if (client_fd < 0)
        return 0;
    if (client_fd == network_socket)
        found = 1;
    for (int i = 0; i < MAX_CLIENTS - 1; i++) {
        if (network_extra_fds[i] == client_fd) {
            network_extra_fds[i] = -1;
            found = 1;
        }
    }
    if (!found)
        return 0;
    if (client_fd == network_socket)
        network_socket = -1;
    network_host_forget_client(client_fd);
    if (network_num_clients > 0)
        network_num_clients--;
    return 1;
}

static int
network_host_flush_slot(struct network_host_line *slot)
{
    while (slot->tx_len > 0) {
        int n;
#ifdef WIN32
        n = SOCKCALL(send(slot->fd, slot->tx_buf, slot->tx_len,
                          NO_SIGNAL_SEND_FLAGS));
#else
        n = SOCKCALL(send(slot->fd, slot->tx_buf, slot->tx_len,
                          MSG_DONTWAIT | MSG_NOSIGNAL));
#endif
        if (n > 0) {
            memmove(slot->tx_buf, slot->tx_buf + n,
                    slot->tx_len - (size_t)n);
            slot->tx_len -= (size_t)n;
            continue;
        }
        if (n < 0 && (SOCK_ERRNO == SOCK_AGAIN
                      || SOCK_ERRNO == SOCK_EWOULDBLOCK))
            return 0;
        fprintf(stderr, "network: host send failed; disconnecting client fd=%d\n",
                slot->fd);
        fflush(stderr);
        slot->dead = 1;
        return -1;
    }
    return 0;
}

static int
network_host_enqueue(int fd, const char *buf, int len)
{
    struct network_host_line *slot;

    if (len < 0)
        return -1;
    slot = network_host_line_slot(fd, 1);
    if (!slot || slot->dead)
        return -1;
    if ((size_t)len > sizeof(slot->tx_buf) - slot->tx_len) {
        fprintf(stderr, "network: host send queue full; disconnecting client fd=%d\n",
                fd);
        fflush(stderr);
        slot->dead = 1;
        return -1;
    }
    memcpy(slot->tx_buf + slot->tx_len, buf, (size_t)len);
    slot->tx_len += (size_t)len;
    return network_host_flush_slot(slot);
}

void
network_host_flush_all(void)
{
    network_host_lines_init();
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (host_lines[i].fd >= 0 && !host_lines[i].dead)
            network_host_flush_slot(&host_lines[i]);
    }
}

static int
network_host_recv_line(int fd, char *buf, int bufsize)
{
    struct network_host_line *slot = network_host_line_slot(fd, 1);

    if (!slot || slot->dead || bufsize <= 0)
        return -1;
    for (;;) {
        char *nl = memchr(slot->buf, '\n', slot->len);
        if (nl) {
            size_t line_len = (size_t)(nl - slot->buf);
            size_t rest;

            if (line_len >= (size_t)bufsize) {
                fprintf(stderr, "network: host line too long; disconnecting client fd=%d\n", fd);
                fflush(stderr);
                slot->len = 0;
                return -1;
            }
            memcpy(buf, slot->buf, line_len);
            buf[line_len] = '\0';
            rest = slot->len - line_len - 1;
            memmove(slot->buf, nl + 1, rest);
            slot->len = rest;
            return (int)line_len;
        }
        if (slot->len >= sizeof(slot->buf)) {
            fprintf(stderr, "network: host line too long; disconnecting client fd=%d\n", fd);
            fflush(stderr);
            slot->len = 0;
            return -1;
        }
        {
#ifdef WIN32
            int n = SOCKCALL(recv(fd, slot->buf + slot->len,
                                  (int)(sizeof(slot->buf) - slot->len), 0));
#else
            int n = SOCKCALL(recv(fd, slot->buf + slot->len,
                                  (int)(sizeof(slot->buf) - slot->len),
                                  MSG_DONTWAIT));
#endif
            if (n > 0) {
                slot->len += (size_t)n;
                continue;
            }
            if (n == 0)
                return -1;
#ifdef WIN32
            if (WSAGetLastError() == WSAEWOULDBLOCK)
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK)
#endif
                return 0;
            return -1;
        }
    }
}

void
network_host_stop(int server_fd, int client_fd)
{
    if (client_fd >= 0) {
        network_send_text(client_fd, "%d\n", MSG_QUIT);
        network_host_forget_client(client_fd);
        CLOSE_SOCK(client_fd);
    }
    if (server_fd >= 0)
        CLOSE_SOCK(server_fd);
    network_wsaclean();
}

int
network_host_send_map(int client_fd)
{
    char buf[128];

    nlog("send_map(%d)\n", client_fd);
    snprintf(buf, sizeof(buf), "%d %d %d\n", MSG_WELCOME, WORLD_SIDE_LEN,
             NETWORK_VERSION);
    if (network_send_raw(client_fd, buf, strlen(buf)) < 0) {
        nlog("send_map: welcome failed\n");
        return -1;
    }
    nlog("send_map: welcome sent\n");

    nlog("send_map: sending compressed world...\n");
    if (send_compressed_map(client_fd, map.type, map.group, map.info, map.pollution) < 0) {
        nlog("send_map: compressed send failed\n");
        return -1;
    }

    nlog("send_map: sending OK...\n");
    network_send_text(client_fd, "%d\n", MSG_OK);

    /* Send initial stats */
    network_send_text(client_fd, "%d %d\n", MSG_MONEY, total_money);
    network_send_text(client_fd, "%d %d\n", MSG_POP, people_pool);
    network_send_text(client_fd, "%d %d\n", MSG_YEAR, total_time);

    nlog("send_map: done OK\n");
    return 0;
}

int
network_host_broadcast_action(int client_fd, char msg_type, int x, int y, int val)
{
    return network_send_text(client_fd, "%d %d %d %d\n", msg_type, x, y, val);
}

int
network_host_broadcast_money(int client_fd)
{
    return network_send_text(client_fd, "%d %d\n", MSG_MONEY, total_money);
}

int
network_host_broadcast_pop(int client_fd)
{
    return network_send_text(client_fd, "%d %d\n", MSG_POP, people_pool);
}

int
network_host_broadcast_year(int client_fd)
{
    return network_send_text(client_fd, "%d %d\n", MSG_YEAR, total_time);
}

int
network_host_broadcast_speed(int client_fd)
{
    int s = 1;
    if (pause_flag) s = 0;
    else if (slow_flag) s = 1;
    else if (med_flag) s = 2;
    else if (fast_flag) s = 3;
    return network_send_text(client_fd, "%d %d\n", MSG_SPEED, s);
}

int
network_host_broadcast_tech(int client_fd)
{
    return network_send_text(client_fd, "%d %d\n", MSG_TECH, tech_level);
}

void
network_host_broadcast_stats(int client_fd)
{
    network_send_text(client_fd, "%d %d %d\n",
		      MSG_STAT1, housed_population, tstarving_population);
    network_send_text(client_fd, "%d %d %d\n",
		      MSG_STAT2, tfood_in_markets, tunemployed_population);
    network_send_text(client_fd, "%d %d %d\n",
		      MSG_STAT3, tjobs_in_markets, numof_shanties);
    network_send_text(client_fd, "%d %d %d\n",
		      MSG_STAT4, tcoal_in_markets, tgoods_in_markets);
    network_send_text(client_fd, "%d %d %d\n",
		      MSG_STAT5, tore_in_markets, tsteel_in_markets);
    network_send_text(client_fd, "%d %d %d\n",
		      MSG_STAT6, data_last_month, 0);
}

void
network_host_broadcast_yearly_stats(int client_fd)
{
    network_send_text(client_fd, "%d %d %d\n",
		      MSG_YTAX1, ly_income_tax, ly_coal_tax);
    network_send_text(client_fd, "%d %d %d\n",
		      MSG_YTAX2, ly_goods_tax, ly_export_tax);
    network_send_text(client_fd, "%d %d %d\n",
		      MSG_YCST1, ly_transport_cost, ly_unemployment_cost);
    network_send_text(client_fd, "%d %d %d\n",
		      MSG_YCST2, ly_import_cost, ly_other_cost);
    network_send_text(client_fd, "%d %d %d\n",
		      MSG_YCST3, ly_university_cost, ly_recycle_cost);
    network_send_text(client_fd, "%d %d %d\n",
		      MSG_YCST4, ly_deaths_cost, ly_health_cost);
    network_send_text(client_fd, "%d %d %d\n",
		      MSG_YCST5, ly_school_cost, ly_windmill_cost);
    network_send_text(client_fd, "%d %d %d\n",
		      MSG_YCST6, ly_rocket_pad_cost, ly_cricket_cost);
    network_send_text(client_fd, "%d %d %d\n",
		      MSG_YINT, ly_interest, ly_fire_cost);
}

int
network_host_send_info(int client_fd)
{
    fprintf(stderr, "network_host_send_info(%d)\n", client_fd); fflush(stderr);

    if (network_send_text(client_fd, "%d\n", MSG_INFO) < 0) {
        fprintf(stderr, "network: send info header failed\n"); fflush(stderr);
        return -1;
    }

    fprintf(stderr, "network: sending compressed world...\n"); fflush(stderr);
    if (send_compressed_map(client_fd, map.type, map.group, map.info, map.pollution) < 0) {
        fprintf(stderr, "network: send compressed world failed\n"); fflush(stderr);
        return -1;
    }

    if (network_send_text(client_fd, "%d\n", MSG_OK) < 0) {
        fprintf(stderr, "network: send OK after info failed\n"); fflush(stderr);
        return -1;
    }

    fprintf(stderr, "network: info sent OK\n"); fflush(stderr);
    return 0;
}

static void
network_bad_action(void)
{
    static time_t last = 0;
    time_t now = time(NULL);

    if (now != last) {
        fprintf(stderr, "network: rejected malformed action line\n");
        fflush(stderr);
        last = now;
    }
}

/* The client sees one host stream.  Log every malformed line so the client
   repro can prove that each payload was actually parsed and rejected. */
static void
network_bad_client_action(void)
{
    fprintf(stderr, "network: rejected malformed action line\n");
    fflush(stderr);
}

static int
parse_action_int(char **pp, int *out)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(*pp, &end, 10);
    if (end == *pp || errno == ERANGE || value < INT_MIN || value > INT_MAX)
        return -1;
    *out = (int)value;
    *pp = end;
    return 0;
}

/* Parse up to four bounded integers from a complete action line.  Both the
   host and the client use this so a malformed value cannot wrap through
   sscanf or leave trailing garbage unread. */
static int
parse_action_values(const char *line, int values[4])
{
    char *p = (char *)line;
    int nvalues = 0;

    while (nvalues < 4) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\r') break;
        if (parse_action_int(&p, &values[nvalues]) < 0)
            return -1;
        nvalues++;
    }
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    if (*p != '\0')
        return -1;
    return nvalues;
}

/* Read exactly one hello line before the host sends any map bytes.  A bad
   or mismatched version gets an explicit disconnect line and a close. */
int
network_host_verify_hello(int client_fd)
{
    char buf[128];
    int values[4] = { 0, 0, 0, 0 };
    int n = network_recv_line(client_fd, buf, sizeof(buf));

    if (n <= 0) {
        fprintf(stderr, "network: rejected client protocol hello (no line)\n");
        fflush(stderr);
        return -1;
    }
    if (parse_action_values(buf, values) == 2 && values[0] == MSG_HELLO) {
        if (values[1] == NETWORK_VERSION) {
            fprintf(stderr, "network: client protocol version %d accepted\n",
                    NETWORK_VERSION);
            fflush(stderr);
            return 0;
        }
        fprintf(stderr,
                "network: rejected client protocol version %d (expected %d)\n",
                values[1], NETWORK_VERSION);
    } else {
        fprintf(stderr, "network: rejected malformed client hello [%s]\n", buf);
    }
    fflush(stderr);
    network_send_text(client_fd, "%d\n", MSG_DISCONN);
    return -1;
}

int
network_host_recv_action(int client_fd, char *action, int *x, int *y, int *val)
{
    char buf[NETWORK_BUF_SIZE];
    int n;
    int values[4] = { 0, 0, 0, 0 };
    int nvalues;

    n = network_host_recv_line(client_fd, buf, sizeof(buf));
    if (n < 0)
	return -1;
    if (n == 0)
	return 0;

    nvalues = parse_action_values(buf, values);
    if (nvalues < 2 || values[0] < 0 || values[0] > 255) {
        network_bad_action();
        return 0;
    }

    *action = (char)values[0];
    *x = values[1];
    *y = values[2];
    *val = values[3];
    return 1;
}

/* ---------------------------------------------------------------------- *
 * Multi-client helpers: loop over all connected client sockets
 * ---------------------------------------------------------------------- */
int
network_host_broadcast_action_all(char type, int x, int y, int val)
{
    int ok = 0;
    if (network_socket >= 0)
        ok |= network_host_broadcast_action(network_socket, type, x, y, val);
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
        if (network_extra_fds[i] >= 0)
            ok |= network_host_broadcast_action(network_extra_fds[i], type, x, y, val);
    return ok;
}

/* Replicate a host action only after the local engine accepted it.  These
   helpers keep failed placements from reaching clients on every host path:
   remote actions, ordinary mouse placement, and drag/overwrite drag paths. */
int
network_host_broadcast_build_if_placed(char type, int x, int y, int val,
                                       int result)
{
    if (result != 0)
        return 0;
    return network_host_broadcast_action_all(type, x, y, val);
}

int
network_host_broadcast_bulldoze_if_removed(int x, int y, int removed)
{
    if (removed <= 0)
        return 0;
    return network_host_broadcast_action_all(MSG_BULLDOZE, x, y, 0);
}

int
network_host_send_map_all(void)
{
    int ok = 0;
    if (network_socket >= 0)
        ok |= network_host_send_map(network_socket);
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
        if (network_extra_fds[i] >= 0)
            ok |= network_host_send_map(network_extra_fds[i]);
    return ok;
}

void
network_host_broadcast_money_all(void)
{
    if (network_socket >= 0) network_host_broadcast_money(network_socket);
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
        if (network_extra_fds[i] >= 0)
            network_host_broadcast_money(network_extra_fds[i]);
}

void
network_host_broadcast_pop_all(void)
{
    if (network_socket >= 0) network_host_broadcast_pop(network_socket);
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
        if (network_extra_fds[i] >= 0)
            network_host_broadcast_pop(network_extra_fds[i]);
}

void
network_host_broadcast_year_all(void)
{
    if (network_socket >= 0) network_host_broadcast_year(network_socket);
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
        if (network_extra_fds[i] >= 0)
            network_host_broadcast_year(network_extra_fds[i]);
}

void
network_host_broadcast_speed_all(void)
{
    if (network_socket >= 0) network_host_broadcast_speed(network_socket);
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
        if (network_extra_fds[i] >= 0)
            network_host_broadcast_speed(network_extra_fds[i]);
}

void
network_host_broadcast_tech_all(void)
{
    if (network_socket >= 0) network_host_broadcast_tech(network_socket);
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
        if (network_extra_fds[i] >= 0)
            network_host_broadcast_tech(network_extra_fds[i]);
}

void
network_host_broadcast_stats_all(void)
{
    if (network_socket >= 0) network_host_broadcast_stats(network_socket);
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
        if (network_extra_fds[i] >= 0)
            network_host_broadcast_stats(network_extra_fds[i]);
}

void
network_host_broadcast_yearly_stats_all(void)
{
    if (network_socket >= 0) network_host_broadcast_yearly_stats(network_socket);
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
        if (network_extra_fds[i] >= 0)
            network_host_broadcast_yearly_stats(network_extra_fds[i]);
}

int
network_host_send_info_all(void)
{
    int ok = 0;
    if (network_socket >= 0)
        ok |= network_host_send_info(network_socket);
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
        if (network_extra_fds[i] >= 0)
            ok |= network_host_send_info(network_extra_fds[i]);
    return ok;
}

/* ---------------------------------------------------------------------- *
 * Client implementation
 * ---------------------------------------------------------------------- */
int
network_client_connect(const char *host, int port)
{
    int fd;
    struct sockaddr_in addr;
    struct hostent *he;

    if (network_wsastart() < 0) return -1;

    fprintf(stderr, "network_client_connect(%s,%d)\n", host, port); fflush(stderr);
    fd = SOCKCALL(socket(AF_INET, SOCK_STREAM, 0));
    if (fd < 0) {
        fprintf(stderr, "network: client socket failed err=%d\n", SOCK_ERRNO); fflush(stderr);
        return -1;
    }
    fprintf(stderr, "network: client socket=%d\n", fd); fflush(stderr);

    he = gethostbyname(host);
    if (!he) {
        fprintf(stderr, "network: gethostbyname failed for %s\n", host); fflush(stderr);
        CLOSE_SOCK(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    fprintf(stderr, "network: connecting to %s:%d...\n", host, port); fflush(stderr);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "network: connect failed err=%d\n", SOCK_ERRNO); fflush(stderr);
        CLOSE_SOCK(fd);
        return -1;
    }
    fprintf(stderr, "network: connected, fd=%d\n", fd); fflush(stderr);
    network_client_reset_send_queue();

    /* The host must see the version before it sends a welcome or map. */
    if (network_send_text(fd, "%d %d\n", MSG_HELLO, NETWORK_VERSION) < 0) {
        fprintf(stderr, "network: client hello send failed\n");
        fflush(stderr);
        CLOSE_SOCK(fd);
        network_wsaclean();
        return -1;
    }

    /* TCP_NODELAY: disable Nagle so actions arrive without delay */
    {
        int opt = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&opt, sizeof(opt));
    }

    /* Non-blocking: prevents send() in network_client_send_action
       from freezing the mouse handler when the kernel buffer is full. */
    if (network_set_nonblocking(fd) < 0) {
        fprintf(stderr, "network: could not set client fd %d non-blocking\n", fd);
        fflush(stderr);
        CLOSE_SOCK(fd);
        network_wsaclean();
        return -1;
    }

    return fd;
}

int
network_client_recv_map(int fd)
{
    char buf[128];
    int n;
    int values[4] = { 0, 0, 0, 0 };

    fprintf(stderr, "network: recv_map: welcome line...\n"); fflush(stderr);
    nlog("recv_map(%d)\n", fd);
    n = network_recv_line(fd, buf, sizeof(buf));
    nlog("recv_map: line=[%s] len=%d\n", buf, n);
    if (n <= 0) {
        fprintf(stderr, "network: recv_map: welcome line failed (n=%d)\n", n); fflush(stderr);
        return -1;
    }

    if (parse_action_values(buf, values) != 3 || values[0] != MSG_WELCOME) {
        fprintf(stderr, "network: recv_map: bad welcome [%s]\n", buf);
        fflush(stderr);
        return -1;
    }
    if (values[1] != WORLD_SIDE_LEN) {
        fprintf(stderr, "network: recv_map: bad world size %d\n", values[1]);
        fflush(stderr);
        return -1;
    }
    if (values[2] != NETWORK_VERSION) {
        fprintf(stderr,
                "network: recv_map: protocol version mismatch (%d != %d)\n",
                values[2], NETWORK_VERSION);
        fflush(stderr);
        return -1;
    }

    fprintf(stderr, "network: recv_map: receiving compressed world...\n"); fflush(stderr);
    nlog("recv_map: receiving compressed world...\n");
    if (recv_compressed_map(fd, map.type, map.group, map.info, map.pollution) < 0) {
        nlog("recv_map: compressed recv failed\n");
        fprintf(stderr, "network: recv_map: compressed recv failed\n"); fflush(stderr);
        return -1;
    }

    fprintf(stderr, "network: recv_map: receiving OK...\n"); fflush(stderr);
    nlog("recv_map: receiving OK...\n");
    n = network_recv_line(fd, buf, sizeof(buf));
    if (n <= 0) {
        nlog("recv_map: OK failed\n");
        fprintf(stderr, "network: recv_map: OK line failed (n=%d)\n", n); fflush(stderr);
        return -1;
    }
    if (parse_action_values(buf, values) != 1 || values[0] != MSG_OK) {
        fprintf(stderr, "network: expected OK after map, got [%s]\n", buf);
        fflush(stderr);
        return -1;
    }
    nlog("recv_map: done OK\n");
    fprintf(stderr, "network: recv_map: done OK\n"); fflush(stderr);
    return 0;
}

#define NETWORK_CLIENT_TX_BYTES 8192

static int client_tx_fd = -1;
static size_t client_tx_len = 0;
static char client_tx_buf[NETWORK_CLIENT_TX_BYTES];
static unsigned long client_tx_dropped = 0;
static int client_tx_resync_needed = 0;

void
network_client_reset_send_queue(void)
{
    client_tx_fd = -1;
    client_tx_len = 0;
    client_tx_resync_needed = 0;
}

unsigned long
network_client_dropped_actions(void)
{
    return client_tx_dropped;
}

int
network_client_resync_needed(void)
{
    return client_tx_resync_needed;
}

void
network_client_clear_resync_needed(void)
{
    client_tx_resync_needed = 0;
}

int
network_client_flush(int fd)
{
    if (fd < 0)
        return -1;
    if (client_tx_fd != fd) {
        client_tx_fd = fd;
        client_tx_len = 0;
    }
    while (client_tx_len > 0) {
        int n = SOCKCALL(send(fd, client_tx_buf, client_tx_len, SEND_FLAGS));
        if (n > 0) {
            memmove(client_tx_buf, client_tx_buf + n,
                    client_tx_len - (size_t)n);
            client_tx_len -= (size_t)n;
            continue;
        }
        if (n < 0 && (SOCK_ERRNO == SOCK_AGAIN
                      || SOCK_ERRNO == SOCK_EWOULDBLOCK))
            return 0;
        client_tx_len = 0;
        return -1;
    }
    return 0;
}

int
network_client_send_action(int fd, char type, int x, int y, int val)
{
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "%d %d %d %d\n", type, x, y, val);

    if (len < 0 || len >= (int)sizeof(buf))
        return -1;
    if (fd < 0)
        return -1;
    if (client_tx_fd != fd) {
        client_tx_fd = fd;
        client_tx_len = 0;
    }
    if ((size_t)len > sizeof(client_tx_buf) - client_tx_len) {
        client_tx_dropped++;
        client_tx_resync_needed = 1;
        fprintf(stderr, "network: client send queue full; action lost; "
                "reconnect to resync\n");
        fflush(stderr);
        return -1;
    }
    memcpy(client_tx_buf + client_tx_len, buf, (size_t)len);
    client_tx_len += (size_t)len;
    return network_client_flush(fd) < 0 ? -1 : 0;
}

/* Client requests are intentionally side-effect free.  The host is the only
   writer of the shared map; these helpers only put the request on the wire. */
int
network_client_request_bulldoze(int fd, int x, int y)
{
    return network_client_send_action(fd, MSG_BULLDOZE, x, y, 0);
}

int
network_client_request_mflags(int fd, int x, int y, int flags)
{
    return network_client_send_action(fd, MSG_MFLAGS, x, y, flags);
}

int
network_client_recv_msg(int fd, char *action, int *x, int *y, int *val)
{
    char buf[NETWORK_BUF_SIZE];
    fd_set rfds;
    struct timeval tv;
    int n;
    int values[4] = { 0, 0, 0, 0 };
    int nvalues;

    /* Poll: return 0 if no data (non-blocking check on a blocking socket) */
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    {
#ifdef WIN32
	int sr = select(0, &rfds, NULL, NULL, &tv);
#else
	int sr = select(fd + 1, &rfds, NULL, NULL, &tv);
#endif
	if (sr < 0) return 0;
	if (sr == 0) return 0;
    }

    n = network_recv_line(fd, buf, sizeof(buf));
    if (n <= 0)
        return -1;

    if (network_client_flush(fd) < 0)
        return -1;

    nvalues = parse_action_values(buf, values);
    if (nvalues < 1 || values[0] < 0 || values[0] > 255) {
        network_bad_client_action();
        return 0;
    }
    *action = (char)values[0];
    *x = values[1];
    *y = values[2];
    *val = values[3];
    return 1;
}

int
network_client_recv_info(int fd)
{
    char buf[128];
    int n, code;

    fprintf(stderr, "network_client_recv_info(%d)\n", fd); fflush(stderr);

    fprintf(stderr, "network: receiving compressed world...\n"); fflush(stderr);
    if (recv_compressed_map(fd, map.type, map.group, map.info, map.pollution) < 0) {
        fprintf(stderr, "network: recv compressed world failed\n"); fflush(stderr);
        return -1;
    }

    n = network_recv_line(fd, buf, sizeof(buf));
    if (n <= 0) {
        fprintf(stderr, "network: recv OK after info failed\n"); fflush(stderr);
        return -1;
    }
    if (sscanf(buf, "%d", &code) != 1 || code != MSG_OK) {
        fprintf(stderr, "network: expected OK after info, got [%s]\n", buf); fflush(stderr);
        return -1;
    }

    fprintf(stderr, "network: info received OK\n"); fflush(stderr);
    return 0;
}

void
network_client_disconnect(int fd)
{
    if (fd >= 0) {
        network_client_reset_send_queue();
        network_send_text(fd, "%d\n", MSG_DISCONN);
        CLOSE_SOCK(fd);
    }
    network_wsaclean();
}

/* ---------------------------------------------------------------------- *
 * Network init/cleanup (public, for main to call)
 * ---------------------------------------------------------------------- */
int
network_global_init(void)
{
    return network_wsastart();
}

void
network_global_cleanup(void)
{
    network_wsaclean();
}

void
network_close(int *fdp)
{
    if (fdp && *fdp >= 0) {
        CLOSE_SOCK(*fdp);
        *fdp = -1;
    }
}
