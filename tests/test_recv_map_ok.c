/*
 * L5 regression: network_client_recv_map() must validate the final MSG_OK,
 * just as network_client_recv_info() does.  A valid map followed by a
 * different message is a failed receive.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zlib.h>

#include "engglobs.h"
#include "lctypes.h"
#include "lintypes.h"
#include "mapwire.h"
#include "network.h"

Map map;
struct GROUP main_groups[NUM_OF_GROUPS];

/* The receive path only sees empty green tiles in this test. */
int
get_group_of_type (short type)
{
    (void)type;
    return GROUP_BARE;
}

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static int
write_all (int fd, const void *data, size_t len)
{
    const unsigned char *p = data;

    while (len > 0) {
        ssize_t n = write (fd, p, len);
        if (n > 0) {
            p += n;
            len -= (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return 0;
}

static int
send_stream (int fd, const char *final_line)
{
    unsigned char *wire = NULL;
    unsigned char *comp = NULL;
    unsigned char hdr[8];
    char welcome[64];
    uLongf csz;
    int len;
    int result = -1;
    int x, y;

    memset (map.type, 0, sizeof (map.type));
    memset (map.group, 0, sizeof (map.group));
    memset (map.info, 0, sizeof (map.info));
    memset (map.pollution, 0, sizeof (map.pollution));
    for (x = 0; x < WORLD_SIDE_LEN; x++) {
        for (y = 0; y < WORLD_SIDE_LEN; y++) {
            map.type[x][y] = CST_GREEN;
            map.group[x][y] = GROUP_BARE;
        }
    }

    wire = (unsigned char *)malloc (MAP_WIRE_BYTES);
    if (wire == NULL
        || map_wire_encode (WORLD_SIDE_LEN, map.type, map.group, map.info,
                            map.pollution, wire)
           != wire + MAP_WIRE_BYTES)
        goto out;
    csz = compressBound ((uLong)MAP_WIRE_BYTES);
    comp = (unsigned char *)malloc (csz);
    if (comp == NULL
        || compress (comp, &csz, wire, (uLong)MAP_WIRE_BYTES) != Z_OK)
        goto out;

    map_put_u32le (hdr, (uint32_t)MAP_WIRE_BYTES);
    map_put_u32le (hdr + 4, (uint32_t)csz);
    len = snprintf (welcome, sizeof (welcome), "%d %d %d\n",
                    MSG_WELCOME, WORLD_SIDE_LEN, NETWORK_VERSION);
    if (len <= 0 || len >= (int)sizeof (welcome))
        goto out;
    if (write_all (fd, welcome, (size_t)len) < 0
        || write_all (fd, hdr, sizeof (hdr)) < 0
        || write_all (fd, comp, (size_t)csz) < 0
        || write_all (fd, final_line, strlen (final_line)) < 0)
        goto out;
    result = 0;

out:
    free (comp);
    free (wire);
    return result;
}

static int
receive_map (const char *final_line)
{
    int sv[2];
    int ret;

    CHECK (socketpair (AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    if (send_stream (sv[0], final_line) < 0) {
        close (sv[0]);
        close (sv[1]);
        return -2;
    }
    CHECK (shutdown (sv[0], SHUT_WR) == 0);
    ret = network_client_recv_map (sv[1]);
    close (sv[0]);
    close (sv[1]);
    return ret;
}

int
main (void)
{
    main_groups[GROUP_BARE].size = 1;
    CHECK (receive_map ("75\n") == 0);   /* MSG_OK */
    CHECK (receive_map ("88\n") == -1);  /* MSG_DISCONN */

    if (failures != 0) {
        fprintf (stderr, "%d recv-map test(s) failed\n", failures);
        return 1;
    }
    printf ("recv map: final OK is required\n");
    return 0;
}
