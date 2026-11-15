/*
 * Native plugin fixture for tests/run_mod_loader.sh.
 *
 * The plugin owns a 2x2 group.  Its simulation callback checks the host
 * accessor contract: reads work, writes are limited to the owned origin,
 * plugin flag bits are masked, and CST_USED sub-tiles are not writable.
 * The resulting fields are inspected in the saved city.
 */
#include <stddef.h>

#include "lincity_plugin.h"

static int host_ok;

static int
test_init (lc_host *host)
{
    host_ok = host != NULL && host->size == sizeof (lc_host)
        && host->api_version == LC_HOST_API_VERSION
        && host->get_origin != NULL && host->get_group != NULL
        && host->get_type != NULL && host->get_field != NULL
        && host->get_flags != NULL && host->set_field != NULL
        && host->set_flags != NULL;
    return host_ok ? 0 : 1;
}

static void
test_shutdown (void)
{
}

static void
test_simulate (lc_host *host, int x, int y)
{
    int ox = -1;
    int oy = -1;
    int sx = -1;
    int sy = -1;
    uint16_t group = 0;
    uint16_t type = 0;
    uint32_t flags = 0;
    int32_t value = 0;
    int ok = host_ok;

    if (host == NULL)
        return;

    ok = ok
        && host->get_origin (host, x, y, &ox, &oy)
        && ox == x && oy == y
        && host->get_group (host, x, y, &group) && group == 42
        && host->get_type (host, x, y, &type) && type == 400
        && host->get_flags (host, x, y, &flags) && flags == 0x10;

    /* A base flag write leaves the existing base flag untouched. */
    ok = ok && host->set_flags (host, x, y, 0x20) == 1
        && host->get_flags (host, x, y, &flags) && flags == 0x10;

    /* Only the reserved bits may be changed by a plugin. */
    ok = ok && host->set_flags (host, x, y, 0xffffffffu) == 1
        && host->get_flags (host, x, y, &flags)
        && flags == 0x70000010u;
    ok = ok && host->set_flags (host, x, y, 0x10000010u) == 1
        && host->get_flags (host, x, y, &flags)
        && flags == 0x10000010u;

    ok = ok && host->set_field (host, x, y, LC_FIELD_POPULATION, 7)
        && host->set_field (host, x, y, LC_FIELD_COAL_RESERVE, 9)
        && host->set_field (host, x, y, LC_FIELD_ORE_RESERVE, 10)
        && host->set_field (host, x, y, LC_FIELD_INT_1, 123)
        && host->set_field (host, x, y, LC_FIELD_INT_2, 124)
        && host->set_field (host, x, y, LC_FIELD_INT_3, 111);
    ok = ok && host->get_field (host, x, y, LC_FIELD_INT_3, &value)
        && value == 111;

    /* The sub-tile resolves to the origin but cannot be written. */
    ok = ok && host->get_origin (host, x + 1, y, &sx, &sy)
        && sx == x && sy == y
        && host->get_type (host, x + 1, y, &type) && type == 39
        && host->set_field (host, x + 1, y, LC_FIELD_INT_1, 777) == 0
        && host->get_field (host, x + 1, y, LC_FIELD_INT_1, &value)
        && value == 0
        && host->set_flags (host, x + 1, y, 0x10000000u) == 0;

    /* A cell owned by a different group/tile is not writable. */
    ok = ok && host->set_field (host, x + 2, y, LC_FIELD_INT_3, 999) == 0;

    host->set_field (host, x, y, LC_FIELD_INT_5, ok ? 1 : 2);
    host->set_field (host, x, y, LC_FIELD_INT_6, 1);
    host->set_field (host, x, y, LC_FIELD_INT_7, 1);
}

static void
test_mps (lc_host *host, int x, int y)
{
    (void) host;
    (void) x;
    (void) y;
}

static const lc_behavior_v1 test_behavior = {
    sizeof (lc_behavior_v1),
    "test_behavior",
    test_simulate,
    test_mps,
    NULL,
    NULL
};

static const lc_group_desc_v1 test_group = {
    sizeof (lc_group_desc_v1),
    "park",
    "Native Test Park",
    42,
    2,
    0,
    100,
    1,
    1000,
    0,
    1000,
    0,
    "test_behavior"
};

static const lc_type_desc_v1 test_type = {
    sizeof (lc_type_desc_v1),
    "park",
    400,
    42,
    "park.png"
};

static const lc_plugin_v1 test_plugin = {
    LC_PLUGIN_API_VERSION,
    sizeof (lc_plugin_v1),
    "native_test",
    "1.0.0",
    test_init,
    test_shutdown,
    &test_behavior,
    1,
    &test_group,
    1,
    &test_type,
    1
};

LC_EXPORT const lc_plugin_v1 *
lc_plugin_entry (void)
{
    return &test_plugin;
}
