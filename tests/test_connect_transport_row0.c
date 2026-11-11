/*
 * M14 regression: connect_transport() must scan row 0 and column 0.
 * The old clipping changed an origin of 0 to 1 and reduced the span, so a
 * one-cell edge call became a zero-cell call and left the tile untouched.
 */
#include <stdio.h>
#include <string.h>

#include "engglobs.h"
#include "lctypes.h"
#include "lin-city.h"

Map map;

/* Only the water branch of connect_transport() uses this; the test exercises
   track cells, so a harmless stub keeps the translation unit linkable. */
int
get_group_of_type (short type)
{
    (void)type;
    return -1;
}

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void
setup_track (int x, int y)
{
    MP_TYPE (x, y) = CST_GREEN;
    MP_GROUP (x, y) = GROUP_TRACK;
    MP_INFO (x, y).flags = 0;
}

int
main (void)
{
    memset (&map, 0, sizeof map);

    /* Two horizontal track cells on row 0. */
    setup_track (0, 0);
    setup_track (1, 0);
    connect_transport (0, 0, 2, 1);
    CHECK (MP_TYPE (0, 0) == CST_TRACK_LR);
    CHECK (MP_INFO (0, 0).flags & FLAG_RIGHT);
    CHECK (MP_TYPE (1, 0) == CST_TRACK_LR);
    CHECK (MP_INFO (1, 0).flags & FLAG_LEFT);

    /* Two vertical track cells on column 0. */
    memset (&map, 0, sizeof map);
    setup_track (0, 0);
    setup_track (0, 1);
    connect_transport (0, 0, 1, 2);
    CHECK (MP_TYPE (0, 0) == CST_TRACK_UD);
    CHECK (MP_INFO (0, 0).flags & FLAG_DOWN);
    CHECK (MP_TYPE (0, 1) == CST_TRACK_UD);
    CHECK (MP_INFO (0, 1).flags & FLAG_UP);

    if (failures != 0) {
        fprintf (stderr, "%d row/column-0 transport test(s) failed\n",
                 failures);
        return 1;
    }
    printf ("connect_transport: row 0 and column 0 are updated\n");
    return 0;
}
