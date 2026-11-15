/*
 * Native plugin fixture for tests/run_mod_loader.sh.
 *
 * The plugin registers one group and one type through the provisional ABI.
 * Its callbacks are deliberately no-ops; this test proves the dynamic loader,
 * ABI checks, manifest opt-in, and atomic registration path.
 */
#include <stddef.h>

#include "lincity_plugin.h"

static int
test_init (lc_host *host)
{
    (void) host;
    return 0;
}

static void
test_shutdown (void)
{
}

static void
test_simulate (lc_host *host, int x, int y)
{
    (void) host;
    (void) x;
    (void) y;
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
    1,
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
