/*
 * P28 regression: client bulldoze/MFLAGS requests are host-authoritative.
 * The request helpers may only enqueue a protocol line; they must not touch
 * the local map.  If the host rejects the request, no broadcast arrives and
 * the client map therefore stays unchanged.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "engglobs.h"
#include "lctypes.h"
#include "network.h"

Map map;

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
    size_t n = 0;

    while (n + 1 < size) {
        ssize_t r = read (fd, buf + n, 1);
        if (r == 1) {
            if (buf[n++] == '\n') {
                buf[n] = '\0';
                return (int)n;
            }
        } else if (r < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    buf[n] = '\0';
    return (int)n;
}

int
main (void)
{
    int sv[2];
    char line[128];
    int type_before;
    int flags_before;

    memset (&map, 0, sizeof map);
    map.type[2][2] = CST_MARKET_EMPTY;
    map.group[2][2] = GROUP_MARKET;
    map.info[2][2].flags = 0;
    type_before = map.type[2][2];
    flags_before = map.info[2][2].flags;

    CHECK (socketpair (AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    CHECK (network_client_request_bulldoze (sv[0], 2, 2) == 0);
    CHECK (read_line (sv[1], line, sizeof line) > 0);
    CHECK (strcmp (line, "68 2 2 0\n") == 0);
    CHECK (map.type[2][2] == type_before);
    CHECK (map.info[2][2].flags == flags_before);

    CHECK (network_client_request_mflags (sv[0], 2, 2, 0x123456) == 0);
    CHECK (read_line (sv[1], line, sizeof line) > 0);
    CHECK (strcmp (line, "70 2 2 1193046\n") == 0);
    CHECK (map.type[2][2] == type_before);
    CHECK (map.info[2][2].flags == flags_before);

    close (sv[0]);
    close (sv[1]);
    if (failures != 0) {
        fprintf (stderr, "%d client-request test(s) failed\n", failures);
        return 1;
    }
    printf ("client requests: bulldoze/MFLAGS send without local mutation\n");
    return 0;
}
