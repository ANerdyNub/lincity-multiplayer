/* ---------------------------------------------------------------------- *
 * network.h
 * This file is part of lincity.
 * ---------------------------------------------------------------------- */
#ifndef NETWORK_H
#define NETWORK_H

#ifndef CLOSE_SOCK
#ifdef WIN32
#define CLOSE_SOCK(fd) do { if ((fd) >= 0) { closesocket((SOCKET)(fd)); (fd) = -1; } } while(0)
#else
#define CLOSE_SOCK(fd) do { if ((fd) >= 0) { close((fd)); (fd) = -1; } } while(0)
#endif
#endif

#define DEFAULT_SOCK_HOST "localhost"
#define DEFAULT_SOCK_PORT 5123
#define NETWORK_PORT 5123
#define NETWORK_VERSION 1
#define NETWORK_BUF_SIZE 4096

/* Protocol message types (first byte of text message) */
#define MSG_HELLO    'H'
#define MSG_WELCOME  'W'
#define MSG_BUILD    'B'
#define MSG_BULLDOZE 'D'
#define MSG_MONEY    'M'
#define MSG_POP      'P'
#define MSG_YEAR     'Y'
#define MSG_SPEED    'S'
#define MSG_TECH     'T'
#define MSG_QUIT     'Q'
#define MSG_OK       'K'
#define MSG_DISCONN  'X'
#define MSG_INFO     'I'
#define MSG_MFLAGS   'F'

/* Drag-pause: client→host, request freeze while dragging roads */
#define MSG_DRAG_START 'R'
#define MSG_DRAG_END   'r'

/* Stats broadcast (carry 2 values each: (msg, mx, my)) */
#define MSG_STAT1    '1'
#define MSG_STAT2    '2'
#define MSG_STAT3    '3'
#define MSG_STAT4    '4'
#define MSG_STAT5    '5'
#define MSG_STAT6    '6'

/* Yearly/last-year stats for finance and other-costs tabs */
#define MSG_YTAX1    'a'   /* ly_income_tax, ly_coal_tax */
#define MSG_YTAX2    'b'   /* ly_goods_tax, ly_export_tax */
#define MSG_YCST1    'c'   /* ly_transport_cost, ly_unemployment_cost */
#define MSG_YCST2    'd'   /* ly_import_cost, ly_other_cost */
#define MSG_YCST3    'e'   /* ly_university_cost, ly_recycle_cost */
#define MSG_YCST4    'f'   /* ly_deaths_cost, ly_health_cost */
#define MSG_YCST5    'g'   /* ly_school_cost, ly_windmill_cost */
#define MSG_YCST6    'h'   /* ly_rocket_pad_cost, ly_cricket_cost */
#define MSG_YINT     'i'   /* ly_interest, ly_fire_cost */

/* ---- Server (host) functions ---- */
int network_host_start(int port);
int network_host_wait_client(int server_fd);
int network_host_send_map(int client_fd);
int network_host_broadcast_action(int client_fd, char type, int x, int y, int val);
int network_host_broadcast_money(int client_fd);
int network_host_broadcast_pop(int client_fd);
int network_host_broadcast_year(int client_fd);
int network_host_broadcast_speed(int client_fd);
int network_host_broadcast_tech(int client_fd);
void network_host_broadcast_stats(int client_fd);
int network_host_send_info(int client_fd);
int network_host_recv_action(int client_fd, char *action, int *x, int *y, int *val);
void network_host_stop(int server_fd, int client_fd);

/* ---- Client functions ---- */
int network_client_connect(const char *host, int port);
int network_client_recv_map(int fd);
int network_client_send_action(int fd, char type, int x, int y, int val);
int network_client_recv_msg(int fd, char *action, int *x, int *y, int *val);
int network_client_recv_info(int fd);
void network_client_disconnect(int fd);

/* ---- Global init/cleanup (WSAStartup/WSACleanup for Windows) ---- */
int network_global_init(void);
void network_global_cleanup(void);

/* ---- Shared utility ---- */
int network_send_raw(int fd, const char *buf, int len);
int network_recv_raw(int fd, char *buf, int len);
int network_send_text(int fd, const char *fmt, ...);
int network_recv_line(int fd, char *buf, int bufsize);
void network_close(int *fdp);

/* ---- Multi-client helpers ---- */
int network_host_broadcast_action_all(char type, int x, int y, int val);
int network_host_send_map_all(void);
void network_host_broadcast_money_all(void);
void network_host_broadcast_pop_all(void);
void network_host_broadcast_year_all(void);
void network_host_broadcast_speed_all(void);
void network_host_broadcast_tech_all(void);
void network_host_broadcast_stats_all(void);
void network_host_broadcast_yearly_stats_all(void);
int network_host_send_info_all(void);

#endif /* NETWORK_H */
