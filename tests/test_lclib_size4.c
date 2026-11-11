/*
 * L4 regression: num_to_ansi() took a buffer size but wrote size + 1 bytes
 * in the size == 4 path.  A 4-byte buffer must never receive a 4-character
 * string plus its terminator.
 */
#include <stdio.h>
#include <string.h>

#include "lclib.h"

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

int
main (void)
{
    char small[4];
    char normal[12];

    memset (small, 'Q', sizeof (small));
    num_to_ansi (small, sizeof (small), 1234);
    CHECK (strlen (small) < sizeof (small));
    CHECK (small[sizeof (small) - 1] == '\0');

    memset (normal, 'Q', sizeof (normal));
    num_to_ansi (normal, sizeof (normal), 1234567);
    CHECK (strlen (normal) < sizeof (normal));

    if (failures != 0) {
        fprintf (stderr, "%d lclib test(s) failed\n", failures);
        return 1;
    }
    printf ("lclib: num_to_ansi respects a 4-byte buffer\n");
    return 0;
}
