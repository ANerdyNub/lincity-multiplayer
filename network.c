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
#include "lin-city.h"
#include "engglobs.h"
#include "cliglobs.h"
#include "network.h"
#include "stats.h"

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
#define CLOSE_SOCK(fd) closesocket((SOCKET)(fd))
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
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#define SOCKCALL(r)    (r)
#define CLOSE_SOCK(fd) close(fd)
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
int
network_send_raw(int fd, const char *buf, int len)
{
    int sent = 0, n;
    while (sent < len) {
        n = SOCKCALL(send(fd, buf + sent, len - sent, NO_SIGNAL_SEND_FLAGS));
        if (n <= 0)
            return -1;
        sent += n;
    }
    return sent;
}

int
network_recv_raw(int fd, char *buf, int len)
{
    int got = 0, n;
    while (got < len) {
        n = SOCKCALL(recv(fd, buf + got, len - got, 0));
        if (n > 0) {
            got += n;
            continue;
        }
        if (n == 0)
            return -1;
        /* Non-blocking: retry on EAGAIN/EWOULDBLOCK */
#ifdef WIN32
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
#endif
            struct timeval tv = { .tv_sec = 0, .tv_usec = 10000 };
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
#ifdef WIN32
            select(0, &rfds, NULL, NULL, &tv);
#else
            select(fd + 1, &rfds, NULL, NULL, &tv);
#endif
            continue;
        }
        return -1;
    }
    return got;
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

    while (i < bufsize - 1) {
        int n = SOCKCALL(recv(fd, &c, 1, 0));
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
        /* Non-blocking: retry on EAGAIN/EWOULDBLOCK */
#ifdef WIN32
        int wsa_err = WSAGetLastError();
        if (wsa_err == WSAEWOULDBLOCK) {
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
#endif
            struct timeval tv = { .tv_sec = 0, .tv_usec = 10000 };
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);
#ifdef WIN32
            select(0, &rfds, NULL, NULL, &tv);
#else
            select(fd + 1, &rfds, NULL, NULL, &tv);
#endif
            continue;
        }
#ifdef WIN32
        fprintf(stderr, "network: recv_line: error fd=%d err=%d\n", fd, wsa_err); fflush(stderr);
#else
        fprintf(stderr, "network: recv_line: error fd=%d err=%d\n", fd, errno); fflush(stderr);
#endif
        return -1;
    }
    buf[i] = '\0';
    return i;
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
    if (listen(fd, 1) < 0) {
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
    return fd;
}

void
network_host_stop(int server_fd, int client_fd)
{
    if (client_fd >= 0) {
        network_send_text(client_fd, "%d\n", MSG_QUIT);
        CLOSE_SOCK(client_fd);
    }
    if (server_fd >= 0)
        CLOSE_SOCK(server_fd);
    network_wsaclean();
}

static int
send_array_raw(int fd, short arr[WORLD_SIDE_LEN][WORLD_SIDE_LEN])
{
    return network_send_raw(fd, (char*)arr, sizeof(short) * WORLD_SIDE_LEN * WORLD_SIDE_LEN);
}

