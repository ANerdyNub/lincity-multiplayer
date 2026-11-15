/*
 * Native plugin fixture for the ABI rejection paths.
 *
 * Build with -DTEST_BAD_SIZE to fail the struct-size check; otherwise the
 * descriptor advertises an unsupported API version.  Both must be rejected
 * before any group or type is registered.
 */
#include <stddef.h>

#include "lincity_plugin.h"

#if defined(TEST_BAD_SIZE)
#define TEST_PLUGIN_SIZE (sizeof (lc_plugin_v1) - 1u)
#define TEST_PLUGIN_API LC_PLUGIN_API_VERSION
#else
#define TEST_PLUGIN_SIZE sizeof (lc_plugin_v1)
#define TEST_PLUGIN_API (LC_PLUGIN_API_VERSION + 1u)
#endif

static const lc_plugin_v1 test_bad_plugin = {
    TEST_PLUGIN_API,
    TEST_PLUGIN_SIZE,
    "native_bad",
    "1.0.0",
    NULL,
    NULL,
    NULL,
    0,
    NULL,
    0,
    NULL,
    0
};

LC_EXPORT const lc_plugin_v1 *
lc_plugin_entry (void)
{
    return &test_bad_plugin;
}
