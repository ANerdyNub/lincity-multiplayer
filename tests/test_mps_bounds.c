/*
 * L1 regression: MPS writers must reject out-of-range slots and must clamp
 * the untruncated snprintf() length before taking an address past the buffer.
 */
#include <stdio.h>
#include <string.h>

#include "mps.h"

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
    char long_s[MPS_INFO_CHARS + 32];

    memset (mps_info, 0, sizeof mps_info);
    memset (long_s, 'x', sizeof long_s - 1);
    long_s[sizeof long_s - 1] = '\0';

    /* These used to index mps_info[-1] or mps_info[MAPPOINT_STATS_LINES]. */
    mps_store_title (-1, "bad");
    mps_store_title (MAPPOINT_STATS_LINES, "bad");
    mps_store_sss (MAPPOINT_STATS_LINES, "a", "b", "c");
    mps_store_sd (-1, "bad", 1);

    /* Truncating writes must stay inside the fixed row. */
    mps_store_ss (0, long_s, "tail");
    mps_store_sd (1, long_s, 42);
    mps_store_sfp (2, long_s, 1.0);
    mps_store_sss (3, long_s, long_s, long_s);

    CHECK (mps_info[0][MPS_INFO_CHARS - 1] == '\0');
    CHECK (mps_info[1][MPS_INFO_CHARS - 1] == '\0');
    CHECK (mps_info[2][MPS_INFO_CHARS - 1] == '\0');
    CHECK (mps_info[3][MPS_INFO_CHARS - 1] == '\0');

    if (failures != 0) {
        fprintf (stderr, "%d MPS bounds test(s) failed\n", failures);
        return 1;
    }
    printf ("mps: out-of-range and truncated writes stay in bounds\n");
    return 0;
}