int
network_host_send_map(int client_fd)
{
    char buf[128];

    nlog("send_map(%d)\n", client_fd);
    snprintf(buf, sizeof(buf), "%d %d\n", MSG_WELCOME, WORLD_SIDE_LEN);
    if (network_send_raw(client_fd, buf, strlen(buf)) < 0) {
        nlog("send_map: welcome failed\n");
        return -1;
    }
    nlog("send_map: welcome sent\n");

    nlog("send_map: sending type...\n");
    if (send_array_raw(client_fd, map.type) < 0) {
        nlog("send_map: type failed\n");
        return -1;
    }
    nlog("send_map: sending group...\n");
    if (send_array_raw(client_fd, map.group) < 0) {
        nlog("send_map: group failed\n");
        return -1;
    }

    nlog("send_map: sending info...\n");
    if (network_send_raw(client_fd, (char*)map.info, sizeof(Map_Point_Info) * WORLD_SIDE_LEN * WORLD_SIDE_LEN) < 0) {
        nlog("send_map: info failed\n");
        return -1;
    }
    nlog("send_map: sending pollution...\n");
    if (network_send_raw(client_fd, (char*)map.pollution, sizeof(int) * WORLD_SIDE_LEN * WORLD_SIDE_LEN) < 0) {
        nlog("send_map: pollution failed\n");
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

    fprintf(stderr, "network: sending type array...\n"); fflush(stderr);
    if (send_array_raw(client_fd, map.type) < 0) {
        fprintf(stderr, "network: send type array failed\n"); fflush(stderr);
        return -1;
    }

    fprintf(stderr, "network: sending group array...\n"); fflush(stderr);
    if (send_array_raw(client_fd, map.group) < 0) {
        fprintf(stderr, "network: send group array failed\n"); fflush(stderr);
        return -1;
    }

    fprintf(stderr, "network: sending info array...\n"); fflush(stderr);
    if (network_send_raw(client_fd, (char*)map.info, sizeof(Map_Point_Info) * WORLD_SIDE_LEN * WORLD_SIDE_LEN) < 0) {
        fprintf(stderr, "network: send info array failed\n"); fflush(stderr);
        return -1;
    }

    fprintf(stderr, "network: sending pollution array...\n"); fflush(stderr);
    if (network_send_raw(client_fd, (char*)map.pollution, sizeof(int) * WORLD_SIDE_LEN * WORLD_SIDE_LEN) < 0) {
        fprintf(stderr, "network: send pollution array failed\n"); fflush(stderr);
        return -1;
    }

    if (network_send_text(client_fd, "%d\n", MSG_OK) < 0) {
        fprintf(stderr, "network: send OK after info failed\n"); fflush(stderr);
        return -1;
    }

    fprintf(stderr, "network: info sent OK\n"); fflush(stderr);
    return 0;
}

int
network_host_recv_action(int client_fd, char *action, int *x, int *y, int *val)
{
    char buf[NETWORK_BUF_SIZE];
    fd_set rfds;
    struct timeval tv;
    int n, msg;

    /* Poll: return 0 if no data (non-blocking check on a blocking socket) */
    FD_ZERO(&rfds);
    FD_SET(client_fd, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    {
#ifdef WIN32
	int sr = select(0, &rfds, NULL, NULL, &tv);
#else
	int sr = select(client_fd + 1, &rfds, NULL, NULL, &tv);
#endif
	if (sr <= 0) return 0;
    }

    n = network_recv_line(client_fd, buf, sizeof(buf));
    if (n <= 0)
	return -1;

    *x = *y = *val = 0;
    if (sscanf(buf, "%d %d %d %d", &msg, x, y, val) >= 2) {
        *action = (char)msg;
        return 1;
    }
    return 0;
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

#ifdef WIN32
    /* Set client socket to non-blocking so sends from mouse handler don't hang */
    {
        u_long mode = 1;
        ioctlsocket((SOCKET)fd, FIONBIO, &mode);
    }
#endif

    return fd;
}

static int
recv_array_raw(int fd, short arr[WORLD_SIDE_LEN][WORLD_SIDE_LEN])
{
    return network_recv_raw(fd, (char*)arr, sizeof(short) * WORLD_SIDE_LEN * WORLD_SIDE_LEN);
}

int
network_client_recv_map(int fd)
{
    char buf[128];
    int n, ws, code;

    fprintf(stderr, "network: recv_map: welcome line...\n"); fflush(stderr);
    nlog("recv_map(%d)\n", fd);
    n = network_recv_line(fd, buf, sizeof(buf));
    nlog("recv_map: line=[%s] len=%d\n", buf, n);
    if (n <= 0) {
        fprintf(stderr, "network: recv_map: welcome line failed (n=%d)\n", n); fflush(stderr);
        return -1;
    }

    if (sscanf(buf, "%d %d", &code, &ws) < 2 || code != MSG_WELCOME) {
        nlog("recv_map: bad welcome code=%d ws=%d\n", code, ws);
        fprintf(stderr, "network: recv_map: bad welcome code=%d ws=%d\n", code, ws); fflush(stderr);
        return -1;
    }
    if (ws != WORLD_SIDE_LEN) {
        fprintf(stderr, "network: recv_map: bad world size %d\n", ws); fflush(stderr);
        return -1;
    }

    fprintf(stderr, "network: recv_map: receiving type...\n"); fflush(stderr);
    nlog("recv_map: receiving type...\n");
    if (recv_array_raw(fd, map.type) < 0) {
        nlog("recv_map: type failed\n");
        fprintf(stderr, "network: recv_map: type failed\n"); fflush(stderr);
        return -1;
    }
    fprintf(stderr, "network: recv_map: receiving group...\n"); fflush(stderr);
    nlog("recv_map: receiving group...\n");
    if (recv_array_raw(fd, map.group) < 0) {
        nlog("recv_map: group failed\n");
        fprintf(stderr, "network: recv_map: group failed\n"); fflush(stderr);
        return -1;
    }

    fprintf(stderr, "network: recv_map: receiving info...\n"); fflush(stderr);
    nlog("recv_map: receiving info...\n");
    if (network_recv_raw(fd, (char*)map.info, sizeof(Map_Point_Info) * WORLD_SIDE_LEN * WORLD_SIDE_LEN) < 0) {
        nlog("recv_map: info failed\n");
        fprintf(stderr, "network: recv_map: info failed\n"); fflush(stderr);
        return -1;
    }
    fprintf(stderr, "network: recv_map: receiving pollution...\n"); fflush(stderr);
    nlog("recv_map: receiving pollution...\n");
    if (network_recv_raw(fd, (char*)map.pollution, sizeof(int) * WORLD_SIDE_LEN * WORLD_SIDE_LEN) < 0) {
        nlog("recv_map: pollution failed\n");
        fprintf(stderr, "network: recv_map: pollution failed\n"); fflush(stderr);
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
    nlog("recv_map: done OK\n");
    fprintf(stderr, "network: recv_map: done OK\n"); fflush(stderr);
    return 0;
}

int
network_client_send_action(int fd, char type, int x, int y, int val)
{
    /* Non-blocking send — small text msgs only.  If the kernel buffer is
       full the msg is silently dropped; callers handle this gracefully. */
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "%d %d %d %d\n", type, x, y, val);
    if (len < 0 || len >= (int)sizeof(buf))
        return -1;
    int n = SOCKCALL(send(fd, buf, len, SEND_FLAGS));
    if (n < 0 && (SOCK_ERRNO == SOCK_AGAIN || SOCK_ERRNO == SOCK_EWOULDBLOCK))
        return 0; /* pretend success — msg was best-effort */
    if (n <= 0)
        return -1;
    return n;
}

int
network_client_recv_msg(int fd, char *action, int *x, int *y, int *val)
{
    char buf[NETWORK_BUF_SIZE];
    fd_set rfds;
    struct timeval tv;
    int n, msg;

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

    *x = *y = *val = 0;
    if (sscanf(buf, "%d %d %d %d", &msg, x, y, val) >= 1) {
        *action = (char)msg;
        return 1;
    }
    return 0;
}

int
network_client_recv_info(int fd)
{
    char buf[128];
    int n, code;

    fprintf(stderr, "network_client_recv_info(%d)\n", fd); fflush(stderr);

    fprintf(stderr, "network: receiving type array...\n"); fflush(stderr);
    if (recv_array_raw(fd, map.type) < 0) {
        fprintf(stderr, "network: recv type array failed\n"); fflush(stderr);
        return -1;
    }

    fprintf(stderr, "network: receiving group array...\n"); fflush(stderr);
    if (recv_array_raw(fd, map.group) < 0) {
        fprintf(stderr, "network: recv group array failed\n"); fflush(stderr);
        return -1;
    }

    fprintf(stderr, "network: receiving info array...\n"); fflush(stderr);
    if (network_recv_raw(fd, (char*)map.info, sizeof(Map_Point_Info) * WORLD_SIDE_LEN * WORLD_SIDE_LEN) < 0) {
        fprintf(stderr, "network: recv info array failed\n"); fflush(stderr);
        return -1;
    }

    fprintf(stderr, "network: receiving pollution array...\n"); fflush(stderr);
    if (network_recv_raw(fd, (char*)map.pollution, sizeof(int) * WORLD_SIDE_LEN * WORLD_SIDE_LEN) < 0) {
        fprintf(stderr, "network: recv pollution array failed\n"); fflush(stderr);
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
