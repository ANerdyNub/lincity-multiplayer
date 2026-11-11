#include <stdio.h>
#include <string.h>

#include "mapwire.h"

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static short type[WORLD_SIDE_LEN][WORLD_SIDE_LEN];
static short group[WORLD_SIDE_LEN][WORLD_SIDE_LEN];
static Map_Point_Info info[WORLD_SIDE_LEN][WORLD_SIDE_LEN];
static int pollution[WORLD_SIDE_LEN][WORLD_SIDE_LEN];

int
main (void)
{
    static const unsigned char expected[192] = {
        0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00, 0x05, 0x00, 0x06, 0x00,
        0x07, 0x00, 0x08, 0x00, 0x04, 0x03, 0x02, 0x01, 0x44, 0x33, 0x22, 0x11,
        0x66, 0x55, 0x88, 0x77, 0x0d, 0x0c, 0x0b, 0x0a, 0x14, 0x13, 0x12, 0x11,
        0x24, 0x23, 0x22, 0x21, 0x34, 0x33, 0x32, 0x31, 0x44, 0x43, 0x42, 0x41,
        0x54, 0x53, 0x52, 0x51, 0x64, 0x63, 0x62, 0x61, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x10, 0x0f, 0x0e, 0x0d, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
        0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x00, 0x80, 0xff, 0xff, 0xff, 0x7f,
    };
    unsigned char encoded[192];
    unsigned char decoded[192];
    short got_type[WORLD_SIDE_LEN][WORLD_SIDE_LEN];
    short got_group[WORLD_SIDE_LEN][WORLD_SIDE_LEN];
    Map_Point_Info got_info[WORLD_SIDE_LEN][WORLD_SIDE_LEN];
    int got_pollution[WORLD_SIDE_LEN][WORLD_SIDE_LEN];
    unsigned char *end;
    const unsigned char *dend;

    memset (type, 0, sizeof (type));
    memset (group, 0, sizeof (group));
    memset (info, 0, sizeof (info));
    memset (pollution, 0, sizeof (pollution));

    type[0][0] = 1; type[0][1] = 2; type[1][0] = 3; type[1][1] = 4;
    group[0][0] = 5; group[0][1] = 6; group[1][0] = 7; group[1][1] = 8;

    info[0][0].population = 0x01020304;
    info[0][0].flags = 0x11223344;
    info[0][0].coal_reserve = 0x5566;
    info[0][0].ore_reserve = 0x7788;
    info[0][0].int_1 = 0x0a0b0c0d;
    info[0][0].int_2 = 0x11121314;
    info[0][0].int_3 = 0x21222324;
    info[0][0].int_4 = 0x31323334;
    info[0][0].int_5 = 0x41424344;
    info[0][0].int_6 = 0x51525354;
    info[0][0].int_7 = 0x61626364;
    info[1][1].population = 0x0d0e0f10;

    pollution[0][0] = -1;
    pollution[0][1] = 0x01020304;
    pollution[1][0] = -2147483647 - 1;
    pollution[1][1] = 0x7fffffff;

    CHECK (map_wire_bytes (2) == sizeof (expected));
    CHECK (map_wire_bytes (0) == 0);
    CHECK (map_wire_bytes (WORLD_SIDE_LEN + 1) == 0);

    end = map_wire_encode (2, type, group, info, pollution, encoded);
    CHECK (end != NULL);
    CHECK (end == encoded + sizeof (expected));
    CHECK (memcmp (encoded, expected, sizeof (expected)) == 0);

    memset (got_type, 0, sizeof (got_type));
    memset (got_group, 0, sizeof (got_group));
    memset (got_info, 0, sizeof (got_info));
    memset (got_pollution, 0, sizeof (got_pollution));
    dend = map_wire_decode (2, encoded, got_type, got_group, got_info,
                            got_pollution);
    CHECK (dend != NULL);
    CHECK (dend == encoded + sizeof (expected));
    CHECK (memcmp (got_type, type, sizeof (type)) == 0);
    CHECK (memcmp (got_group, group, sizeof (group)) == 0);
    CHECK (memcmp (got_info, info, sizeof (info)) == 0);
    CHECK (memcmp (got_pollution, pollution, sizeof (pollution)) == 0);

    /* Overwrite the decoded copy and decode again to prove it is not
       accidentally reading the source arrays. */
    memset (decoded, 0xa5, sizeof (decoded));
    memcpy (decoded, encoded, sizeof (decoded));
    decoded[16] ^= 0xff;           /* first info byte: population LSB */
    memset (got_info, 0, sizeof (got_info));
    dend = map_wire_decode (2, decoded, got_type, got_group, got_info,
                            got_pollution);
    CHECK (dend == decoded + sizeof (decoded));
    CHECK (got_info[0][0].population == 0x010203fb);

    if (failures != 0) {
        fprintf (stderr, "%d map-wire test(s) failed\n", failures);
        return 1;
    }
    printf ("map wire: golden bytes and round-trip passed\n");
    return 0;
}
