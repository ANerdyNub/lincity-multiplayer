/*
 * P19/M8 regression: a missing save file must return NULL, and the close
 * helper must make the NULL case a no-op instead of dereferencing it.
 */
#include <stdio.h>

extern FILE *fopen_read_gzipped (char *fn);
extern void fclose_read_gzipped (FILE *fp);

int
main (void)
{
    FILE *fp = fopen_read_gzipped ("this-file-does-not-exist");

    if (fp != NULL) {
        fclose_read_gzipped (fp);
        fputs ("forced popen did not fail\n", stderr);
        return 1;
    }
    fclose_read_gzipped (NULL);
    puts ("pclose: failed popen is reported and NULL close is a no-op");
    return 0;
}
