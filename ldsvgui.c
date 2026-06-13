/* ---------------------------------------------------------------------- *
 * ldsvgui.c
 * This file is part of lincity.
 * Lincity is copyright (c) I J Peters 1995-1997, (c) Greg Sharp 1997-2001.
 * ---------------------------------------------------------------------- */
#include "lcconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "lcstring.h"
#include "ldsvgui.h"
#include "lcintl.h"
#include "screen.h"
#include "pbar.h"
#include "module_buttons.h"
#include "fileutil.h"
#include "lchelp.h"

/* this is for OS/2 - RVI */
#ifdef __EMX__
#include <sys/select.h>
#include <X11/Xlibint.h>      /* required for __XOS2RedirRoot */
#define chown(x,y,z)
#define OS2_DEFAULT_LIBDIR "/XFree86/lib/X11/lincity"
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if defined (TIME_WITH_SYS_TIME)
#include <time.h>
#include <sys/time.h>
#else
#if defined (HAVE_SYS_TIME_H)
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif

#if defined (WIN32)
#include <winsock.h>
#if defined (__BORLANDC__)
#include <dir.h>
#include <dirent.h>
#include <dos.h>
#endif
#include <io.h>
#include <direct.h>
#include <process.h>
#endif

#if defined (HAVE_DIRENT_H)
#include <dirent.h>
#define NAMLEN(dirent) strlen((dirent)->d_name)
#else
#define dirent direct
#define NAMLEN(dirent) (dirent)->d_namlen
#if defined (HAVE_SYS_NDIR_H)
#include <sys/ndir.h>
#endif
#if defined (HAVE_SYS_DIR_H)
#include <sys/dir.h>
#endif
#if defined (HAVE_NDIR_H)
#include <ndir.h>
#endif
#endif

#include <ctype.h>
#include "common.h"
#ifdef LC_X11
#include <X11/cursorfont.h>
#endif
#include "lctypes.h"
#include "lin-city.h"
#include "cliglobs.h"
#include "engglobs.h"
#include "ldsvguts.h"
#include "fileutil.h"
#include "mouse.h"
#include "stats.h"
#ifdef NETWORK_ENABLE
#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#include "network.h"
static FILE *netlog_fp = NULL;
static void netlog_write(const char *fmt, ...) {
    if (!netlog_fp) netlog_fp = fopen("lincity_net.log", "w");
    if (netlog_fp) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(netlog_fp, fmt, ap);
        va_end(ap);
        fflush(netlog_fp);
    }
}
#define NETLOG(...) netlog_write(__VA_ARGS__)
#endif

/* ---------------------------------------------------------------------- *
 * Private Fn Prototypes
 * ---------------------------------------------------------------------- */
int verify_city (char *cname);
void input_network_host (char *s);
void input_network_port (char *s);

/* ---------------------------------------------------------------------- *
 * Private Global Variables
 * ---------------------------------------------------------------------- */
#if defined (WIN32)
extern char LIBDIR[];
#elif defined (__EMX__)
#ifdef LIBDIR
#undef LIBDIR   /* yes, I know I shouldn't ;-) */
#endif
char LIBDIR[256];
#endif

extern char *lc_save_dir;
extern char save_names[10][42];

/* ---------------------------------------------------------------------- *
 * Public Functions
 * ---------------------------------------------------------------------- */
void
draw_prefs_cb (void)
{
    Rect* mw = &scr.main_win;
    int x, y;
    char* graphic;

    x = mw->x + 50;
    y = mw->y + 30;
    graphic = overwrite_transport_flag ? 
	    checked_box_graphic : unchecked_box_graphic;
    Fgl_putbox (x, y, 16, 16, graphic);

    y += 16;
    graphic = suppress_popups ? unchecked_box_graphic : checked_box_graphic;
    Fgl_putbox (x, y, 16, 16, graphic);

    y += 16;
    graphic = time_multiplex_stats ? 
	    checked_box_graphic : unchecked_box_graphic;
    Fgl_putbox (x, y, 16, 16, graphic);

#if defined (LC_X11)
    y += 16;
    graphic = confine_flag ? checked_box_graphic : unchecked_box_graphic;
    Fgl_putbox (x, y, 16, 16, graphic);
#endif
    y += 16;
    graphic = !no_autosave ? checked_box_graphic : unchecked_box_graphic;
    Fgl_putbox (x, y, 16, 16, graphic);
}

