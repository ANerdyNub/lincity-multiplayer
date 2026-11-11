#include <stdio.h>

#include "network.h"

/* Only the host fd bookkeeping path is linked. */
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

int
main (void)
{
    /* -1 is not a client.  It must not match a free extra slot or
       decrement the count (the original H1 free-slot pattern). */
    network_socket = 11;
    network_extra_fds[0] = -1;
    network_extra_fds[1] = -1;
    network_extra_fds[2] = -1;
    network_num_clients = 1;
    CHECK (network_host_drop_client (-1) == 0);
    CHECK (network_socket == 11);
    CHECK (network_extra_fds[0] == -1);
    CHECK (network_extra_fds[1] == -1);
    CHECK (network_extra_fds[2] == -1);
    CHECK (network_num_clients == 1);
    CHECK (network_host_drop_client (11) == 1);
    CHECK (network_socket == -1);
    CHECK (network_num_clients == 0);

    /* Primary client disconnect decrements exactly once. */
    network_socket = 5;
    network_extra_fds[0] = 6;
    network_extra_fds[1] = -1;
    network_extra_fds[2] = -1;
    network_num_clients = 2;
    CHECK (network_host_drop_client (5) == 1);
    CHECK (network_socket == -1);
    CHECK (network_extra_fds[0] == 6);
    CHECK (network_num_clients == 1);

    /* A duplicate fd snapshot must not decrement the count again. */
    CHECK (network_host_drop_client (5) == 0);
    CHECK (network_num_clients == 1);

    /* Extra client disconnect, then a reused fd that was never tracked. */
    CHECK (network_host_drop_client (6) == 1);
    CHECK (network_extra_fds[0] == -1);
    CHECK (network_num_clients == 0);
    CHECK (network_host_drop_client (6) == 0);
    CHECK (network_num_clients == 0);
    CHECK (network_host_drop_client (7) == 0);
    CHECK (network_num_clients == 0);

    /* A reused fd in the primary slot is counted once when admitted. */
    network_socket = 9;
    network_num_clients = 1;
    CHECK (network_host_drop_client (9) == 1);
    CHECK (network_socket == -1);
    CHECK (network_num_clients == 0);

    if (failures != 0) {
        fprintf (stderr, "%d host-fd test(s) failed\n", failures);
        return 1;
    }
    printf ("host fds: drop clears one slot and never makes the count negative\n");
    return 0;
}
