#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cliglobs.h"
#include "fileutil.h"
#include "geometry.h"
#include "ldsvgui.h"

/* Minimal globals and UI stubs for draw_save_dir(). */
Screen_Geometry scr;
char *lc_save_dir;
int lc_save_dir_len;
char save_names[10][42];

int
directory_exists (char *dir)
{
    (void) dir;
    return 1;
}

int
verify_city (char *cname)
{
    (void) cname;
    return 1;
}

void
Fgl_setfontcolors (int bg, int fg)
{
    (void) bg;
    (void) fg;
}

void
Fgl_write (int x, int y, char *s)
{
    (void) x;
    (void) y;
    (void) s;
}

void
malloc_failure (void)
{
    abort ();
}

int
main (void)
{
    char path[] = "/tmp/lincity-p24-XXXXXX";
    int fd;

    fd = mkstemp (path);
    if (fd < 0) {
        perror ("mkstemp");
        return 1;
    }
    close (fd);

    /* A regular file is not a directory: opendir() must fail cleanly. */
    lc_save_dir = path;
    lc_save_dir_len = strlen (path);
    draw_save_dir (0);

    unlink (path);
    puts ("save dir: an unopenable directory leaves the dialog alive");
    return 0;
}
