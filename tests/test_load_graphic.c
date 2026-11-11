#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <unistd.h>

#include "fileutil.h"

static jmp_buf error_jmp;
static char last_error[LC_PATH_MAX];

void
do_error (char *s)
{
    snprintf (last_error, sizeof last_error, "%s", s);
    longjmp (error_jmp, 1);
}

static void
expect_error (const char *name)
{
    if (setjmp (error_jmp) == 0) {
        char *p = load_graphic ((char *) name);
        fprintf (stderr, "load_graphic(%s) unexpectedly returned %p\n",
                 name, (void *) p);
        exit (1);
    }
    if (last_error[0] == '\0')
        exit (1);
}

int
main (void)
{
    char dir[] = "/tmp/lincity-p25-XXXXXX";
    char path[LC_PATH_MAX], data[] = "png!";
    char *p;
    FILE *f;

    if (mkdtemp (dir) == NULL) {
        perror ("mkdtemp");
        return 1;
    }
    snprintf (graphic_path, sizeof graphic_path, "%s/", dir);

    expect_error ("missing.graphic");

    snprintf (path, sizeof path, "%s/empty.graphic", dir);
    f = fopen (path, "wb");
    if (f == NULL) {
        perror ("fopen");
        return 1;
    }
    fclose (f);
    expect_error ("empty.graphic");

    snprintf (path, sizeof path, "%s/valid.graphic", dir);
    f = fopen (path, "wb");
    if (f == NULL || fwrite (data, 1, sizeof data - 1, f) != sizeof data - 1) {
        perror ("write valid.graphic");
        return 1;
    }
    fclose (f);
    p = load_graphic ("valid.graphic");
    if (p == NULL || memcmp (p, data, sizeof data - 1) != 0) {
        fprintf (stderr, "load_graphic returned the wrong payload\n");
        return 1;
    }
    free (p);

    unlink (path);
    snprintf (path, sizeof path, "%s/empty.graphic", dir);
    unlink (path);
    rmdir (dir);
    puts ("load_graphic: missing, empty, and valid files are handled");
    return 0;
}
