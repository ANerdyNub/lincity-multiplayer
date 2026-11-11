#include <stdio.h>

#include "engglobs.h"

int marketx[MAX_NUMOF_MARKETS], markety[MAX_NUMOF_MARKETS];
int substationx[MAX_NUMOF_SUBSTATIONS], substationy[MAX_NUMOF_SUBSTATIONS];
int numof_markets;
int numof_substations;

static int
check_markets (int count)
{
    int i;

    numof_markets = count;
    for (i = 0; i < MAX_NUMOF_MARKETS; i++) {
        marketx[i] = i;
        markety[i] = i + 1;
    }
    shuffle_markets ();
    for (i = 0; i < count; i++)
        if (markety[i] != marketx[i] + 1) {
            fprintf (stderr, "FAIL: market pair %d split\n", i);
            return 1;
        }
    return 0;
}

static int
check_substations (int count)
{
    int i;

    numof_substations = count;
    for (i = 0; i < MAX_NUMOF_SUBSTATIONS; i++) {
        substationx[i] = i;
        substationy[i] = i + 1;
    }
    shuffle_substations ();
    for (i = 0; i < count; i++)
        if (substationy[i] != substationx[i] + 1) {
            fprintf (stderr, "FAIL: substation pair %d split\n", i);
            return 1;
        }
    return 0;
}

int
main (void)
{
    int count;

    for (count = -1; count <= 3; count++) {
        if (check_markets (count) || check_substations (count))
            return 1;
    }
    puts ("shuffle: zero/one/negative counts return without division");
    return 0;
}