void
do_prefs_buttons (int x, int y)
{
    int outx, outy, outh, outw;
    Rect* mw = &scr.main_win;
    if (x > mw->x + 50 && x < mw->x + 50 + 16) {
        if (y > mw->y + 30 && y < mw->y + 30 + 16) {
	    hide_mouse ();
	    overwrite_transport_flag = !overwrite_transport_flag;
	    draw_prefs_cb ();
	    redraw_mouse ();
	} else if (y > mw->y + 30 + 16 && y < mw->y + 30 + 2*16) {
	    hide_mouse ();
	    suppress_popups = !suppress_popups;
	    draw_prefs_cb ();
	    redraw_mouse ();
	} else if (y > mw->y + 30 + 2*16 && y < mw->y + 30 + 3*16) {
	    hide_mouse ();
	    time_multiplex_stats = !time_multiplex_stats;
	    draw_prefs_cb ();
	    redraw_mouse ();
#if defined (LC_X11)
	} else if (y > mw->y + 30 + 3*16 && y < mw->y + 30 + 4*16) {
	    hide_mouse ();
	    confine_flag = !confine_flag;
	    draw_prefs_cb ();
	    set_pointer_confinement ();
	    redraw_mouse ();
	} else if (y > mw->y + 30 + 4*16 && y < mw->y + 30 + 5*16) {
	    hide_mouse ();
	    no_autosave = !no_autosave;
	    draw_prefs_cb ();
	    redraw_mouse ();
#endif
	} else if (y > mw->y + 30 + 3*16 && y < mw->y + 30 + 4*16) {
	    hide_mouse ();
	    no_autosave = !no_autosave;
	    draw_prefs_cb ();
	    redraw_mouse ();
	}
    }
    outx = 370;
    outy = 387;
    outh = 12;
    outw = 3*8 + 4;
    if (x > mw->x + outx && x < mw->x + outx + outw &&
	y > mw->y + outy && y < mw->y + outy + outh)
    {
	close_prefs_screen ();
	refresh_main_screen ();
    }
}

void
do_prefs_mouse (int x, int y, int mbutton)
{
    Rect* mw = &scr.main_win;
    if (mouse_in_rect(mw, x, y)) {
	do_prefs_buttons (x, y);
	return;
    }
    /* If the user clicks outside of main window, cancel prefs?? */
    close_prefs_screen ();
    refresh_main_screen ();
}

void
do_prefs_screen (void)
{
    int x,y,w,h;
    Rect* mw = &scr.main_win;

    prefs_drawn_flag = 1;

    hide_mouse ();
    Fgl_fillbox (mw->x, mw->y, mw->w, mw->h, LOAD_BG_COLOUR);
    Fgl_setfontcolors (LOAD_BG_COLOUR, TEXT_FG_COLOUR);
    Fgl_write (mw->x + 80, mw->y + 4*8, _("Transport overwrite"));
    Fgl_write (mw->x + 80, mw->y + 6*8, _("Popup info to dialog boxes"));
    Fgl_write (mw->x + 80, mw->y + 8*8, _("Time multiplexed stats windows"));
#if defined (LC_X11)
    Fgl_write (mw->x + 80, mw->y + 10*8, _("Confine X pointer"));
    Fgl_write (mw->x + 80, mw->y + 12*8, _("Autosave"));
#else
    Fgl_write (mw->x + 80, mw->y + 10*8, _("Autosave"));
#endif

    x = 370;
    y = 387;
    h = 12;
    w = 3*8 + 4;
    Fgl_hline (mw->x + x, mw->y + y,
	       mw->x + x + w, HELPBUTTON_COLOUR);
    Fgl_hline (mw->x + x, mw->y + y + h, 
	       mw->x + x + w, HELPBUTTON_COLOUR);
    Fgl_line (mw->x + x, mw->y + y, 
	      mw->x + x, mw->y + y + h, HELPBUTTON_COLOUR);
    Fgl_line (mw->x + x + w, mw->y + y,
	      mw->x + x + w, mw->y + y + h, HELPBUTTON_COLOUR);
    Fgl_write (mw->x + x + 2, mw->y + y + 2, _("OUT"));

    draw_prefs_cb ();

    redraw_mouse ();
}

void
close_prefs_screen (void)
{
    save_lincityrc();

    prefs_flag = 0;
    prefs_drawn_flag = 0;
#ifdef USE_EXPANDED_FONT
    Fgl_setwritemode (WRITEMODE_OVERWRITE | FONT_EXPANDED);
#else
    Fgl_setfontcolors (TEXT_BG_COLOUR, TEXT_FG_COLOUR);
#endif
}

