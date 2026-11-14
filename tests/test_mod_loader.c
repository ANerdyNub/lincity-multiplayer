/* ---------------------------------------------------------------------- *
 * test_mod_loader.c
 * Focused tests for the data-mod INI parser, discovery, ID preview,
 * atomic registration, and validation hooks.
 * ---------------------------------------------------------------------- */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lctypes.h"
#include "lintypes.h"
#include "mod_ini.h"
#include "mod_loader.h"
#include "mod_registry.h"
#include "validation.h"

struct GROUP main_groups[MAX_GROUPS];
struct TYPE main_types[MAX_TYPES];

/* The production definition lives in lintypes.c; the loader test links only
   the registry and loader, so supply the small lookup they share. */
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
    tile = malloc (16 * 16);
    if (tile == NULL) {
        if (err != NULL && err_size > 0)
            snprintf (err, err_size, "out of memory");
        return NULL;
    }
    memset (tile, 7, 16 * 16);
    return tile;
}

static void
reset_registry (void)
{
    struct GROUP group;

    memset (&group, 0, sizeof group);
    snprintf (group.name, sizeof group.name, "Bare");
    group.size = 1;
    group.colour = 1;

    mod_registry_reset ();
    CHECK (mod_register_group (0, &group));
    CHECK (mod_register_type (CST_GREEN, 0, NULL));
    CHECK (num_groups == 1);
    CHECK (num_types == 1);
}

static int
expect_failure (const char *dir, const char *needle)
{
    char err[512];
    int ok;

    mod_loader_init ();
    ok = mod_loader_load (dir, NULL, test_lookup, test_graphic,
                          err, sizeof err);
    if (ok) {
        fprintf (stderr, "expected failure for %s\n", dir);
        return 0;
    }
    if (needle != NULL && strstr (err, needle) == NULL) {
        fprintf (stderr, "error for %s did not contain '%s': %s\n",
                 dir, needle, err);
        return 0;
    }
    return 1;
}

static void
test_valid_mod (void)
{
    char err[512];
    struct MOD_INI ini;

    reset_registry ();
    mod_loader_init ();
    CHECK (mod_loader_load ("tests/fixtures/mods/valid", NULL,
                            test_lookup, test_graphic, err, sizeof err));
    CHECK (mod_find_group_by_key ("test_park.park") == 42);
    CHECK (mod_find_type_by_key ("test_park.park") == 400);
    CHECK (mod_find_type_by_key ("test_park.park_alt") == 401);
    CHECK (strcmp (main_groups[42].owner, "test_park") == 0);
    CHECK (strcmp (main_groups[42].category, "Parks") == 0);
    CHECK (main_groups[42].menu_order == 42);
    CHECK (main_groups[42].default_type == 400);
    CHECK (main_groups[42].button != NULL);
    CHECK (main_groups[42].button != main_types[400].graphic);
    CHECK (strstr (main_groups[42].help, "park.hlp") != NULL);
    CHECK (main_types[400].graphic != NULL);
    CHECK (main_types[401].graphic != NULL);
    CHECK (main_types[400].graphic != main_types[401].graphic);
    CHECK (strcmp (main_types[401].name, "Alt Park") == 0);
    CHECK (main_types[400].name[0] == '\0');
    CHECK (mod_group_type_count (42) == 2);
    CHECK (mod_group_type_at (42, 0) == 400);
    CHECK (mod_group_type_at (42, 1) == 401);
    CHECK (mod_group_type_at (42, 2) == -1);
    CHECK (mod_group_default_type (42) == 400);
    CHECK (mod_loader_enabled_count () == 1);
    CHECK (strcmp (mod_loader_enabled_id (0), "test_park") == 0);
    CHECK (strcmp (mod_loader_enabled_version (0), "1.0.0") == 0);
    CHECK (type_ok (400));
    CHECK (type_ok (401));
    CHECK (group_ok (42));

    mod_ini_init (&ini);
    CHECK (mod_ini_parse ("tests/fixtures/mods/valid/valid_mod/mod.ini",
                          &ini, err, sizeof err));
    CHECK (strcmp (mod_ini_get (&ini, "mod", "id"), "test_park") == 0);
    CHECK (strcmp (mod_ini_get (&ini, "group:park", "behavior"),
                   "parkland") == 0);
    CHECK (mod_ini_section_count (&ini, "group:park") == 11);
    mod_ini_free (&ini);

    mod_loader_shutdown ();
}

