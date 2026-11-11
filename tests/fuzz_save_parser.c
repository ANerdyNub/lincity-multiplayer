/* libFuzzer target for the save parser and map_state_ok().
 *
 * A full 100x100 text map contains 420,000 decimal fields; parsing that on
 * every execution caps libFuzzer at tens of executions per second.  The fast
 * path below parses the real save suffix (origin, coordinate tables, counts,
 * and the scalar/tail section) against a valid map cached once at startup.
 * Inputs of 100 KiB or more still take the full-file path, including
 * save_read_map(), so the uncompressed save corpus exercises the map parser.
 *
 * Build/run with tests/run_fuzz_save.sh.  Set FUZZ_MAP to the uncompressed
 * opening save used for the fast-path map.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "validation.h"
#include "save_parse.h"
#include "module_buttons.h"
#include "pbar.h"
#include "fuzz_type_groups.h"

#define FULL_INPUT_MIN 100000

struct GROUP main_groups[NUM_OF_GROUPS];
struct TYPE main_types[NUM_OF_TYPES];

static Map base_map;
static int base_map_ok;
static Map cell_map;
static int cell_map_ok;
static int base_used_x = -1;
static int base_used_y = -1;
static int base_origin_x = -1;
static int base_origin_y = -1;
static int base_component_x = -1;
static int base_component_y = -1;

int
get_group_of_type (short selected_type)
{
    if (!type_ok (selected_type))
        return -1;
    return main_types[selected_type].group;
}

static void
init_tables (void)
{
    static int done;
    int i;

    if (done)
        return;
    done = 1;
    for (i = 0; i < NUM_OF_GROUPS; i++) {
        main_groups[i].size = fuzz_group_size[i];
        main_groups[i].group = (unsigned short) i;
    }
    for (i = 0; i < NUM_OF_TYPES; i++)
        main_types[i].group = fuzz_type_group[i];
}

static void
fill_map_groups (Map *map)
{
    int x, y;

    for (x = 0; x < WORLD_SIDE_LEN; x++)
        for (y = 0; y < WORLD_SIDE_LEN; y++)
            map->group[x][y] = (short) get_group_of_type (map->type[x][y]);
}

static int
read_saved_coords (FILE *ofile, int *x, int *y, int limit)
{
    int i;

    for (i = 0; i < limit; i++)
        if (!save_read_int (ofile, &x[i]) || !save_read_int (ofile, &y[i]))
            return 0;
    return 1;
}

static int
read_save_counts (FILE *ofile, int ver,
                  int *originx, int *originy, int *total_time,
                  int *substationx, int *substationy,
                  int *substation_count,
                  int *marketx, int *markety, int *market_count)
{
    int limit;

    if (!save_read_int (ofile, originx) || !save_read_int (ofile, originy))
        return 0;
    if (!map_coord_ok (*originx, *originy))
        return 0;
    if (!save_read_int (ofile, total_time))
        return 0;

    limit = ver <= MM_MS_C_VER ? OLD_MAX_NUMOF_SUBSTATIONS
                               : MAX_NUMOF_SUBSTATIONS;
    if (!read_saved_coords (ofile, substationx, substationy, limit))
        return 0;
    if (!save_read_int (ofile, substation_count)
        || *substation_count < 0 || *substation_count > limit)
        return 0;

    limit = ver <= MM_MS_C_VER ? OLD_MAX_NUMOF_MARKETS : MAX_NUMOF_MARKETS;
    if (!read_saved_coords (ofile, marketx, markety, limit))
        return 0;
    if (!save_read_int (ofile, market_count)
        || *market_count < 0 || *market_count > limit)
        return 0;

    return 1;
}

/* Parse from people_pool through the ten compatibility strings.  This is the
   section P13's scalar checks cover; keep it in sync with load_city(). */