#if defined (NETWORK_ENABLE)
void
do_network_screen (void)
{
    Rect* mw = &scr.main_win;
    char host[256], port_str[64];
    int key = 0, choice = 0;
    int server_fd = -1, client_fd = -1;

    NETLOG("do_network_screen: entered\n");
    hide_mouse ();
    Fgl_fillbox (mw->x, mw->y, mw->w, mw->h, SAVE_BG_COLOUR);
    Fgl_setfontcolors (SAVE_BG_COLOUR, TEXT_FG_COLOUR);

    while (choice == 0) {
	Fgl_fillbox (mw->x, mw->y, mw->w, mw->h, SAVE_BG_COLOUR);
	Fgl_write (mw->x + 100, mw->y + 40,  _("1. Host a game"));
	Fgl_write (mw->x + 100, mw->y + 60,  _("2. Join a game"));
	Fgl_write (mw->x + 100, mw->y + 100, _("ESC - Cancel"));
	redraw_mouse ();

#if defined(LC_X11) || defined(WIN32)
	do {
	    call_event ();
	    key = x_key_value;
	} while (key == 0);
	x_key_value = 0;
#else
	key = getchar ();
#endif

	switch (key) {
	case '1':
	{
	    int host_choice = 0;

	    /* Sub-menu: new game or load game */
	    while (host_choice == 0) {
		Fgl_fillbox (mw->x, mw->y, mw->w, mw->h, SAVE_BG_COLOUR);
		Fgl_setfontcolors (SAVE_BG_COLOUR, TEXT_FG_COLOUR);
		Fgl_write (mw->x + 100, mw->y + 40,  _("1. Host a new game"));
		Fgl_write (mw->x + 100, mw->y + 60,  _("2. Host a saved game"));
		Fgl_write (mw->x + 100, mw->y + 100, _("ESC - Cancel"));
		redraw_mouse ();

#if defined(LC_X11) || defined(WIN32)
		do {
		    call_event ();
		    key = x_key_value;
		} while (key == 0);
		x_key_value = 0;
#else
		key = getchar ();
#endif

		switch (key) {
		case '1':
		    new_city (&main_screen_originx, &main_screen_originy, 1);
		    host_choice = 1;
		    break;
		case '2':
		    if (do_load_city () != 0)
			host_choice = 1;
		    break;
		case 27:
		    host_choice = -1;
		    break;
		}
	    }
	    if (host_choice < 0)
		break;

	    /* Choose max players (1=manual, 2-4=auto-start) */
	    network_max_clients = 2;
	    {
		int choosing = 1;
		char s[80];
		while (choosing) {
		    Fgl_fillbox (mw->x, mw->y, mw->w, mw->h, SAVE_BG_COLOUR);
		    Fgl_setfontcolors (SAVE_BG_COLOUR, TEXT_FG_COLOUR);
		    sprintf (s, _("Max players (1=manual, 2-4): %d"),
			     network_max_clients);
		    Fgl_write (mw->x + 100, mw->y + 40,  s);
		    Fgl_write (mw->x + 100, mw->y + 80,  _("+ / - to change"));
		    Fgl_write (mw->x + 100, mw->y + 100, _("ENTER to confirm"));
		    Fgl_write (mw->x + 100, mw->y + 120, _("ESC - Cancel"));
		    redraw_mouse ();
#if defined(LC_X11) || defined(WIN32)
		    do { call_event (); key = x_key_value; } while (key == 0);
		    x_key_value = 0;
#else
		    key = getchar ();
#endif
		    if (key == '+' || key == '=') {
			if (network_max_clients < MAX_CLIENTS)
			    network_max_clients++;
		    }
		    if (key == '-') {
			if (network_max_clients > 1)
			    network_max_clients--;
		    }
		    if (key == 13 || key == 10) choosing = 0;
		    if (key == 27) { choosing = 0; host_choice = -1; }
		}
	    }
	    if (host_choice < 0)
		break;

	    /* Start server and enter lobby */
	    NETLOG("host: starting server on port %d\n", DEFAULT_SOCK_PORT);
	    server_fd = network_host_start (DEFAULT_SOCK_PORT);
	    NETLOG("host: server_fd=%d\n", server_fd);
	    if (server_fd < 0) {
		NETLOG("host: FAILED to start server\n");
		Fgl_write (mw->x + 100, mw->y + 140, _("Failed to start server!"));
		redraw_mouse ();
		lc_usleep (2000);
		break;
	    }
	    {
		int cancel = 0;
		int lobby_active = 1;
		char s[80];

		network_socket = -1;
		network_num_clients = 0;
		network_is_host = 1;
		for (int i = 0; i < MAX_CLIENTS - 1; i++)
		    network_extra_fds[i] = -1;

		while (lobby_active) {
		    /* Try to accept new connections (non-blocking) */
		    {
			struct sockaddr_in cliaddr;
			socklen_t addrlen = sizeof(cliaddr);
			fd_set rfds;
			struct timeval tv;
			FD_ZERO(&rfds);
			FD_SET(server_fd, &rfds);
			tv.tv_sec = 0;
			tv.tv_usec = 100000;
		    if (select(server_fd + 1, &rfds, NULL, NULL, &tv) > 0) {
			    int new_fd = accept(server_fd, (struct sockaddr*)&cliaddr, &addrlen);
			    NETLOG("host: accept returned fd=%d, num_clients=%d max=%d\n",
				   new_fd, network_num_clients, network_max_clients);
			    if (new_fd >= 0 && network_num_clients < MAX_CLIENTS) {
				if (network_socket < 0)
				    network_socket = new_fd;
				else {
				    for (int i = 0; i < MAX_CLIENTS - 1; i++) {
					if (network_extra_fds[i] < 0) {
					    network_extra_fds[i] = new_fd;
					    break;
					}
				    }
				}
				network_num_clients++;
			    } else if (new_fd >= 0) {
				NETLOG("host: rejecting excess client\n");
				CLOSE_SOCK(new_fd);
			    }
			}
		    }

		    /* Draw lobby */
		    Fgl_fillbox (mw->x, mw->y, mw->w, mw->h, SAVE_BG_COLOUR);
		    Fgl_setfontcolors (SAVE_BG_COLOUR, TEXT_FG_COLOUR);
		    Fgl_write (mw->x + 100, mw->y + 40,  _("Lobby - waiting for players"));
		    Fgl_write (mw->x + 100, mw->y + 60,  _("Port 5123"));
		    sprintf (s, _("Connected: %d/%d"), network_num_clients, network_max_clients);
		    Fgl_write (mw->x + 100, mw->y + 80,  s);
		    if (network_num_clients < network_max_clients)
			Fgl_write (mw->x + 100, mw->y + 120, _("Waiting for more players..."));
		    else
			Fgl_write (mw->x + 100, mw->y + 120, _("All players connected!"));
		    Fgl_write (mw->x + 100, mw->y + 140, _("S - Start game"));
		    Fgl_write (mw->x + 100, mw->y + 160, _("ESC - Cancel"));
		    redraw_mouse ();

#if defined(LC_X11) || defined(WIN32)
		    call_event ();
		    if (x_key_value == 's' || x_key_value == 'S')
			lobby_active = 0;
		    if (x_key_value == 27) {
			cancel = 1;
			lobby_active = 0;
		    }
		    x_key_value = 0;
#endif
		    /* Auto-start when max players reached */
		    if (network_max_clients > 0
			&& network_num_clients >= network_max_clients)
			lobby_active = 0;
		}

		if (cancel) {
		    CLOSE_SOCK(network_socket);
		    for (int i = 0; i < MAX_CLIENTS - 1; i++)
			CLOSE_SOCK(network_extra_fds[i]);
		    network_num_clients = 0;
		    network_is_host = 0;
		    CLOSE_SOCK(server_fd);
		    break;
		}
	    }
	    /* Send map to all connected clients */
	    NETLOG("host: sending map to %d clients\n", network_num_clients);
	    int ms_ret = network_host_send_map_all ();
	    NETLOG("host: send_map_all returned %d\n", ms_ret);
	    if (ms_ret < 0) {
		NETLOG("host: MAP SEND FAILED!\n");
		Fgl_write (mw->x + 100, mw->y + 140, _("Map send failed!"));
		redraw_mouse ();
		lc_usleep (2000);
		CLOSE_SOCK(network_socket);
		for (int i = 0; i < MAX_CLIENTS - 1; i++)
		    CLOSE_SOCK(network_extra_fds[i]);
		CLOSE_SOCK(server_fd);
		break;
	    }
	    network_game = 1;
	    CLOSE_SOCK(server_fd);
	    NETLOG("host: game started, network_socket=%d\n", network_socket);
	    choice = 1;
	    break;
	}

	case '2':
	    strcpy (host, DEFAULT_SOCK_HOST);
	    sprintf (port_str, "%d", DEFAULT_SOCK_PORT);
	    Fgl_write (mw->x + 100, mw->y + 140, _("Enter host address:"));
	    redraw_mouse ();
	    input_network_host (host);
	    Fgl_write (mw->x + 100, mw->y + 180, _("Enter port:"));
	    redraw_mouse ();
	    input_network_port (port_str);
	    fprintf(stderr, "network: connecting to %s:%s\n", host, port_str); fflush(stderr);
	    NETLOG("client: connecting to %s:%d\n", host, atoi(port_str));
	    client_fd = network_client_connect (host, atoi (port_str));
	    NETLOG("client: connect returned fd=%d\n", client_fd);
	    if (client_fd < 0) {
		NETLOG("client: CONNECTION FAILED!\n");
		Fgl_write (mw->x + 100, mw->y + 220, _("Connection failed!"));
		redraw_mouse ();
		lc_usleep (2000);
		break;
	    }
	    /* Client lobby — wait for host to start game */
	    {
		int cancel = 0;
		int waiting = 1;
		while (waiting) {
		    Fgl_fillbox (mw->x, mw->y, mw->w, mw->h, SAVE_BG_COLOUR);
		    Fgl_setfontcolors (SAVE_BG_COLOUR, TEXT_FG_COLOUR);
		    Fgl_write (mw->x + 100, mw->y + 40,
			       _("Connected! Waiting for host to start..."));
		    Fgl_write (mw->x + 100, mw->y + 80, _("ESC - Cancel"));
		    redraw_mouse ();

		    /* Poll for host data (non-blocking) */
		    {
			fd_set rfds;
			struct timeval tv;
			FD_ZERO (&rfds);
			FD_SET (client_fd, &rfds);
			tv.tv_sec = 0;
			tv.tv_usec = 200000;
			if (select (client_fd + 1, &rfds, NULL, NULL, &tv) > 0)
			    waiting = 0;
		    }

#if defined(LC_X11) || defined(WIN32)
		    call_event ();
		    if (x_key_value == 27) {
			cancel = 1;
			waiting = 0;
		    }
		    x_key_value = 0;
#else
		    /* non-X11/non-WIN32: check for a byte on stdin to allow cancel */
		    {
			fd_set stdin_rfds;
			struct timeval stdin_tv;
			FD_ZERO (&stdin_rfds);
			FD_SET (0, &stdin_rfds);
			stdin_tv.tv_sec = 0;
			stdin_tv.tv_usec = 0;
			if (select (1, &stdin_rfds, NULL, NULL, &stdin_tv) > 0) {
			    char c;
			    if (read (0, &c, 1) > 0 && c == 27)
				{ cancel = 1; waiting = 0; }
			}
		    }
#endif
		}
		if (cancel) {
		    CLOSE_SOCK(client_fd);
		    break;
		}
	    }
	    /* Init game state before receiving map */
	    NETLOG("client: calling new_city\n");
	    new_city (&main_screen_originx, &main_screen_originy, 0);
	    NETLOG("client: receiving map...\n");
	    int mr_ret = network_client_recv_map (client_fd);
	    NETLOG("client: recv_map returned %d\n", mr_ret);
	    if (mr_ret < 0) {
		NETLOG("client: MAP RECEIVE FAILED!\n");
		Fgl_write (mw->x + 100, mw->y + 220, _("Map receive failed!"));
		redraw_mouse ();
		lc_usleep (2000);
		CLOSE_SOCK(client_fd);
		break;
	    }
	    network_game = 1;
	    network_is_host = 0;
	    network_socket = client_fd;
	    NETLOG("client: game started, socket=%d\n", network_socket);
	    choice = 1;
	    break;

	case 27:
	    NETLOG("network: ESC pressed, exiting\n");
	    new_city (&main_screen_originx, &main_screen_originy, 1);
	    network_flag = 0;
	    network_game = 0;
	    choice = -1;
	    break;
	}
    }

    NETLOG("do_network_screen: cleanup, choice=%d\n", choice);
    db_flag = 0;
    block_help_exit = 0;
    if (choice == -1) {
	activate_help ("opening.hlp");
    } else {
	help_flag = 0;
    }
    /* Ensure speed control is active */
    if (!pause_flag && !slow_flag && !fast_flag)
	med_flag = 1;
    cs_mouse_handler (0, -1, 0);
    cs_mouse_handler (0, 1, 0);
    hide_mouse ();
    Fgl_setfontcolors (TEXT_BG_COLOUR, TEXT_FG_COLOUR);
    refresh_main_screen ();
    redraw_mouse ();
    NETLOG("do_network_screen: returning\n");
}
#endif

