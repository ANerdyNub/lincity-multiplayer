#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engglobs.h"
#include "lctypes.h"
#include "power.h"

Map map;
int numof_substations;
int substationx[MAX_NUMOF_SUBSTATIONS];
int substationy[MAX_NUMOF_SUBSTATIONS];
extern int grid_num;
extern int grid_inc;

static int failures;

/* map_power_grid() allocates its Grid nodes through lclib. */
void *
lcalloc (size_t size)
{
    return calloc (1, size);
}

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
    Grid test_grid;
    Grid sentinel;
    int i;

    memset (&map, 0, sizeof map);
    memset (&test_grid, 0, sizeof test_grid);
    memset (&sentinel, 0, sizeof sentinel);
    memset (grid, 0, sizeof grid);

    /* map_power_grid() numbers real grids from 1 and must leave grid[0]
       unused.  A substation is a grid node but not a power source, so this
       exercises the numbering path without main_types. */
    grid[0] = &sentinel;
    map.group[5][5] = GROUP_SUBSTATION;
    map.type[5][5] = CST_SUBSTATION_R;
    map_power_grid ();
    CHECK (grid_num == 1);
    CHECK (grid[0] == &sentinel);
    CHECK (grid[1] != NULL);
    CHECK (map.info[5][5].int_6 == 1);
    free (grid[1]);
    grid[1] = NULL;

    grid[1] = &test_grid;
    grid_num = 1;
    test_grid.total_power = 100;
    test_grid.avail_power = 100;

    /* The normal path still works. */
    numof_substations = 1;
    substationx[0] = 5;
    substationy[0] = 5;
    map.info[5][5].int_6 = 1;
    CHECK (get_power (5, 5, 10, 0) == 1);
    CHECK (test_grid.total_power == 90);
    CHECK (map.info[5][5].int_5 == 10);

    /* A corrupt int_6 must not index grid[]. */
    map.info[5][5].int_6 = MAX_GRIDS + 7;
    CHECK (get_power (5, 5, 10, 0) == 0);
    map.info[5][5].int_6 = 2;
    CHECK (get_power (5, 5, 10, 0) == 0);
    map.info[5][5].int_6 = 0;
    CHECK (get_power (5, 5, 10, 0) == 0);
    map.info[5][5].int_6 = 1;

    /* A count beyond the fixed coordinate arrays is clamped, not read. */
    numof_substations = MAX_NUMOF_SUBSTATIONS + 1;
    for (i = 0; i < MAX_NUMOF_SUBSTATIONS; i++) {
        substationx[i] = 5;
        substationy[i] = 5;
    }
    CHECK (get_power (5, 5, 1, 0) == 1);
    power_time_step ();

    /* Negative and out-of-range counts/coordinates are ignored. */
    numof_substations = -1;
    CHECK (get_power (5, 5, 1, 0) == 0);
    power_time_step ();

    numof_substations = 1;
    substationx[0] = WORLD_SIDE_LEN;
    substationy[0] = 5;
    CHECK (get_power (5, 5, 1, 0) == 0);
    power_time_step ();

    /* A corrupt grid count must not walk past grid[]. */
    numof_substations = 0;
    grid_num = MAX_GRIDS + 1;
    power_time_step ();

    if (failures != 0) {
        fprintf (stderr, "%d power test(s) failed\n", failures);
        return 1;
    }
    puts ("power: corrupt counts and int_6 are bounded");
    return 0;
}
