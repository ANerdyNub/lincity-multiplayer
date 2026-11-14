/* ---------------------------------------------------------------------- *
 * test_mod_menu.c
 * Headless tests for the build-panel menu/page model.
 * ---------------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>

#include "lctypes.h"
#include "lin-city.h"
#include "mod_menu.h"

static int failures;
static int checks;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

struct BASE_SNAPSHOT
{
    int group;
    int type;
    const char *help;
};

/* Page 0 must remain exactly the original 32 buttons in slot order. */
static const struct BASE_SNAPSHOT base_snapshot[MOD_MENU_SLOTS] = {
    { GROUP_RESIDENCE_LL,       CST_RESIDENCE_LL,       "residential.hlp" },
    { GROUP_ORGANIC_FARM,       CST_FARM_O0,            "farm.hlp" },
    { GROUP_MARKET,             CST_MARKET_EMPTY,       "market.hlp" },
    { GROUP_COMMUNE,            CST_COMMUNE_1,          "commune.hlp" },
    { GROUP_OREMINE,            CST_OREMINE_1,          "oremine.hlp" },
    { GROUP_TIP,                CST_TIP_0,              "tip.hlp" },
    { GROUP_PARKLAND,           CST_PARKLAND_PLANE,     "park.hlp" },
    { GROUP_CRICKET,            CST_CRICKET_1,          "cricket.hlp" },
    { GROUP_MILL,               CST_MILL_0,             "mill.hlp" },
    { GROUP_PORT,               CST_EX_PORT,            "port.hlp" },
    { GROUP_COALMINE,           CST_COALMINE_EMPTY,     "coalmine.hlp" },
    { GROUP_UNIVERSITY,         CST_UNIVERSITY,         "university.hlp" },
    { GROUP_INDUSTRY_H,         CST_INDUSTRY_H_C,       "industryh.hlp" },
    { GROUP_POWER_LINE,         CST_POWERL_H_L,         "powerline.hlp" },
    { GROUP_SUBSTATION,         CST_SUBSTATION_R,       "substation.hlp" },
    { GROUP_SOLAR_POWER,        CST_POWERS_SOLAR,       "powerssolar.hlp" },
    { GROUP_BARE,               CST_GREEN,              "bulldoze.hlp" },
    { GROUP_MONUMENT,           CST_MONUMENT_0,         "monument.hlp" },
    { GROUP_POTTERY,            CST_POTTERY_0,          "pottery.hlp" },
    { GROUP_TRACK,              CST_TRACK_LR,           "track.hlp" },
    { GROUP_WATER,              CST_WATER,              "river.hlp" },
    { GROUP_SCHOOL,             CST_SCHOOL,             "school.hlp" },
    { GROUP_BLACKSMITH,         CST_BLACKSMITH_0,       "blacksmith.hlp" },
    { GROUP_FIRESTATION,        CST_FIRESTATION_1,      "firestation.hlp" },
    { GROUP_WINDMILL,           CST_WINDMILL_1_R,       "windmill.hlp" },
    { GROUP_ROAD,               CST_ROAD_LR,            "road.hlp" },
    { GROUP_HEALTH,             CST_HEALTH,             "health.hlp" },
    { GROUP_INDUSTRY_L,         CST_INDUSTRY_L_C,       "industryl.hlp" },
    { GROUP_RAIL,               CST_RAIL_LR,            "rail.hlp" },
    { GROUP_COAL_POWER,         CST_POWERS_COAL_EMPTY,  "powerscoal.hlp" },
    { GROUP_RECYCLE,            CST_RECYCLE,            "recycle.hlp" },
    { GROUP_ROCKET,             CST_ROCKET_1,           "rocket.hlp" }
};

static char base_graphics[MOD_MENU_SLOTS][4];

static void
add_base_page (void)
{
    int i;

    mod_menu_reset ();
    for (i = 0; i < MOD_MENU_SLOTS; i++) {
        CHECK (mod_menu_add_base (base_snapshot[i].group,
                                  base_snapshot[i].type,
                                  base_graphics[i],
                                  base_snapshot[i].help));
    }
}

static void
test_page_zero_is_frozen (void)
{
    struct MOD_MENU_ENTRY *entry;
    int i;

    mod_menu_reset ();
    CHECK (!mod_menu_add_mod (100, 500, NULL, NULL, NULL, 1));

    for (i = 0; i < MOD_MENU_SLOTS - 1; i++) {
        CHECK (mod_menu_add_base (base_snapshot[i].group,
                                  base_snapshot[i].type,
                                  base_graphics[i],
                                  base_snapshot[i].help));
    }
    CHECK (!mod_menu_build ());
    CHECK (mod_menu_page_count () == 0);

    CHECK (mod_menu_add_base (base_snapshot[i].group,
                              base_snapshot[i].type,
                              base_graphics[i],
                              base_snapshot[i].help));
    CHECK (mod_menu_add_mod (100, 500, base_graphics[0], "mod.hlp",
                             "Test", 42));
    CHECK (mod_menu_build ());
    CHECK (mod_menu_page_count () == 2);
    CHECK (mod_menu_page_is_base (0));
    CHECK (!mod_menu_page_is_base (1));
    CHECK (mod_menu_page_size (0) == MOD_MENU_SLOTS);
    CHECK (strcmp (mod_menu_page_category (0), "Base") == 0);
    CHECK (strcmp (mod_menu_page_category (1), "Test") == 0);

    for (i = 0; i < MOD_MENU_SLOTS; i++) {
        entry = mod_menu_entry (0, i);
        CHECK (entry != NULL);
        if (entry == NULL)
            continue;
        CHECK (entry->group == base_snapshot[i].group);
        CHECK (entry->type == base_snapshot[i].type);
        CHECK (entry->graphic == base_graphics[i]);
        CHECK (strcmp (entry->help, base_snapshot[i].help) == 0);
        CHECK (entry->base == 1);
        CHECK (strcmp (entry->category, "Base") == 0);
        CHECK (entry->order == i);
        CHECK (entry->available == 1);
        CHECK (entry->help_seen == 0);
    }
    CHECK (mod_menu_entry (0, MOD_MENU_SLOTS) == NULL);
    CHECK (mod_menu_entry (1, 1) == NULL);
    CHECK (mod_menu_entry (-1, 0) == NULL);

    entry = mod_menu_entry (1, 0);
    CHECK (entry != NULL);
    if (entry != NULL) {
        CHECK (entry->group == 100);
        CHECK (entry->type == 500);
        CHECK (entry->base == 0);
        CHECK (entry->available == 1);
        CHECK (strcmp (entry->help, "mod.hlp") == 0);
    }
}