void
do_save_city ()
{
    Rect* mw = &scr.main_win;
    char s[200], c;
    hide_mouse ();
    Fgl_fillbox (mw->x, mw->y, mw->w, mw->h
		 ,SAVE_BG_COLOUR);
    Fgl_setfontcolors (SAVE_BG_COLOUR, TEXT_FG_COLOUR);
    Fgl_write (mw->x + 100, mw->y + 15, _("Save a scene"));
    Fgl_write (mw->x + 8, mw->y + 35
	       ,_("Choose the number of the scene you want to save"));
    Fgl_write (mw->x + 110, mw->y + 210
	       ,_("Press space to cancel."));
    draw_save_dir (SAVE_BG_COLOUR);
    db_flag = 1;
#ifdef LC_X11
    redraw_mouse ();
    cs_mouse_handler (0, -1, 0);
    cs_mouse_handler (0, 1, 0);
    do
    {
	call_event ();
	c = x_key_value;
    }
    while (c == 0);
    x_key_value = 0;
#elif defined (WIN32)
    while (0 == (c = GetKeystroke ()));	/* Wait for keystroke */
    redraw_mouse ();
#else
    c = getchar ();
    redraw_mouse ();
#endif
    if (c > '0' && c <= '9')
    {
	Fgl_write (mw->x + 40, mw->y + 300
		   ,_("Type comment for the saved scene"));
	Fgl_write (mw->x + 16, mw->y + 310
		   ,_("The comment may be up to 40 characters"));
	Fgl_write (mw->x + 40, mw->y + 320
		   ,_("and may contain spaces or % . - + ,"));
	strcpy (s, &(save_names[c - '0'][2]));
	input_save_filename (s);
	remove_scene (save_names[c - '0']);
	sprintf (save_names[c - '0'], "%d_", c - '0');
	strcat (save_names[c - '0'], s);
	Fgl_fillbox (mw->x + 5, mw->y + 300
		     ,360, 30, SAVE_BG_COLOUR);
	Fgl_write (mw->x + 70, mw->y + 310
		   ,_("Saving city scene... please wait"));
	save_city (save_names[c - '0']);
    }
    db_flag = 0;
    cs_mouse_handler (0, -1, 0);
    cs_mouse_handler (0, 1, 0);
    hide_mouse ();
    Fgl_setfontcolors (TEXT_BG_COLOUR, TEXT_FG_COLOUR);
    save_flag = 0;
    refresh_main_screen ();
    redraw_mouse ();
}

