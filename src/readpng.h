/* ---------------------------------------------------------------------- *
 * readpng.h
 * This file is part of lincity (see meta/COPYRIGHT for copyright information).
 * ---------------------------------------------------------------------- */
#ifndef __readpng_h__
#define __readpng_h__

#include <stddef.h>

int load_png_graphics (char *txt_filename, char *png_filename);

/* Generic 16x16 tile loader for data mods.  The returned buffer is a
   malloc'ed 256-byte array of palette indices, or NULL with a short
   diagnostic in err. */
char *load_png_tile (const char *path, char *err, size_t err_size);

#endif /* __readpng_h__ */
