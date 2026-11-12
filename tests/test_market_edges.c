#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engglobs.h"
#include "lctypes.h"
#include "lintypes.h"
#include "lin-city.h"

Map map;
struct GROUP main_groups[NUM_OF_GROUPS];
struct TYPE main_types[NUM_OF_TYPES];
int num_groups = NUM_OF_GROUPS;
int num_types = NUM_OF_TYPES;
int marketx[MAX_NUMOF_MARKETS], markety[MAX_NUMOF_MARKETS];
int numof_markets;
int income_tax;
int total_time;

/* do_market() updates the pbar inventory; this test only exercises the
   neighbor scan, so record the call without pulling in stats.c. */
void
inventory (int x, int y)
{
    (void) x;
    (void) y;
}

/* get_stuff()/put_stuff() are retained by the linker but are not reached
   while the single market can satisfy this tile. */
void
do_error (char *s)
{
    fprintf (stderr, "unexpected do_error: %s\n", s);
    exit (1);
}

static int
run_corner (int x, int y)
{
    int i;

    memset (&map, 0, sizeof map);
    for (i = 0; i < MAX_NUMOF_MARKETS; i++) {
        marketx[i] = -1;
        markety[i] = -1;
    }
    numof_markets = 1;
    marketx[0] = x;
    markety[0] = y;
    map.info[x][y].int_2 = MAX_JOBS_IN_MARKET;
    map.type[x][y] = CST_MARKET_EMPTY;
    map.group[x][y] = GROUP_MARKET;
    total_time = 0;
    do_market (x, y);
    return 0;
}

int
main (void)
{
    static const int points[][2] = {
        {0, 0}, {0, 99}, {99, 0}, {99, 99},
        {0, 50}, {99, 50}, {50, 0}, {50, 99},
    };
    int i;

    for (i = 0; i < (int) (sizeof points / sizeof points[0]); i++)
        run_corner (points[i][0], points[i][1]);
    puts ("market: corner/edge neighbor probes stay in bounds");
    return 0;
}