void 
load_opening_city (char *s)
{
  char *cname = (char *) malloc (strlen (opening_path) + strlen (s) + 2);
  sprintf (cname, "%s%c%s", opening_path, PATH_SLASH, s);
  load_city (cname);
  free (cname);

  strcpy (given_scene, s);
  db_flag = 0;
  cs_mouse_handler (0, -1, 0);
  cs_mouse_handler (0, 1, 0);
  /* GCS:  Should I hide_mouse() here, as is done in do_load_city above? */
  hide_mouse ();
  Fgl_setfontcolors (TEXT_BG_COLOUR, TEXT_FG_COLOUR);
  refresh_main_screen ();
  suppress_ok_buttons = 1;
  update_avail_modules (0);
  suppress_ok_buttons = 0;
  /* GCS: ?? */
  redraw_mouse ();
}

int
do_load_city (void)
{
    Rect* mw = &scr.main_win;
    char c;
    int loaded = 0;
    hide_mouse ();
    Fgl_fillbox (mw->x, mw->y, mw->w, mw->h
		 ,LOAD_BG_COLOUR);
    Fgl_setfontcolors (LOAD_BG_COLOUR, TEXT_FG_COLOUR);
    Fgl_write (mw->x + 140, mw->y + 15, _("Load a file"));
    Fgl_write (mw->x + 40, mw->y + 35
	       ,_("Choose the number of the scene you want"));
    Fgl_write (mw->x + 40, mw->y + 50
	       ,_("Entries coloured red are either not there,"));
    Fgl_write (mw->x + 44, mw->y + 60
	       ,_("or they are from an earlier version, they"));
    Fgl_write (mw->x + 110, mw->y + 70
	       ,_("might not load properly."));
    Fgl_write (mw->x + 110, mw->y + 210
	       ,_("Press space to cancel."));
    draw_save_dir (LOAD_BG_COLOUR);
    db_flag = 1;

    do {
#ifdef LC_X11
	redraw_mouse ();
	cs_mouse_handler (0, -1, 0);
	cs_mouse_handler (0, 1, 0);
	do {
	    call_event ();
	    c = x_key_value;
	} while (c == 0);
	x_key_value = 0;
#elif defined (WIN32)
	while (0 == (c = GetKeystroke ()));	/* Wait for keystroke */
	redraw_mouse ();
#else
	c = getchar ();
	redraw_mouse ();
#endif
	if (c > '0' && c <= '9') {
	    if (strlen (save_names[c - '0']) < 1) {
		redraw_mouse ();
		if (yn_dial_box (_("No scene."),
				 _("There is no save scene with this number."),
				 _("Do you want to"),
				 _("try again?")) != 0)
		    c = 0;
		else
		    c = ' ';
		hide_mouse ();
	    }
	}
    } while (c==0);

    redraw_mouse ();
    if (c > '0' && c <= '9') {
	if (yn_dial_box (_("Loading Scene")
			 ,_("Do you want to load the scene")
			 ,save_names[c - '0']
			 ,_("and forget the current game?")) != 0)
	{
	    Fgl_write (mw->x + 70, mw->y + 310
		       ,_("Loading scene...  please wait"));
	    load_saved_city (save_names[c - '0']);
	    refresh_pbars();
	    loaded = 1;
	}
    }
    db_flag = 0;
    cs_mouse_handler (0, -1, 0);
    cs_mouse_handler (0, 1, 0);
    hide_mouse ();
    Fgl_setfontcolors (TEXT_BG_COLOUR, TEXT_FG_COLOUR);
    load_flag = 0;
    refresh_main_screen ();
    suppress_ok_buttons = 1;
    update_avail_modules (0);
    suppress_ok_buttons = 0;
    redraw_mouse ();
    return loaded;
}

