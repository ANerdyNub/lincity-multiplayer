/* ---------------------------------------------------------------------- *
 * test_bulldoze_traits.c
 * Bulldozing a mod group must undo the same registry traits that placement
 * used.  Before this test, do_bulldoze_area() compared base group IDs, so a
 * mod market or substation stayed in the fixed list until it filled up.
 * ---------------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>

#include "engglobs.h"
#include "engine.h"
#include "lctypes.h"
#include "stats.h"
#include "mod_registry.h"

#define TEST_MOD_GROUP 42
#define TEST_MOD_TYPE 400

Map map;
struct GROUP main_groups[NUM_OF_GROUPS];
struct TYPE main_types[NUM_OF_TYPES];
int num_groups = TEST_MOD_GROUP + 1;
int num_types = TEST_MOD_TYPE + 1;

int numof_markets;
int marketx[MAX_NUMOF_MARKETS], markety[MAX_NUMOF_MARKETS];
int numof_substations;
int substationx[MAX_NUMOF_SUBSTATIONS], substationy[MAX_NUMOF_SUBSTATIONS];
int numof_shanties, numof_communes;
int people_pool;

static int failures;
static int health_cover_calls;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

/* do_bulldoze_area() clears cells through this hook.  The test only needs
   the trait-driven list/counter cleanup, so the map is reset by hand. */
void
clear_mappoint (short fill, int x, int y)
{
    (void)fill;
    (void)x;
    (void)y;
}

void
do_fire_cover (int x, int y)
{
    (void)x;
    (void)y;
}

void
do_health_cover (int x, int y)
{
    (void)x;
    (void)y;
    health_cover_calls++;
}

void
do_cricket_cover (int x, int y)
{
    (void)x;
    (void)y;
}

static void
put_mod_cell (int group)
{
    map.type[1][1] = TEST_MOD_TYPE;
    map.group[1][1] = (short)group;
}

static void
test_market (int group)
{
    int i;

    main_groups[group].traits = GROUP_TRAIT_MARKET;
    numof_markets = 0;
    for (i = 0; i <= MAX_NUMOF_MARKETS; i++) {
        if (!add_a_market (1, 1)) {
            fprintf (stderr,
                     "FAIL: market group %d could not place after %d removals\n",
                     group, i);
            failures++;
            return;
        }
        put_mod_cell (group);
        do_bulldoze_area (CST_GREEN, 1, 1);
        CHECK (numof_markets == 0);
        if (numof_markets != 0)
            return;
    }
}

static void
test_substation (int group)
{
    int i;

    main_groups[group].traits = GROUP_TRAIT_POWER_ENTRY;
    numof_substations = 0;
    for (i = 0; i <= MAX_NUMOF_SUBSTATIONS; i++) {
        if (!add_a_substation (1, 1)) {
            fprintf (stderr,
                     "FAIL: substation group %d could not place after %d removals\n",
                     group, i);
            failures++;
            return;
        }
        put_mod_cell (group);
        do_bulldoze_area (CST_GREEN, 1, 1);
        CHECK (numof_substations == 0);
        if (numof_substations != 0)
            return;
    }
}

static void
test_counter (int group, unsigned int trait, int *counter, const char *name)
{
    main_groups[group].traits = trait;
    *counter = 1;
    put_mod_cell (group);
    do_bulldoze_area (CST_GREEN, 1, 1);
    if (*counter != 0) {
        fprintf (stderr, "FAIL: bulldozing mod %s group left counter %d\n",
                 name, *counter);
        failures++;
    }
}

int
get_group_of_type (short type)
{
    if (type < 0 || type >= num_types)
        return -1;
    return main_types[type].group;
}

static void
test_market_inventory_uses_trait (void)
{
    memset (&map, 0, sizeof map);
    main_groups[TEST_MOD_GROUP].traits = GROUP_TRAIT_MARKET;
    main_types[TEST_MOD_TYPE].group = TEST_MOD_GROUP;
    map.type[3][3] = TEST_MOD_TYPE;
    map.info[3][3].int_1 = 7;
    map.info[3][3].int_2 = 8;
    map.info[3][3].int_3 = 9;
    map.info[3][3].int_4 = 10;
    map.info[3][3].int_5 = 11;
    map.info[3][3].int_6 = 12;
    map.info[3][3].int_7 = 13;
    food_in_markets = jobs_in_markets = coal_in_markets = 0;
    goods_in_markets = ore_in_markets = steel_in_markets = 0;
    waste_in_markets = 0;
    inventory (3, 3);
    CHECK (food_in_markets == 7);
    CHECK (jobs_in_markets == 8);
    CHECK (waste_in_markets == 13);
}

static void
test_health_cover_uses_trait (void)
{
    memset (&map, 0, sizeof map);
    main_groups[TEST_MOD_GROUP].traits = GROUP_TRAIT_HEALTH;
    map.group[2][2] = TEST_MOD_GROUP;
    map.type[2][2] = TEST_MOD_TYPE;
    health_cover_calls = 0;
    do_fire_health_and_cricket_cover ();
    CHECK (health_cover_calls == 1);
}

int
main (void)
{
    memset (&map, 0, sizeof map);
    memset (&main_groups, 0, sizeof main_groups);
    memset (&main_types, 0, sizeof main_types);

    main_groups[GROUP_MARKET].traits = GROUP_TRAIT_MARKET;
    main_groups[GROUP_SUBSTATION].traits = GROUP_TRAIT_POWER_ENTRY;
    main_groups[GROUP_WINDMILL].traits = GROUP_TRAIT_POWER_ENTRY;
    main_groups[GROUP_SHANTY].traits = GROUP_TRAIT_SHANTY;
    main_groups[GROUP_COMMUNE].traits = GROUP_TRAIT_COMMUNE;
    main_groups[TEST_MOD_GROUP].size = 1;

    test_market (GROUP_MARKET);
    test_market (TEST_MOD_GROUP);
    test_substation (GROUP_SUBSTATION);
    test_substation (TEST_MOD_GROUP);
    test_counter (GROUP_SHANTY, GROUP_TRAIT_SHANTY, &numof_shanties, "shanty");
    test_counter (TEST_MOD_GROUP, GROUP_TRAIT_SHANTY, &numof_shanties,
                  "shanty");
    test_counter (GROUP_COMMUNE, GROUP_TRAIT_COMMUNE, &numof_communes,
                  "commune");
    test_counter (TEST_MOD_GROUP, GROUP_TRAIT_COMMUNE, &numof_communes,
                  "commune");
    test_market_inventory_uses_trait ();
    test_health_cover_uses_trait ();

    if (failures != 0) {
        fprintf (stderr, "%d bulldoze trait test(s) failed\n", failures);
        return 1;
    }
    puts ("bulldoze: base and mod market/substation/shanty/commune traits clean up");
    return 0;
}