static void
test_config_order (void)
{
    char err[512];

    reset_registry ();
    mod_loader_init ();
    CHECK (mod_loader_load ("tests/fixtures/mods/config", NULL,
                            test_lookup, test_graphic, err, sizeof err));
    /* mods.conf enables config_b first even though discovery sorts
       config_a before config_b. */
    CHECK (mod_find_group_by_key ("config_b.park") == 42);
    CHECK (mod_find_group_by_key ("config_a.park") == 43);
    CHECK (mod_find_type_by_key ("config_b.park") == 400);
    CHECK (mod_find_type_by_key ("config_a.park") == 401);
    CHECK (strcmp (main_groups[42].category, "Mods") == 0);
    CHECK (strcmp (main_groups[43].category, "Mods") == 0);
    CHECK (main_groups[42].menu_order == 1000);
    CHECK (main_groups[43].menu_order == 1001);
    CHECK (main_groups[42].button == main_types[400].graphic);
    CHECK (main_groups[43].button == main_types[401].graphic);
    CHECK (main_groups[42].help[0] == '\0');
    CHECK (mod_loader_enabled_count () == 2);
    CHECK (strcmp (mod_loader_enabled_id (0), "config_b") == 0);
    CHECK (strcmp (mod_loader_enabled_id (1), "config_a") == 0);
    mod_loader_shutdown ();
}

static void
test_duplicate_id (void)
{
    reset_registry ();
    CHECK (expect_failure ("tests/fixtures/mods/duplicate_id",
                           "duplicate mod id"));
    CHECK (num_groups == 1);
    CHECK (num_types == 1);
}

static void
test_duplicate_type_id (void)
{
    reset_registry ();
    CHECK (expect_failure ("tests/fixtures/mods/duplicate_type",
                           "duplicate ID 400"));
    CHECK (num_groups == 1);
    CHECK (num_types == 1);
}

static void
test_bad_behavior (void)
{
    reset_registry ();
    CHECK (expect_failure ("tests/fixtures/mods/bad_behavior",
                           "unknown behavior"));
    CHECK (num_groups == 1);
    CHECK (num_types == 1);
}

static void
test_missing_graphic (void)
{
    reset_registry ();
    CHECK (expect_failure ("tests/fixtures/mods/missing_graphic",
                           "graphic is missing"));
    CHECK (num_groups == 1);
    CHECK (num_types == 1);
}

static void
test_path_traversal (void)
{
    reset_registry ();
    CHECK (expect_failure ("tests/fixtures/mods/traversal",
                           "unsafe graphic path"));
    CHECK (num_groups == 1);
    CHECK (num_types == 1);
}

static void
test_rollback (void)
{
    reset_registry ();
    CHECK (expect_failure ("tests/fixtures/mods/rollback",
                           "unknown behavior"));
    CHECK (num_groups == 1);
    CHECK (num_types == 1);
    CHECK (mod_find_group_by_key ("good_mod.park") == -1);
    CHECK (mod_find_type_by_key ("good_mod.park") == -1);
}

