#include <stdio.h>
#include <string.h>

#include "lctypes.h"
#include "validation.h"

struct GROUP main_groups[NUM_OF_GROUPS];
struct TYPE main_types[NUM_OF_TYPES];

int
get_group_of_type (short selected_type)
{
    if (!type_ok (selected_type))
        return -1;
    return main_types[selected_type].group;
}

static short test_type[WORLD_SIDE_LEN][WORLD_SIDE_LEN];
static short test_group[WORLD_SIDE_LEN][WORLD_SIDE_LEN];
static Map_Point_Info test_info[WORLD_SIDE_LEN][WORLD_SIDE_LEN];
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
    /* map_coord_ok: edges and the first out-of-range values. */
    CHECK (!map_coord_ok (-1, 0));
    CHECK (!map_coord_ok (0, -1));
    CHECK (!map_coord_ok (WORLD_SIDE_LEN, 0));
    CHECK (!map_coord_ok (0, WORLD_SIDE_LEN));
    CHECK (map_coord_ok (0, 0));
    CHECK (map_coord_ok (WORLD_SIDE_LEN - 1, WORLD_SIDE_LEN - 1));

    /* map_rect_ok: exact edge, one past it, and non-positive sizes. */
    CHECK (map_rect_ok (0, 0, WORLD_SIDE_LEN, WORLD_SIDE_LEN));
    CHECK (map_rect_ok (WORLD_SIDE_LEN - 1, WORLD_SIDE_LEN - 1, 1, 1));
    CHECK (!map_rect_ok (1, 0, WORLD_SIDE_LEN, 1));
    CHECK (!map_rect_ok (0, 1, 1, WORLD_SIDE_LEN));
    CHECK (!map_rect_ok (-1, 0, 1, 1));
    CHECK (!map_rect_ok (0, -1, 1, 1));
    CHECK (!map_rect_ok (0, 0, 0, 1));
    CHECK (!map_rect_ok (0, 0, 1, 0));
    CHECK (!map_rect_ok (0, 0, -1, 1));
    CHECK (!map_rect_ok (0, 0, 1, -1));
    CHECK (!map_rect_ok (0, 0, WORLD_SIDE_LEN + 1, 1));

    /* The save loader and the save-list display share one version range. */
    CHECK (save_version_ok (MIN_LOAD_VERSION));
    CHECK (save_version_ok (VERSION_INT));
    CHECK (!save_version_ok (MIN_LOAD_VERSION - 1));
    CHECK (!save_version_ok (VERSION_INT + 1));

    /* type_ok and group_ok: valid first/last, invalid both sides. */
    CHECK (!type_ok (-1));
    CHECK (!type_ok (NUM_OF_TYPES));
    CHECK (type_ok (0));
    CHECK (type_ok (NUM_OF_TYPES - 1));

    CHECK (!group_ok (-1));
    CHECK (!group_ok (NUM_OF_GROUPS));
    CHECK (group_ok (0));
    CHECK (group_ok (NUM_OF_GROUPS - 1));

    /* Only market and port may carry trade flags. */
    CHECK (trade_group_ok (GROUP_MARKET));
    CHECK (trade_group_ok (GROUP_PORT));
    CHECK (!trade_group_ok (GROUP_BARE));
    CHECK (!trade_group_ok (GROUP_TRACK));

    /* Full market payload: all twelve trade bits, 0x0fff00. */
    CHECK ((1048320 & FLAG_TRADE_MASK) == 1048320);
    CHECK (merge_trade_flags (0, 1048320) == 1048320);

    /* 0x707f00 is seven trade bits plus all three coverage bits.  A tile
       that already has non-trade state must keep every one of those bits
       when the trade bits are merged. */
    {
        int existing = FLAG_FIRE_COVER | FLAG_HEALTH_COVER
            | FLAG_CRICKET_COVER | FLAG_IS_RIVER | FLAG_HAD_POWER
            | FLAG_MULTI_TRANSPORT;
        int merged = merge_trade_flags (existing, 7372544);

        CHECK ((7372544 & FLAG_TRADE_MASK) == 32512);
        CHECK ((merged & ~FLAG_TRADE_MASK) == existing);
        CHECK ((merged & FLAG_TRADE_MASK) == 32512);
        CHECK (merged == (existing | 32512));
    }

    /* Full map-state validator: a valid 2x2 market footprint, its saved
       market list, and a 2x2 substation footprint/list. */
    {
        int i, x, y;
        int mx[1] = { 5 };
        int my[1] = { 5 };
        int badx[1] = { WORLD_SIDE_LEN };
        int bady[1] = { 5 };
        int sx[1] = { 10 };
        int sy[1] = { 10 };

        for (i = 0; i < NUM_OF_GROUPS; i++)
            main_groups[i].size = 1;
        main_groups[GROUP_MARKET].size = 2;
        main_groups[GROUP_SUBSTATION].size = 2;
        main_types[CST_GREEN].group = GROUP_BARE;
        main_types[CST_USED].group = GROUP_USED;
        main_types[CST_MARKET_EMPTY].group = GROUP_MARKET;
        main_types[CST_SUBSTATION_R].group = GROUP_SUBSTATION;

        for (x = 0; x < WORLD_SIDE_LEN; x++) {
            for (y = 0; y < WORLD_SIDE_LEN; y++) {
                test_type[x][y] = CST_GREEN;
                test_group[x][y] = GROUP_BARE;
                memset (&test_info[x][y], 0, sizeof (test_info[x][y]));
            }
        }

        test_type[5][5] = CST_MARKET_EMPTY;
        test_group[5][5] = GROUP_MARKET;
        for (x = 5; x < 7; x++) {
            for (y = 5; y < 7; y++) {
                if (x == 5 && y == 5)
                    continue;
                test_type[x][y] = CST_USED;
                test_group[x][y] = GROUP_USED;
                test_info[x][y].int_1 = 5;
                test_info[x][y].int_2 = 5;
            }
        }

        test_type[10][10] = CST_SUBSTATION_R;
        test_group[10][10] = GROUP_SUBSTATION;
        for (x = 10; x < 12; x++) {
            for (y = 10; y < 12; y++) {
                if (x == 10 && y == 10)
                    continue;
                test_type[x][y] = CST_USED;
                test_group[x][y] = GROUP_USED;
                test_info[x][y].int_1 = 10;
                test_info[x][y].int_2 = 10;
            }
        }

        CHECK (map_arrays_ok (test_type, test_group, test_info));
        CHECK (map_state_ok (test_type, test_group, test_info,
                             1, mx, my, 1, sx, sy));
        CHECK (!map_state_ok (test_type, test_group, test_info,
                              MAX_NUMOF_MARKETS + 1, mx, my, -1, NULL, NULL));
        CHECK (!map_state_ok (test_type, test_group, test_info,
                              1, badx, bady, -1, NULL, NULL));
        CHECK (!map_state_ok (test_type, test_group, test_info,
                              1, (int[]){ 0 }, (int[]){ 0 }, -1, NULL, NULL));
        CHECK (!map_state_ok (test_type, test_group, test_info,
                              -1, NULL, NULL, MAX_NUMOF_SUBSTATIONS + 1,
                              sx, sy));

        /* A used cell must point at the actual origin, not a neighbouring
           used cell or an out-of-bounds coordinate. */
        test_info[6][5].int_1 = 6;
        CHECK (!map_arrays_ok (test_type, test_group, test_info));
        test_info[6][5].int_1 = 5;
        test_info[6][5].int_2 = WORLD_SIDE_LEN;
        CHECK (!map_arrays_ok (test_type, test_group, test_info));
        test_info[6][5].int_2 = 5;

        /* A missing used cell in the footprint is rejected. */
        test_type[6][6] = CST_GREEN;
        test_group[6][6] = GROUP_BARE;
        CHECK (!map_arrays_ok (test_type, test_group, test_info));
    }

    /* Targeted failure paths in map_used_cell_ok() and map_state_ok(),
       including the saved market/substation list validation. */
    {
        int i, x, y;
        int mx[1] = { 5 };
        int my[1] = { 5 };
        int sx[1] = { 10 };
        int sy[1] = { 10 };
        int used_x[1] = { 6 };
        int used_y[1] = { 5 };
        int green_x[1] = { 0 };
        int green_y[1] = { 0 };

        for (i = 0; i < NUM_OF_GROUPS; i++)
            main_groups[i].size = 1;
        main_groups[GROUP_MARKET].size = 2;
        main_groups[GROUP_SUBSTATION].size = 2;
        main_types[CST_GREEN].group = GROUP_BARE;
        main_types[CST_USED].group = GROUP_USED;
        main_types[CST_MARKET_EMPTY].group = GROUP_MARKET;
        main_types[CST_SUBSTATION_R].group = GROUP_SUBSTATION;

        for (x = 0; x < WORLD_SIDE_LEN; x++) {
            for (y = 0; y < WORLD_SIDE_LEN; y++) {
                test_type[x][y] = CST_GREEN;
                test_group[x][y] = GROUP_BARE;
                memset (&test_info[x][y], 0, sizeof (test_info[x][y]));
            }
        }

        test_type[5][5] = CST_MARKET_EMPTY;
        test_group[5][5] = GROUP_MARKET;
        for (x = 5; x < 7; x++) {
            for (y = 5; y < 7; y++) {
                if (x == 5 && y == 5)
                    continue;
                test_type[x][y] = CST_USED;
                test_group[x][y] = GROUP_USED;
                test_info[x][y].int_1 = 5;
                test_info[x][y].int_2 = 5;
            }
        }
        test_type[10][10] = CST_SUBSTATION_R;
        test_group[10][10] = GROUP_SUBSTATION;
        for (x = 10; x < 12; x++) {
            for (y = 10; y < 12; y++) {
                if (x == 10 && y == 10)
                    continue;
                test_type[x][y] = CST_USED;
                test_group[x][y] = GROUP_USED;
                test_info[x][y].int_1 = 10;
                test_info[x][y].int_2 = 10;
            }
        }
        CHECK (map_arrays_ok (test_type, test_group, test_info));

        /* map_used_cell_ok: origin group/type/footprint failures. */
        test_group[5][5] = GROUP_BARE;
        CHECK (!map_used_cell_ok (test_type, test_group, test_info, 6, 5));
        test_group[5][5] = GROUP_MARKET;
        test_type[5][6] = CST_GREEN;
        test_group[5][6] = GROUP_BARE;
        CHECK (!map_used_cell_ok (test_type, test_group, test_info, 6, 5));
        test_type[5][6] = CST_USED;
        test_group[5][6] = GROUP_USED;
        test_info[5][6].int_1 = 4;
        CHECK (!map_used_cell_ok (test_type, test_group, test_info, 6, 5));
        test_info[5][6].int_1 = 5;

        main_groups[GROUP_MARKET].size = 1;
        CHECK (!map_used_cell_ok (test_type, test_group, test_info, 6, 5));
        main_groups[GROUP_MARKET].size = 5;
        CHECK (!map_used_cell_ok (test_type, test_group, test_info, 6, 5));
        main_groups[GROUP_MARKET].size = 2;

        test_info[6][5].int_1 = 6;
        CHECK (!map_used_cell_ok (test_type, test_group, test_info, 6, 5));
        test_info[6][5].int_1 = WORLD_SIDE_LEN;
        CHECK (!map_used_cell_ok (test_type, test_group, test_info, 6, 5));
        test_info[6][5].int_1 = 5;

        test_info[0][0].int_1 = 5;
        test_info[0][0].int_2 = 5;
        CHECK (!map_used_cell_ok (test_type, test_group, test_info, 0, 0));
        test_info[0][0].int_1 = 0;
        test_info[0][0].int_2 = 0;

        /* A standalone used cell reaches map_state_ok's map_used_cell_ok
           return (validation.h:131), unlike a used cell encountered while
           checking its multi-tile origin first. */
        test_type[0][0] = CST_USED;
        test_group[0][0] = GROUP_USED;
        CHECK (!map_arrays_ok (test_type, test_group, test_info));
        test_type[0][0] = CST_GREEN;
        test_group[0][0] = GROUP_BARE;

        /* map_state_ok: type/group, size, rectangle and footprint errors. */
        test_type[0][0] = NUM_OF_TYPES;
        CHECK (!map_arrays_ok (test_type, test_group, test_info));
        test_type[0][0] = CST_GREEN;
        test_group[0][0] = GROUP_MARKET;
        CHECK (!map_arrays_ok (test_type, test_group, test_info));
        test_group[0][0] = GROUP_BARE;
        main_groups[GROUP_BARE].size = 5;
        CHECK (!map_arrays_ok (test_type, test_group, test_info));
        main_groups[GROUP_BARE].size = 1;
        /* Use the market group (size 2) at the far edge; making every green
           cell size 2 would fail on the first footprint before x=99. */
        test_type[99][99] = CST_MARKET_EMPTY;
        test_group[99][99] = GROUP_MARKET;
        CHECK (!map_arrays_ok (test_type, test_group, test_info));
        test_type[99][99] = CST_GREEN;
        test_group[99][99] = GROUP_BARE;

        test_type[5][6] = CST_GREEN;
        test_group[5][6] = GROUP_BARE;
        CHECK (!map_arrays_ok (test_type, test_group, test_info));
        test_type[5][6] = CST_USED;
        test_group[5][6] = GROUP_USED;
        test_info[5][6].int_1 = 4;
        CHECK (!map_arrays_ok (test_type, test_group, test_info));
        test_info[5][6].int_1 = 5;

        /* Saved market and substation lists: null, used cell, wrong group. */
        CHECK (!map_state_ok (test_type, test_group, test_info,
                              1, NULL, NULL, -1, NULL, NULL));
        CHECK (!map_state_ok (test_type, test_group, test_info,
                              1, used_x, used_y, -1, NULL, NULL));
        CHECK (!map_state_ok (test_type, test_group, test_info,
                              1, green_x, green_y, -1, NULL, NULL));
        CHECK (!map_state_ok (test_type, test_group, test_info,
                              -1, NULL, NULL, 1, NULL, NULL));
        CHECK (!map_state_ok (test_type, test_group, test_info,
                              -1, NULL, NULL, 1, used_x, used_y));
        CHECK (!map_state_ok (test_type, test_group, test_info,
                              -1, NULL, NULL, 1, green_x, green_y));
        CHECK (map_state_ok (test_type, test_group, test_info,
                             1, mx, my, 1, sx, sy));
        CHECK (map_state_ok (test_type, test_group, test_info,
                             0, NULL, NULL, 0, NULL, NULL));
    }

    if (failures != 0) {
        fprintf (stderr, "%d validation test(s) failed\n", failures);
        return 1;
    }
    printf ("validation: all checks passed\n");
    return 0;
}