void
draw_save_dir (int bg_colour)
{
    Rect* mw = &scr.main_win;
    char *s, s2[200];
    int i, j, l;
#if defined (WIN32)
    char filespec[8];
#if defined(_MSC_VER) || defined(__MINGW32__)
    struct _finddata_t fileinfo;
#elif defined (__BORLANDC__)
    struct ffblk fileinfo;
#endif
    long fh;
#else
    struct dirent *ep;
    DIR *dp;
#endif
    if ((s = (char *) malloc (lc_save_dir_len + strlen (LC_SAVE_DIR) + 64)) == 0)
	malloc_failure ();
    strcpy (s, lc_save_dir);
    if (!directory_exists (s))
    {
	printf (_("Couldn't find the save directory %s\n"), s);
	free (s);
	return;
    }
    /* GCS FIX:  Technically speaking, there is a race condition here. */
#if defined (WIN32)
    _chdir (s);
#else
    dp = opendir (s);
#endif
    for (i = 1; i < 10; i++)
    {
	save_names[i][0] = 0;
#if defined (WIN32)
	sprintf (filespec, "%d_*", i);
#if defined (_MSC_VER) || defined(__MINGW32__)
	fh = _findfirst (filespec, &fileinfo);
#elif defined (__BORLANDC__)
	fh = findfirst (filespec, &fileinfo, FA_ARCH);
#endif
	if (fh != -1)
	{
#else
	    while ((ep = readdir (dp)))	/* extra brackets to stop warning */

	    {
		if (*(ep->d_name) == (i + '0')
		    && *((ep->d_name) + 1) == '_')
		{
#endif
		    sprintf (s2, "%2d ", i);
#if defined (WIN32)
#if defined (_MSC_VER) || defined(__MINGW32__)
		    strncpy (save_names[i], fileinfo.name, 40);
#elif defined (__BORLANDC__)
		    strncpy (save_names[i], fileinfo.ff_name, 40);
#endif
#else /* UNIX */
		    strncpy (save_names[i], ep->d_name, 40);
#endif
		    if (strlen (save_names[i]) > 2)
			strncat (s2, &(save_names[i][2]), 40);
		    else
			strcat (s2, "???");
#if defined (WIN32)
#if defined (_MSC_VER) || defined(__MINGW32__)
		    _findclose (fh);
#elif defined (__BORLANDC__)
		    findclose(&fileinfo);
#endif
		}
#else
	    }
	}
#endif
	if (strlen (save_names[i]) < 1)
	    sprintf (s2, " %d .....", i);
	else
	{
	    l = strlen (s2);
	    for (j = 0; j < l; j++)
		if (s2[j] == '_')
		    s2[j] = ' ';
	}
	if (verify_city (save_names[i]) == 0)
	    Fgl_setfontcolors (bg_colour, red (28));
	else
	    Fgl_setfontcolors (bg_colour, green (28));
	Fgl_write (mw->x + 24, mw->y + 10 * (10 + i), s2);
#if !defined (WIN32)
	rewinddir (dp);
#endif
    }
#if defined (WIN32)
    _chdir (LIBDIR);		/* go back... */
#else
    closedir (dp);
#endif
    Fgl_setfontcolors (bg_colour, TEXT_FG_COLOUR);
    free (s);
}

