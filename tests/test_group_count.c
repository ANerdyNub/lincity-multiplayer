#include <stdio.h>
#include <string.h>

#include "engglobs.h"
#include "validation.h"
#include "simulate.h"

Map map;
struct GROUP main_groups[NUM_OF_GROUPS];
struct TYPE main_types[NUM_OF_TYPES];
int num_groups = NUM_OF_GROUPS;
int num_types = NUM_OF_TYPES;

int
get_group_of_type (short selected_type)
{
    if (!type_ok (selected_type))
        return -1;
    return main_types[selected_type].group;
}

int
main (void)
{
    int counts[NUM_OF_GROUPS];
    int i;

    memset (&map, 0, sizeof map);
    for (i = 0; i < NUM_OF_TYPES; i++)
        main_types[i].group = i < NUM_OF_GROUPS ? i : 0;
    map.type[0][0] = 1;
    map.group[0][0] = 1;
    map.type[0][1] = -1;                  /* invalid type */
    map.type[0][2] = NUM_OF_TYPES;        /* invalid type */
    map.type[0][3] = 2;
    map.group[0][3] = NUM_OF_GROUPS;      /* invalid group */
    map.type[0][4] = 3;
    map.group[0][4] = 4;                  /* mismatched group/type */

    count_all_groups (counts, NUM_OF_GROUPS);
    if (counts[1] != 1) {
        fprintf (stderr, "FAIL: valid group count %d != 1\n", counts[1]);
        return 1;
    }
    for (i = 0; i < NUM_OF_GROUPS; i++)
        if (i != 1 && counts[i] != 0) {
            fprintf (stderr, "FAIL: invalid cell counted in group %d\n", i);
            return 1;
        }
    puts ("group count: invalid types/groups skipped");
    return 0;
}
