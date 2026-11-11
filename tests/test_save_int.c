#define _GNU_SOURCE
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "save_parse.h"

int
main (void)
{
    char buf[] = "-2147483648\n2147483647\n2147483648\n-2147483649\nx\n";
    FILE *f = fmemopen (buf, strlen (buf), "r");
    int v;

    if (!f) {
        fprintf (stderr, "FAIL: fmemopen\n");
        return 1;
    }
    if (!save_read_int (f, &v) || v != INT_MIN) {
        fprintf (stderr, "FAIL: INT_MIN parsed as %d\n", v);
        return 1;
    }
    if (!save_read_int (f, &v) || v != INT_MAX) {
        fprintf (stderr, "FAIL: INT_MAX parsed as %d\n", v);
        return 1;
    }
    if (save_read_int (f, &v)) {
        fprintf (stderr, "FAIL: positive overflow accepted as %d\n", v);
        return 1;
    }
    if (save_read_int (f, &v)) {
        fprintf (stderr, "FAIL: negative overflow accepted as %d\n", v);
        return 1;
    }
    if (save_read_int (f, &v)) {
        fprintf (stderr, "FAIL: non-number accepted as %d\n", v);
        return 1;
    }
    fclose (f);
    puts ("save int: INT_MIN/INT_MAX accepted; overflow rejected");
    return 0;
}