void
edit_string (char* s, unsigned int maxlen, int xpos, int ypos)
{
    char c;
    int i, t, on;
    c = 0;
    s[maxlen+1] = 0;
    t = strlen (s);
    for (i = 0; i < t; i++)
	if (s[i] == '_')
	    s[i] = ' ';
    while (c != 0xd && c != 0xa)
    {
	Fgl_write (xpos, ypos, s);
	Fgl_write (xpos + (strlen (s) * 8), ypos, "_");
	on = 1;
	get_real_time ();
	t = real_time;
#ifdef LC_X11
	call_event ();
	while ((c = x_key_value) == 0)
#elif defined (WIN32)
	    while ((c = GetKeystroke ()) == 0)
#else
		while ((c = vga_getkey ()) == 0)
#endif
		{
#ifdef LC_X11
		    call_event ();
#endif
		    get_real_time ();
		    if (real_time > t + 200) {
			if (on == 1) {
			    Fgl_write (xpos + (strlen (s) * 8),
				       ypos, " ");
			    on = 0;
			} else {
			    Fgl_write (xpos + (strlen (s) * 8),
				       ypos, "_");
			    on = 1;
			}
			get_real_time ();
			t = real_time;
		    }
		}
#ifdef LC_X11
	x_key_value = 0;
#endif
	if ((isalnum (c) || c == ' ' || c == '.' || c == '%' || c == ','
	     || c == '-' || c == '+') && strlen (s) < maxlen)
	{
	    t = strlen (s);
	    s[t] = c;
	    s[t + 1] = 0;
	} 
	else if (c == 0x7f && strlen (s) > 0) 
	{
	    Fgl_write (xpos + (strlen (s) * 8), ypos, " ");
	    s[strlen (s) - 1] = 0;
	}
    }
    t = strlen (s);
    for (i = 0; i < t; i++)
	if (s[i] == ' ')
	    s[i] = '_';
}

