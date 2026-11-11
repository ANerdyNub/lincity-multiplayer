/* ---------------------------------------------------------------------- *
 * validation.h
 * Small header-only bounds helpers shared by the network and save paths.
 * ---------------------------------------------------------------------- */

#ifndef __validation_h__
#define __validation_h__

#include "lin-city.h"
#include "lintypes.h"
#include "lctypes.h"

static inline int
map_coord_ok (int x, int y)
{
    return x >= 0 && x < WORLD_SIDE_LEN && y >= 0 && y < WORLD_SIDE_LEN;
}

static inline int
map_rect_ok (int x, int y, int w, int h)
{
    return w > 0 && h > 0
        && x >= 0 && y >= 0
        && x <= WORLD_SIDE_LEN - w
        && y <= WORLD_SIDE_LEN - h;
}

static inline int
save_version_ok (int version)
{
    return version >= MIN_LOAD_VERSION && version <= VERSION_INT;
}

static inline int
type_ok (int type)
{
    return type >= 0 && type < NUM_OF_TYPES;
}

static inline int
group_ok (int group)
{
    return group >= 0 && group < NUM_OF_GROUPS;
}

static inline int
trade_group_ok (int group)
{
    return group == GROUP_MARKET || group == GROUP_PORT;
}

/* MSG_MFLAGS carries a whole flags word.  Only the twelve trade bits are
   authoritative; coverage and every other bit already on the tile must
   survive the merge. */
static inline int
merge_trade_flags (int existing, int incoming)
{
    return (existing & ~FLAG_TRADE_MASK) | (incoming & FLAG_TRADE_MASK);
}

/* Check the data carried by one CST_USED cell: it must point back to an
   in-bounds, multi-tile building origin whose entire footprint is made of
   CST_USED cells with the same origin. */
static inline int
map_used_cell_ok (short type[][WORLD_SIDE_LEN],
                  short group[][WORLD_SIDE_LEN],
                  Map_Point_Info info[][WORLD_SIDE_LEN],
                  int x, int y)
{
    int ox = info[x][y].int_1;
    int oy = info[x][y].int_2;
    int ot, og, size, i, j;

    if (!map_coord_ok (ox, oy))
        return 0;
    ot = type[ox][oy];
    og = group[ox][oy];
    if (ot == CST_USED || !type_ok (ot) || !group_ok (og))
        return 0;
    if (get_group_of_type ((short) ot) != og)
        return 0;
    size = main_groups[og].size;
    if (size < 2 || size > 4 || !map_rect_ok (ox, oy, size, size))
        return 0;
    if (x < ox || x >= ox + size || y < oy || y >= oy + size)
        return 0;

    for (i = 0; i < size; i++) {
        for (j = 0; j < size; j++) {
            int tx = ox + i;
            int ty = oy + j;

            if (i == 0 && j == 0)
                continue;
            if (type[tx][ty] != CST_USED || group[tx][ty] != GROUP_USED)
                return 0;
            if (info[tx][ty].int_1 != ox || info[tx][ty].int_2 != oy)
                return 0;
        }
    }
    return 1;
}

/* Full shared map validator.  It checks every type/group pair, every
   CST_USED origin reference, and every multi-tile footprint.  The optional
   market/substation lists are checked when their count is >= 0; the network
   path has no lists and passes -1, while load_city() passes the saved
   counts and coordinate arrays. */
static inline int
map_state_ok (short type[][WORLD_SIDE_LEN],
              short group[][WORLD_SIDE_LEN],
              Map_Point_Info info[][WORLD_SIDE_LEN],
              int num_markets, const int marketx[], const int markety[],
              int num_substations, const int substationx[],
              const int substationy[])
{
    int x, y, q;

    for (x = 0; x < WORLD_SIDE_LEN; x++) {
        for (y = 0; y < WORLD_SIDE_LEN; y++) {
            int t = type[x][y];
            int g = group[x][y];

            if (!type_ok (t) || !group_ok (g))
                return 0;
            if (get_group_of_type ((short) t) != g)
                return 0;

            if (t == CST_USED) {
                if (!map_used_cell_ok (type, group, info, x, y))
                    return 0;
            } else {
                int size = main_groups[g].size;

                if (size < 0 || size > 4)
                    return 0;
                if (size > 1) {
                    int i, j;

                    if (!map_rect_ok (x, y, size, size))
                        return 0;
                    for (i = 0; i < size; i++) {
                        for (j = 0; j < size; j++) {
                            int tx = x + i;
                            int ty = y + j;

                            if (i == 0 && j == 0)
                                continue;
                            if (type[tx][ty] != CST_USED
                                || group[tx][ty] != GROUP_USED)
                                return 0;
                            if (info[tx][ty].int_1 != x
                                || info[tx][ty].int_2 != y)
                                return 0;
                        }
                    }
                }
            }
        }
    }

    if (num_markets >= 0) {
        if (num_markets > MAX_NUMOF_MARKETS
            || (num_markets > 0 && (!marketx || !markety)))
            return 0;
        for (q = 0; q < num_markets; q++) {
            int mx = marketx[q];
            int my = markety[q];

            if (!map_coord_ok (mx, my) || type[mx][my] == CST_USED)
                return 0;
            if (get_group_of_type (type[mx][my]) != GROUP_MARKET)
                return 0;
        }
    }
    if (num_substations >= 0) {
        if (num_substations > MAX_NUMOF_SUBSTATIONS
            || (num_substations > 0 && (!substationx || !substationy)))
            return 0;
        for (q = 0; q < num_substations; q++) {
            int sx = substationx[q];
            int sy = substationy[q];
            int sg;

            if (!map_coord_ok (sx, sy) || type[sx][sy] == CST_USED)
                return 0;
            sg = get_group_of_type (type[sx][sy]);
            if (sg != GROUP_SUBSTATION && sg != GROUP_WINDMILL)
                return 0;
        }
    }
    return 1;
}

/* Compatibility wrapper for map-only callers and tests. */
static inline int
map_arrays_ok (short type[][WORLD_SIDE_LEN],
               short group[][WORLD_SIDE_LEN],
               Map_Point_Info info[][WORLD_SIDE_LEN])
{
    return map_state_ok (type, group, info, -1, NULL, NULL,
                         -1, NULL, NULL);
}

#endif /* __validation_h__ */