static int
read_save_tail (FILE *ofile)
{
    int i, dummy, monthgraph_size;
    float f;
    char s[256];

    for (i = 0; i < 25; i++)
        if (!save_read_int (ofile, &dummy))
            return 0;
    if (!save_read_int (ofile, &monthgraph_size)
        || monthgraph_size < 0 || monthgraph_size > 100000)
        return 0;
    for (i = 0; i < monthgraph_size * 4; i++)
        if (!save_read_int (ofile, &dummy))
            return 0;
    for (i = 0; i < 3; i++)
        if (!save_read_int (ofile, &dummy))
            return 0;
    for (i = 0; i < PBAR_DATA_SIZE * NUM_PBARS; i++)
        if (!save_read_int (ofile, &dummy))
            return 0;
    for (i = 0; i < 2 * NUM_PBARS; i++)
        if (!save_read_int (ofile, &dummy))
            return 0;

    if (!save_read_int (ofile, &dummy))   /* cheat_flag */
        return 0;
    if (!save_read_int (ofile, &dummy))   /* pollution deaths */
        return 0;
    if (fscanf (ofile, "%f", &f) != 1)   /* pollution history */
        return 0;
    if (!save_read_int (ofile, &dummy))   /* starvation deaths */
        return 0;
    if (fscanf (ofile, "%f", &f) != 1)   /* starvation history */
        return 0;
    if (!save_read_int (ofile, &dummy))   /* unemployment years */
        return 0;
    if (fscanf (ofile, "%f", &f) != 1)   /* unemployment history */
        return 0;
    for (i = 0; i < 3; i++)
        if (!save_read_int (ofile, &dummy))
            return 0;
    for (i = 0; i < NUMOF_MODULES; i++)
        if (!save_read_int (ofile, &dummy))
            return 0;
    if (!save_read_int (ofile, &dummy) || !save_read_int (ofile, &dummy))
        return 0;

    if (fscanf (ofile, "%128s", s) != 1)
        return 0;
    if (fscanf (ofile, "%128s", s) != 1)
        return 0;
    if (fgets (s, sizeof s, ofile) == NULL)
        return 0;
    if (fgets (s, sizeof s, ofile) == NULL)
        return 0;
    return 1;
}

struct cell_snapshot
{
    int x, y;
    short type, group;
    Map_Point_Info info;
};

static void
restore_cell (const struct cell_snapshot *s)
{
    cell_map.type[s->x][s->y] = s->type;
    cell_map.group[s->x][s->y] = s->group;
    cell_map.info[s->x][s->y] = s->info;
}

/* Mutate a handful of cells starting from the cached valid map, then run the
   full validator.  The first mutation targets a real CST_USED cell so the
   origin/footprint checks are reached even when the rest of the input is
   noise.  Each touched cell is restored afterwards, so the cached map stays
   immutable across executions. */
static int
run_mapcells_input (const uint8_t *data, size_t size)
{
    int n, i;
    size_t p = 0;

    if (!cell_map_ok || size < 4)
        return 0;

    n = 1 + data[p++] % 8;
    for (i = 0; i < n; i++) {
        struct cell_snapshot snap;
        int x, y, action, val;
        short t, g;

        /* The first three mutations walk the common CST_USED paths: a valid
           origin/footprint, a changed footprint component, and a bad
           component origin.  Remaining mutations are arbitrary. */
        if (i == 0 && base_used_x >= 0) {
            x = base_used_x;
            y = base_used_y;
            action = 3;
        } else if (i == 1 && base_component_x >= 0) {
            x = base_component_x;
            y = base_component_y;
            action = 4;
        } else if (i == 2 && base_component_x >= 0) {
            x = base_component_x;
            y = base_component_y;
            action = 5;
        } else {
            x = data[p++ % size] % WORLD_SIDE_LEN;
            y = data[p++ % size] % WORLD_SIDE_LEN;
            action = data[p++ % size] % 5;
        }
        snap.x = x;
        snap.y = y;
        snap.type = cell_map.type[x][y];
        snap.group = cell_map.group[x][y];
        snap.info = cell_map.info[x][y];

        switch (action) {
        case 0:
            t = (short) (data[p++ % size] % (NUM_OF_TYPES + 2)) - 1;
            g = (short) (data[p++ % size] % (NUM_OF_GROUPS + 2)) - 1;
            cell_map.type[x][y] = t;
            cell_map.group[x][y] = g;
            break;
        case 1:
            t = (short) (data[p++ % size] % NUM_OF_TYPES);
            cell_map.type[x][y] = t;
            cell_map.group[x][y] = (short) get_group_of_type (t);
            break;
        case 2:
            val = (int) data[p++ % size] - 128;
            cell_map.info[x][y].int_1 = val;
            cell_map.info[x][y].int_2 = val;
            break;
        case 3:
            cell_map.type[x][y] = CST_USED;
            cell_map.group[x][y] = GROUP_USED;
            cell_map.info[x][y].int_1 = base_origin_x;
            cell_map.info[x][y].int_2 = base_origin_y;
            break;
        case 4:
            cell_map.type[x][y] = CST_GREEN;
            cell_map.group[x][y] = GROUP_BARE;
            break;
        case 5:
            cell_map.info[x][y].int_1 = base_origin_x + 1;
            cell_map.info[x][y].int_2 = base_origin_y;
            break;
        default:
            cell_map.info[x][y].flags = (int) data[p++ % size];
            cell_map.info[x][y].int_1 = (int) data[p++ % size] - 128;
            cell_map.info[x][y].int_2 = (int) data[p++ % size] - 128;
            break;
        }

        /* Validate each mutation independently; this keeps a later random
           mutation from masking the CST_USED path reached above. */
        (void) map_state_ok (cell_map.type, cell_map.group, cell_map.info,
                             -1, NULL, NULL, -1, NULL, NULL);
        restore_cell (&snap);
    }
    return 0;
}

