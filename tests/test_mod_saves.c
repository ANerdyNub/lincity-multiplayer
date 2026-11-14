/* ---------------------------------------------------------------------- *
 * test_mod_saves.c
 * Focused tests for the versioned save mod header: formatting, parsing,
 * stable sparse type remapping, missing/incompatible mod rejection, and
 * all-or-nothing remap rollback.
 * ---------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lintypes.h"
#include "mod_loader.h"
#include "mod_registry.h"
#include "save_mods.h"

struct GROUP main_groups[MAX_GROUPS];
struct TYPE main_types[MAX_TYPES];

int
get_group_of_type (short type)
{
    if (type < 0 || type >= num_types)
        return -1;
    return main_types[type].group;
}

static int failures;
static int checks;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,         \
                     #cond);                                                 \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static const struct MOD_BEHAVIOR park_behavior = {
    "parkland", 0, NULL, NULL, NULL
};

static const struct MOD_BEHAVIOR *
test_lookup (const char *name)
{
    if (name != NULL && strcmp (name, "parkland") == 0)
        return &park_behavior;
    return NULL;
}

static char *
test_graphic (const char *path, char *err, size_t err_size)
{
    char *tile;

    (void) path;
    (void) err;
    (void) err_size;
    tile = malloc (16 * 16);
    if (tile == NULL)
        return NULL;
    memset (tile, 7, 16 * 16);
    return tile;
}

static void
reset_base_registry (void)
{
    struct GROUP group;

    memset (&group, 0, sizeof group);
    snprintf (group.name, sizeof group.name, "Bare");
    group.size = 1;
    group.colour = 1;

    mod_registry_reset ();
    CHECK (mod_register_group (0, &group));
    CHECK (mod_register_type (0, 0, NULL));
}

static int
parse_text (const char *text, char *err, size_t err_size)
{
    FILE *file = tmpfile ();
    int ok;

    if (file == NULL) {
        fprintf (stderr, "tmpfile failed\n");
        exit (2);
    }
    fputs (text, file);
    rewind (file);
    ok = save_mod_header_read (file, err, err_size);
    fclose (file);
    return ok;
}

static void
test_empty_header (void)
{
    char err[512];
    char *header;
    short cells[WORLD_SIDE_LEN][WORLD_SIDE_LEN];

    reset_base_registry ();
    mod_loader_init ();

    header = save_mod_header_format ();
    CHECK (header != NULL);
    if (header != NULL) {
        CHECK (strncmp (header, "#lcm 1\n", 7) == 0);
        CHECK (strstr (header, "mods 0\n") != NULL);
        CHECK (strstr (header, "types 1\n") != NULL);
        CHECK (strstr (header, "type 0 base.type.0\n") != NULL);
        CHECK (strlen (header) >= 4
               && strcmp (header + strlen (header) - 4, "end\n") == 0);
        CHECK (parse_text (header, err, sizeof err));
        free (header);
    }

    memset (cells, 0, sizeof cells);
    CHECK (save_mod_remap_types (cells, err, sizeof err));
    CHECK (cells[0][0] == 0);
    CHECK (cells[WORLD_SIDE_LEN - 1][WORLD_SIDE_LEN - 1] == 0);
}

static void
test_mod_header (void)
{
    char err[512];
    char *header;
    short cells[WORLD_SIDE_LEN][WORLD_SIDE_LEN];
    struct GROUP group;
    static const char *const sparse_header =
        "#lcm 1\n"
        "mods 0\n"
        "types 3\n"
        "type 0 base.type.0\n"
        "type 650 test_park.park\n"
        "type 900 test_park.park_alt\n"
        "end\n";

    reset_base_registry ();
    mod_loader_init ();
    CHECK (mod_loader_load ("tests/fixtures/mods/valid", NULL,
                            test_lookup, test_graphic, err, sizeof err));

    header = save_mod_header_format ();
    CHECK (header != NULL);
    if (header != NULL) {
        CHECK (strstr (header, "mod test_park 1.0.0 1\n") != NULL);
        CHECK (strstr (header, "type 400 test_park.park\n") != NULL);
        CHECK (strstr (header, "type 401 test_park.park_alt\n") != NULL);
        CHECK (strstr (header, "type 0 base.type.0\n") != NULL);
        CHECK (parse_text (header, err, sizeof err));
        free (header);
    }

    memset (cells, 0, sizeof cells);
    cells[1][1] = 400;
    cells[2][2] = 401;
    CHECK (save_mod_remap_types (cells, err, sizeof err));
    CHECK (cells[1][1] == 400);
    CHECK (cells[2][2] == 401);

    /* Sparse saved IDs are mapped by key, not by an offset from a base. */
    CHECK (parse_text (sparse_header, err, sizeof err));
    memset (cells, 0, sizeof cells);
    cells[3][3] = 650;
    cells[4][4] = 900;
    CHECK (save_mod_remap_types (cells, err, sizeof err));
    CHECK (cells[3][3] == 400);
    CHECK (cells[4][4] == 401);

    /* The same keys at different current IDs must still remap correctly. */
    memset (&group, 0, sizeof group);
    snprintf (group.name, sizeof group.name, "Park");
    group.size = 1;
    mod_registry_reset ();
    CHECK (mod_register_group (0, &group));
    CHECK (mod_register_type (0, 0, NULL));
    CHECK (mod_register_group_keyed (42, "test_park.park", "test_park",
                                     &group));
    CHECK (mod_register_type_keyed (700, "test_park.park", "test_park",
                                    42, NULL));
    CHECK (mod_register_type_keyed (950, "test_park.park_alt", "test_park",
                                    42, NULL));
    CHECK (parse_text (sparse_header, err, sizeof err));
    memset (cells, 0, sizeof cells);
    cells[5][5] = 650;
    cells[6][6] = 900;
    CHECK (save_mod_remap_types (cells, err, sizeof err));
    CHECK (cells[5][5] == 700);
    CHECK (cells[6][6] == 950);

    mod_loader_shutdown ();
}