static void
test_synthetic_map (void)
{
    static short type[WORLD_SIDE_LEN][WORLD_SIDE_LEN];
    static short group[WORLD_SIDE_LEN][WORLD_SIDE_LEN];
    static Map_Point_Info info[WORLD_SIDE_LEN][WORLD_SIDE_LEN];
    char err[512];
    int x;
    int y;

    reset_registry ();
    mod_loader_init ();
    CHECK (mod_loader_load ("tests/fixtures/mods/valid", NULL,
                            test_lookup, test_graphic, err, sizeof err));

    for (x = 0; x < WORLD_SIDE_LEN; x++) {
        for (y = 0; y < WORLD_SIDE_LEN; y++) {
            type[x][y] = CST_GREEN;
            group[x][y] = 0;
            memset (&info[x][y], 0, sizeof info[x][y]);
        }
    }
    type[10][10] = 400;
    group[10][10] = 42;
    CHECK (map_state_ok (type, group, info, -1, NULL, NULL,
                         -1, NULL, NULL));

    type[10][10] = 401;
    CHECK (map_state_ok (type, group, info, -1, NULL, NULL,
                         -1, NULL, NULL));

    /* An unknown module type must fail the shared validator. */
    type[10][10] = 999;
    CHECK (!map_state_ok (type, group, info, -1, NULL, NULL,
                          -1, NULL, NULL));
    mod_loader_shutdown ();
}

static void
test_ini_errors (void)
{
    char dir[] = "/tmp/lc-mod-ini-XXXXXX";
    char path[256];
    char err[512];
    struct MOD_INI ini;
    FILE *fp;

    CHECK (mkdtemp (dir) != NULL);
    snprintf (path, sizeof path, "%s/dup.ini", dir);
    fp = fopen (path, "w");
    CHECK (fp != NULL);
    if (fp != NULL) {
        fputs ("[mod]\nid = one\nid = two\n", fp);
        fclose (fp);
        mod_ini_init (&ini);
        CHECK (!mod_ini_parse (path, &ini, err, sizeof err));
        CHECK (strstr (err, "duplicate key") != NULL);
        mod_ini_free (&ini);
    }

    snprintf (path, sizeof path, "%s/short.ini", dir);
    fp = fopen (path, "wb");
    CHECK (fp != NULL);
    if (fp != NULL) {
        fwrite ("\xef", 1, 1, fp);
        fclose (fp);
        mod_ini_init (&ini);
        CHECK (!mod_ini_parse (path, &ini, err, sizeof err));
        mod_ini_free (&ini);
    }

    snprintf (path, sizeof path, "%s/comment.ini", dir);
    fp = fopen (path, "w");
    CHECK (fp != NULL);
    if (fp != NULL) {
        fputs ("[Mod]\nName = Test # comment\n", fp);
        fclose (fp);
        mod_ini_init (&ini);
        CHECK (mod_ini_parse (path, &ini, err, sizeof err));
        CHECK (strcmp (mod_ini_get (&ini, "mod", "name"), "Test") == 0);
        mod_ini_free (&ini);
    }

    unlink (path);
}

static void
test_config_disable (void)
{
    char dir[] = "/tmp/lc-mod-conf-XXXXXX";
    char path[256];
    char err[512];
    FILE *fp;

    CHECK (mkdtemp (dir) != NULL);
    snprintf (path, sizeof path, "%s/mods.conf", dir);
    fp = fopen (path, "w");
    CHECK (fp != NULL);
    if (fp != NULL) {
        fputs ("config_b = 1\nconfig_a = 0\n", fp);
        fclose (fp);
    }

    reset_registry ();
    mod_loader_init ();
    CHECK (mod_loader_load ("tests/fixtures/mods/config", path,
                            test_lookup, test_graphic, err, sizeof err));
    CHECK (mod_loader_enabled_count () == 1);
    CHECK (strcmp (mod_loader_enabled_id (0), "config_b") == 0);
    CHECK (mod_find_group_by_key ("config_a.park") == -1);
    mod_loader_shutdown ();
    unlink (path);
}

int
main (void)
{
    test_valid_mod ();
    test_config_order ();
    test_duplicate_id ();
    test_duplicate_type_id ();
    test_bad_behavior ();
    test_missing_graphic ();
    test_path_traversal ();
    test_rollback ();
    test_synthetic_map ();
    test_ini_errors ();
    test_config_disable ();

    if (failures != 0) {
        fprintf (stderr, "%d/%d checks failed\n", failures, checks);
        return 1;
    }
    printf ("PASS: %d mod-loader checks\n", checks);
    return 0;
}