static int
run_save_input (const uint8_t *data, size_t size, int full)
{
    int ver = 98;
    int originx, originy, total_time;
    int substationx[MAX_NUMOF_SUBSTATIONS];
    int substationy[MAX_NUMOF_SUBSTATIONS];
    int marketx[MAX_NUMOF_MARKETS];
    int markety[MAX_NUMOF_MARKETS];
    int substation_count, market_count;
    Map *map = NULL;
    FILE *ofile = NULL;
    int rc = 0;

    if (size == 0 || size > 3 * 1024 * 1024)
        return 0;
    if (!full && !base_map_ok)
        return 0;

    if (full) {
        map = (Map *) calloc (1, sizeof *map);
        if (!map)
            goto out;
    } else {
        map = &base_map;
    }

    ofile = fmemopen ((void *) data, size, "r");
    if (!ofile)
        goto out;

    if (full) {
        if (!save_read_int (ofile, &ver) || !save_version_ok (ver))
            goto out;
        if (!save_read_map (ofile, map))
            goto out;
        fill_map_groups (map);
    }

    if (!read_save_counts (ofile, ver, &originx, &originy, &total_time,
                           substationx, substationy, &substation_count,
                           marketx, markety, &market_count))
        goto out;
    if (!map_state_ok (map->type, map->group, map->info,
                       market_count, marketx, markety,
                       substation_count, substationx, substationy))
        goto out;
    if (!read_save_tail (ofile))
        goto out;

    rc = 1;
out:
    if (ofile)
        fclose (ofile);
    if (full)
        free (map);
    return rc;
}

static int
load_base_map (void)
{
    const char *path = getenv ("FUZZ_MAP");
    FILE *ofile;
    int ver;

    if (!path || !*path) {
        fprintf (stderr, "FUZZ_MAP is not set; run through tests/run_fuzz_save.sh\n");
        return 0;
    }
    ofile = fopen (path, "r");
    if (!ofile)
        return 0;
    if (!save_read_int (ofile, &ver) || !save_version_ok (ver)
        || !save_read_map (ofile, &base_map)) {
        fclose (ofile);
        return 0;
    }
    fill_map_groups (&base_map);
    fclose (ofile);
    return 1;
}

static void
find_base_used_cell (void)
{
    int x, y;

    for (x = 0; x < WORLD_SIDE_LEN && base_used_x < 0; x++)
        for (y = 0; y < WORLD_SIDE_LEN; y++) {
            int ox, oy, g, size;

            if (base_map.type[x][y] != CST_USED)
                continue;
            ox = base_map.info[x][y].int_1;
            oy = base_map.info[x][y].int_2;
            if (!map_coord_ok (ox, oy))
                continue;
            g = base_map.group[ox][oy];
            if (!group_ok (g))
                continue;
            size = main_groups[g].size;
            if (size < 2 || size > 4 || !map_rect_ok (ox, oy, size, size))
                continue;
            base_used_x = x;
            base_used_y = y;
            base_origin_x = ox;
            base_origin_y = oy;
            base_component_x = ox + size - 1;
            base_component_y = oy + size - 1;
            if (base_map.type[base_component_x][base_component_y] != CST_USED)
                base_component_x = x, base_component_y = y;
            break;
        }
}

int
LLVMFuzzerInitialize (int *argc, char ***argv)
{
    (void) argc;
    (void) argv;
    init_tables ();
    base_map_ok = load_base_map ();
    if (base_map_ok) {
        memcpy (&cell_map, &base_map, sizeof cell_map);
        find_base_used_cell ();
        cell_map_ok = 1;
    }
    return 0;
}

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
    const char *mode = getenv ("FUZZ_MODE");
    int full = size >= FULL_INPUT_MIN;

    if (mode && strcmp (mode, "mapcells") == 0)
        return run_mapcells_input (data, size);
    (void) run_save_input (data, size, full);
    return 0;
}
