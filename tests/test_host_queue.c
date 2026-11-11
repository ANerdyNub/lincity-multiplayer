#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "network.h"

/* Only the host enqueue path is linked; these globals are enough for it. */
int network_is_host = 1;
int network_socket = -1;
int network_extra_fds[3] = { -1, -1, -1 };

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

int
main (void)
{
    int sv[2];
    int flags;
    int i;
    int sent = 0;
    int full = 0;
    char action = 0;
    int x = 0, y = 0, val = 0;

    CHECK (socketpair (AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    flags = fcntl (sv[0], F_GETFL, 0);
    CHECK (flags >= 0);
    CHECK (fcntl (sv[0], F_SETFL, flags | O_NONBLOCK) == 0);
    {
        int small = 4096;
        CHECK (setsockopt (sv[0], SOL_SOCKET, SO_SNDBUF,
                           &small, sizeof (small)) == 0);
    }

    network_socket = sv[0];
    for (i = 0; i < 100000; i++) {
        if (network_send_raw (sv[0], "x", 1) < 0) {
            full = 1;
            break;
        }
        sent++;
    }
    CHECK (sent > 0);
    CHECK (full);

    /* A full queue marks the slot dead; the host loop sees -1 and closes it. */
    CHECK (network_host_recv_action (sv[0], &action, &x, &y, &val) < 0);

    close (sv[0]);
    close (sv[1]);
    network_socket = -1;
    if (failures != 0) {
        fprintf (stderr, "%d host-queue test(s) failed\n", failures);
        return 1;
    }
    printf ("host queue: full queue marks the client for disconnect\n");
    return 0;
}