static void
test_missing_and_incompatible (void)
{
    char err[512];
    char *header;
    static const char *const version_header =
        "#lcm 1\n"
        "mods 1\n"
        "mod test_park 2.0.0 1\n"
        "types 0\n"
        "end\n";
    static const char *const api_header =
        "#lcm 1\n"
        "mods 1\n"
        "mod test_park 1.0.0 2\n"
        "types 0\n"
        "end\n";
    static const char *const ghost_header =
        "#lcm 1\n"
        "mods 1\n"
        "mod ghost 1.0.0 1\n"
        "types 0\n"
        "end\n";
    static const char *const type_header =
        "#lcm 1\n"
        "mods 1\n"
        "mod test_park 1.0.0 1\n"
        "types 1\n"
        "type 400 test_park.nope\n"
        "end\n";

    reset_base_registry ();
    mod_loader_init ();
    {
        int ok = mod_loader_load ("tests/fixtures/mods/valid", NULL,
                                  test_lookup, test_graphic, err, sizeof err);

        if (!ok)
            fprintf (stderr, "first reload failed: %s\n", err);
        CHECK (ok);
    }
    header = save_mod_header_format ();
    CHECK (header != NULL);

    /* The save's mod must already be enabled before the map is read. */
    mod_loader_shutdown ();
    err[0] = '\0';
    CHECK (header != NULL && !parse_text (header, err, sizeof err));
    CHECK (strstr (err, "requires mod test_park@1.0.0") != NULL);
    free (header);

    reset_base_registry ();
    mod_loader_init ();
    {
        int ok = mod_loader_load ("tests/fixtures/mods/valid", NULL,
                                  test_lookup, test_graphic, err, sizeof err);

        if (!ok)
            fprintf (stderr, "second reload failed: %s\n", err);
        CHECK (ok);
    }

    err[0] = '\0';
    CHECK (!parse_text (version_header, err, sizeof err));
    CHECK (strstr (err, "requires mod test_park@2.0.0") != NULL);
    CHECK (strstr (err, "version 1.0.0 is enabled") != NULL);

    err[0] = '\0';
    CHECK (!parse_text (api_header, err, sizeof err));
    CHECK (strstr (err, "requires mod test_park api 2") != NULL);
    CHECK (strstr (err, "api 1 is enabled") != NULL);

    err[0] = '\0';
    CHECK (!parse_text (ghost_header, err, sizeof err));
    CHECK (strstr (err, "mod ghost@1.0.0") != NULL);

    err[0] = '\0';
    CHECK (!parse_text (type_header, err, sizeof err));
    CHECK (strstr (err, "test_park.nope") != NULL);

    mod_loader_shutdown ();
}

static void
test_malformed_header (void)
{
    static const char *const bad[] = {
        "",
        "#lcm 2\nmods 0\ntypes 0\nend\n",
        "#lcm 1\nmods 0\ntypes 0\n",
        "#lcm 1\nmods -1\ntypes 0\nend\n",
        "#lcm 1\nmods 0 extra\ntypes 0\nend\n",
        "#lcm 1\nmods 0\ntypes 2\n"
        "type 0 base.type.0\ntype 0 base.type.0\nend\n",
        "#lcm 1\nmods 0\ntypes 2\n"
        "type 1 base.type.0\ntype 2 base.type.0\nend\n",
        "#lcm 1\nmods 0\ntypes 1\ntype 1024 base.type.0\nend\n",
        "#lcm 1\nmods 0\ntypes 1\ntype -1 base.type.0\nend\n",
        "#lcm 1\nmods 0\ntypes 1\ntype 5 base.type.999\nend\n",
        "#lcm 1\nmods 0\ntypes 1\ntype 5 base.type.0 extra\nend\n",
        "#lcm 1\nmods 0\ntypes 0\nstop\n",
        "#lcm 1\nmods 0\n"
    };
    char err[512];
    size_t i;

    reset_base_registry ();
    mod_loader_init ();

    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        err[0] = '\0';
        CHECK (!parse_text (bad[i], err, sizeof err));
        CHECK (err[0] != '\0');
    }
}

static void
test_remap_rollback (void)
{
    static const char *const header =
        "#lcm 1\n"
        "mods 0\n"
        "types 1\n"
        "type 0 base.type.0\n"
        "end\n";
    char err[512];
    short cells[WORLD_SIDE_LEN][WORLD_SIDE_LEN];

    reset_base_registry ();
    mod_loader_init ();
    CHECK (parse_text (header, err, sizeof err));

    memset (cells, 0, sizeof cells);
    cells[5][5] = 123;
    err[0] = '\0';
    CHECK (!save_mod_remap_types (cells, err, sizeof err));
    CHECK (strstr (err, "no mapping") != NULL);
    /* The validation pass must not write any cell. */
    CHECK (cells[5][5] == 123);
    CHECK (cells[0][0] == 0);
}

int
main (void)
{
    test_empty_header ();
    test_mod_header ();
    test_missing_and_incompatible ();
    test_malformed_header ();
    test_remap_rollback ();

    if (failures != 0) {
        fprintf (stderr, "%d save-mod test(s) failed\n", failures);
        return 1;
    }
    printf ("mod saves: %d checks passed\n", checks);
    return 0;
}
