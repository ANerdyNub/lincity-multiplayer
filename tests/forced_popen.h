/*
 * Force popen() to fail while compiling the production fileutil translation
 * unit for the P19 regression test.  The function has internal linkage in each
 * translation unit, so this is safe to include.
 */
#include <stdio.h>

static FILE *
forced_popen_failure (const char *cmd, const char *mode)
{
    (void) cmd;
    (void) mode;
    return NULL;
}

#define popen forced_popen_failure