static void
test_category_order_and_stability (void)
{
    int page;
    int slot;

    add_base_page ();
    CHECK (mod_menu_add_mod (110, 510, NULL, NULL, "Zeta", 5));
    CHECK (mod_menu_add_mod (101, 501, NULL, NULL, "Alpha", 30));
    CHECK (mod_menu_add_mod (102, 502, NULL, NULL, "Alpha", 10));
    CHECK (mod_menu_add_mod (103, 503, NULL, NULL, "Alpha", 10));
    CHECK (mod_menu_add_mod (104, 504, NULL, NULL, "Beta", -1));
    CHECK (mod_menu_add_mod (105, 505, NULL, NULL, "", 7));
    CHECK (mod_menu_build ());

    /* Categories are ordered by name: Alpha, Beta, Mods, Zeta. */
    CHECK (mod_menu_page_count () == 5);
    CHECK (strcmp (mod_menu_page_category (1), "Alpha") == 0);
    CHECK (strcmp (mod_menu_page_category (2), "Beta") == 0);
    CHECK (strcmp (mod_menu_page_category (3), "Mods") == 0);
    CHECK (strcmp (mod_menu_page_category (4), "Zeta") == 0);

    /* Equal order values keep insertion order. */
    CHECK (mod_menu_entry (1, 0)->group == 102);
    CHECK (mod_menu_entry (1, 1)->group == 103);
    CHECK (mod_menu_entry (1, 2)->group == 101);
    CHECK (mod_menu_entry (2, 0)->group == 104);
    CHECK (mod_menu_entry (3, 0)->group == 105);
    CHECK (mod_menu_entry (4, 0)->group == 110);

    CHECK (mod_menu_find_group (103, &page, &slot));
    CHECK (page == 1);
    CHECK (slot == 1);
    CHECK (!mod_menu_find_group (999, &page, &slot));
}

static void
test_pagination (void)
{
    int i;

    add_base_page ();
    for (i = 0; i < 34; i++)
        CHECK (mod_menu_add_mod (200 + i, 600 + i, NULL, NULL, "Many", i));
    CHECK (mod_menu_add_mod (300, 700, NULL, NULL, "Next", 0));
    CHECK (mod_menu_build ());

    /* Base, Many x2, Next. */
    CHECK (mod_menu_page_count () == 4);
    CHECK (mod_menu_page_size (1) == MOD_MENU_SLOTS);
    CHECK (mod_menu_page_size (2) == 2);
    CHECK (mod_menu_page_size (3) == 1);
    CHECK (strcmp (mod_menu_page_category (1), "Many") == 0);
    CHECK (strcmp (mod_menu_page_category (2), "Many") == 0);
    CHECK (mod_menu_entry (1, 0)->group == 200);
    CHECK (mod_menu_entry (1, 31)->group == 231);
    CHECK (mod_menu_entry (2, 0)->group == 232);
    CHECK (mod_menu_entry (2, 1)->group == 233);
    CHECK (mod_menu_entry (3, 0)->group == 300);
}

static void
test_limits_and_defaults (void)
{
    struct MOD_MENU_ENTRY *entry;
    int i;

    add_base_page ();
    CHECK (mod_menu_add_mod (100, 500, NULL, "0123456789012345678901234567890123456789XYZ",
                             NULL, 0));
    CHECK (mod_menu_build ());
    entry = mod_menu_entry (1, 0);
    CHECK (entry != NULL);
    if (entry != NULL) {
        CHECK (strlen (entry->help) == MOD_MENU_HELP_LEN);
        CHECK (strcmp (entry->category, "Mods") == 0);
    }

    mod_menu_reset ();
    CHECK (mod_menu_add_base (0, 0, NULL, NULL));
    CHECK (mod_menu_add_base (1, 1, NULL, NULL));
    CHECK (!mod_menu_build ());

    mod_menu_reset ();
    for (i = 0; i < MOD_MENU_MAX_ENTRIES - MOD_MENU_SLOTS; i++) {
        if (!mod_menu_add_base (i, i, NULL, NULL))
            break;
    }
    CHECK (i == MOD_MENU_SLOTS);
    for (i = 0; i < MOD_MENU_MAX_ENTRIES - MOD_MENU_SLOTS; i++) {
        if (!mod_menu_add_mod (1000 + i, 2000 + i, NULL, NULL, "Big", i))
            break;
    }
    CHECK (i == MOD_MENU_MAX_ENTRIES - MOD_MENU_SLOTS);
    CHECK (!mod_menu_add_mod (9999, 9999, NULL, NULL, "Big", 0));
}

int
main (void)
{
    test_page_zero_is_frozen ();
    test_category_order_and_stability ();
    test_pagination ();
    test_limits_and_defaults ();

    printf ("test_mod_menu: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
