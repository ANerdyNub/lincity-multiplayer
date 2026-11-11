#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "network.h"

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
    int sent = 0;
    int i;
    char drain[4096];
    unsigned long dropped;

    CHECK (socketpair (AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    flags = fcntl (sv[0], F_GETFL, 0);
    CHECK (flags >= 0);
    CHECK (fcntl (sv[0], F_SETFL, flags | O_NONBLOCK) == 0);
    {
        int small = 4096;
        CHECK (setsockopt (sv[0], SOL_SOCKET, SO_SNDBUF,
                           &small, sizeof (small)) == 0);
    }

    network_client_reset_send_queue ();
    for (i = 0; i < 100000 && network_client_dropped_actions () == 0; i++) {
        if (network_client_send_action (sv[0], 'B', i, i, i) < 0) {
            /* A full queue is the expected explicit drop path. */
            sent++;
        } else {
            sent++;
        }
    }
    dropped = network_client_dropped_actions ();
    CHECK (sent > 0);
    CHECK (dropped > 0);
    CHECK (network_client_resync_needed () != 0);

    /* Drain the receiver and retry; a later action must be delivered. */
    while (recv (sv[1], drain, sizeof (drain), MSG_DONTWAIT) > 0)
        ;
    CHECK (network_client_send_action (sv[0], 'D', 1, 2, 3) >= 0);
    CHECK (network_client_flush (sv[0]) == 0);
    CHECK (recv (sv[1], drain, sizeof (drain), 0) > 0);

    close (sv[0]);
    close (sv[1]);
    if (failures != 0) {
        fprintf (stderr, "%d send-queue test(s) failed\n", failures);
        return 1;
    }
    printf ("send queue: drops are counted, resync is requested, and delivery resumes\n");
    return 0;
}
