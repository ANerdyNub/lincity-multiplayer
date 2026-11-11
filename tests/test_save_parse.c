#include <stdio.h>

#include "save_parse.h"

static Map test_map;

static int
truncated_cell (int fields)
{
    FILE *ofile = tmpfile ();
    int i;
    int rc;

    if (ofile == NULL)
        return -1;
    for (i = 0; i < fields; i++)
        fputs ("0\n", ofile);
    rewind (ofile);
    rc = save_read_map (ofile, &test_map);
    fclose (ofile);
    return rc;
}

int
main (void)
{
    int failures = 0;

    /* 40 fields are the entire first Map_Point_Info except pollution/type;
       EOF at the pollution read reaches save_parse.h:109. */
    if (truncated_cell (40) != 0) {
        fprintf (stderr, "FAIL: truncated pollution field was accepted\n");
        failures++;
    }
    /* Add pollution, leave out type; EOF reaches save_parse.h:112. */
    if (truncated_cell (41) != 0) {
        fprintf (stderr, "FAIL: truncated type field was accepted\n");
        failures++;
    }

    if (failures != 0) {
        fprintf (stderr, "%d save-parser test(s) failed\n", failures);
        return 1;
    }
    puts ("save parse: truncated pollution/type fields rejected");
    return 0;
}
