#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "network.h"

/* Only the host broadcast path is linked. */
int network_is_host = 1;
int network_socket = -1;
int network_extra_fds[3] = { -1, -1, -1 };
int network_num_clients = 0;

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static int
read_line (int fd, char *buf, size_t size)
{
    size_t got = 0;

    while (got + 1 < size) {
        ssize_t n = recv (fd, buf + got, 1, MSG_DONTWAIT);
        if (n < 0)
            return -1;
        if (n == 0)
            break;
        got += (size_t)n;
        if (buf[got - 1] == '\n')
            break;
    }
    buf[got] = '\0';
    return (int)got;
}

int
main (void)
{
    int sv[2];
    char line[128];

    CHECK (socketpair (AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    network_socket = sv[0];
    network_num_clients = 1;

    /* A failed drag/placement result must not enqueue MSG_BUILD. */
    CHECK (network_host_broadcast_build_if_placed (MSG_BUILD, 2, 2, 91, -1) == 0);
    CHECK (read_line (sv[1], line, sizeof (line)) < 0);
    CHECK (network_host_broadcast_build_if_placed (MSG_BUILD, 2, 2, 91, -3) == 0);
    CHECK (read_line (sv[1], line, sizeof (line)) < 0);

    /* A placed build is broadcast exactly as received. */
    network_host_broadcast_build_if_placed (MSG_BUILD, 2, 2, 71, 0);
    CHECK (read_line (sv[1], line, sizeof (line)) > 0);
    CHECK (strcmp (line, "66 2 2 71\n") == 0);

    /* A failed bulldoze result must not enqueue MSG_BULLDOZE. */
    CHECK (network_host_broadcast_bulldoze_if_removed (2, 2, 0) == 0);
    CHECK (read_line (sv[1], line, sizeof (line)) < 0);
    CHECK (network_host_broadcast_bulldoze_if_removed (2, 2, -1) == 0);
    CHECK (read_line (sv[1], line, sizeof (line)) < 0);

    /* A removed footprint is broadcast with a zero value. */
    network_host_broadcast_bulldoze_if_removed (2, 2, 2);
    CHECK (read_line (sv[1], line, sizeof (line)) > 0);
    CHECK (strcmp (line, "68 2 2 0\n") == 0);

    close (sv[0]);
    close (sv[1]);
    network_socket = -1;

    if (failures != 0) {
        fprintf (stderr, "%d action-gate test(s) failed\n", failures);
        return 1;
    }
    printf ("action gate: failed placement/bulldoze results are not broadcast\n");
    return 0;
}