void
input_save_filename (char *s)
{
    Rect* mw = &scr.main_win;
    edit_string (s, 40, mw->x + 24, mw->y + 340);
}

void
input_network_host (char *s)
{
    Rect* mw = &scr.main_win;
    Fgl_write (mw->x + 50, mw->y + 240, "Host:");
    edit_string (s, 40, mw->x + 124, mw->y + 240);
}

void
input_network_port (char *s)
{
    Rect* mw = &scr.main_win;
    Fgl_write (mw->x + 50, mw->y + 280, "Port:");
    edit_string (s, 40, mw->x + 124, mw->y + 280);
}


void
do_get_nw_server (void)
{
    Rect* mw = &scr.main_win;
    char c;
    hide_mouse ();
    Fgl_fillbox (mw->x, mw->y, mw->w, mw->h, NW_BG_COLOUR);
    Fgl_setfontcolors (LOAD_BG_COLOUR, TEXT_FG_COLOUR);
    Fgl_write (mw->x + 140, mw->y + 15, _("Choose network server"));
    Fgl_write (mw->x + 40, mw->y + 35
	       ,_("Please enter the address and port of the server"));
    Fgl_write (mw->x + 110, mw->y + 210
	       ,_("Press space to cancel."));
    draw_save_dir (NW_BG_COLOUR);
    do
    {
#ifdef LC_X11
	db_flag = 1;
	redraw_mouse ();
	cs_mouse_handler (0, -1, 0);
	cs_mouse_handler (0, 1, 0);
	do
	{
	    call_event ();
	    c = x_key_value;
	}
	while (c == 0);
	x_key_value = 0;
#elif defined (WIN32)
	while (0 == (c = GetKeystroke ()));	/* Wait for keystroke */
#else
	c = getchar ();
#endif
	if (c > '0' && c <= '9')
	    if (strlen (save_names[c - '0']) < 1)
	    {
		redraw_mouse ();
		if (yn_dial_box (_("No scene.")
				 ,_("There is no save scene with this number.")
				 ,_("Do you want to")
				 ,_("try again?")) != 0)
		    c = 0;
		else
		    c = ' ';
		hide_mouse ();
	    }
    }
    while ((c <= '0' || c > '9') && c != ' ');
    redraw_mouse ();
    if (c > '0' && c <= '9')
    {
	if (yn_dial_box (_("Loading Scene")
			 ,_("Do you want to load the scene")
			 ,save_names[c - '0']
			 ,_("and forget the current game?")) != 0)
	{
	    Fgl_write (mw->x + 70, mw->y + 310
		       ,_("Loading scene...  please wait"));
	    load_saved_city (save_names[c - '0']);
	}
    }
    db_flag = 0;
    cs_mouse_handler (0, -1, 0);
    cs_mouse_handler (0, 1, 0);
    hide_mouse ();
    Fgl_setfontcolors (TEXT_BG_COLOUR, TEXT_FG_COLOUR);
    refresh_main_screen ();
    suppress_ok_buttons = 1;
    update_avail_modules (0);
    suppress_ok_buttons = 0;
    redraw_mouse ();
}
