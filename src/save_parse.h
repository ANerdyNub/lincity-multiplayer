/* ---------------------------------------------------------------------- *
 * save_parse.h
 * Map section reader shared by load_city() and the save-parser fuzzer.
 * ---------------------------------------------------------------------- */

#ifndef __save_parse_h__
#define __save_parse_h__

#include <limits.h>
#include <stdio.h>

#include "engglobs.h"

/* The save map has 420,000 one-byte decimal fields.  fscanf("%d") per
   field is the parser's throughput bottleneck (it locks the stream and
   reparses the format for every byte), so use a small unlocked scanner.
   The accepted syntax is the integer subset the format writes: optional
   sign, decimal digits, and leading whitespace. */
static inline int
save_getc (FILE *ofile)
{
#if defined(HAVE_GETC_UNLOCKED)
    return getc_unlocked (ofile);
#else
    return getc (ofile);
#endif
}

static inline int
save_read_int (FILE *ofile, int *value)
{
    int c, sign = 1;
    unsigned int v = 0;

    do {
        c = save_getc (ofile);
    } while (c == ' ' || c == '\t' || c == '\n' || c == '\r'
             || c == '\f' || c == '\v');
    if (c == EOF)
        return 0;
    if (c == '-' || c == '+') {
        sign = c == '-' ? -1 : 1;
        c = save_getc (ofile);
    }
    if (c < '0' || c > '9') {
        if (c != EOF)
            ungetc (c, ofile);
        return 0;
    }
    do {
        v = v * 10u + (unsigned int) (c - '0');
        c = save_getc (ofile);
    } while (c >= '0' && c <= '9');
    if (c != EOF)
        ungetc (c, ofile);
    if (sign < 0) {
        if (v > (unsigned int) INT_MAX + 1u)
            return 0;
        *value = (int) (-(long long) v);
    } else {
        if (v > (unsigned int) INT_MAX)
            return 0;
        *value = (int) v;
    }
    return 1;
}

/* Read the 100x100 map exactly as the text save format stores it: each
   native field is written as one decimal byte per line, least-significant
   byte first.  Keeping this byte-wise makes the format independent of the
   host's integer representation, as the writer is. */
static inline int
save_read_map (FILE *ofile, Map *map)
{
    int x, y, z, n;

    for (x = 0; x < WORLD_SIDE_LEN; x++) {
        for (y = 0; y < WORLD_SIDE_LEN; y++) {
#define SAVE_READ_BYTE(field) do { \
    if (!save_read_int (ofile, &n)) return 0; \
    *(((unsigned char *) &(field)) + z) = (unsigned char) n; \
} while (0)

            for (z = 0; z < (int) sizeof (int); z++)
                SAVE_READ_BYTE (map->info[x][y].population);
            for (z = 0; z < (int) sizeof (int); z++)
                SAVE_READ_BYTE (map->info[x][y].flags);
            for (z = 0; z < (int) sizeof (unsigned short); z++)
                SAVE_READ_BYTE (map->info[x][y].coal_reserve);
            for (z = 0; z < (int) sizeof (unsigned short); z++)
                SAVE_READ_BYTE (map->info[x][y].ore_reserve);
            for (z = 0; z < (int) sizeof (int); z++)
                SAVE_READ_BYTE (map->info[x][y].int_1);
            for (z = 0; z < (int) sizeof (int); z++)
                SAVE_READ_BYTE (map->info[x][y].int_2);
            for (z = 0; z < (int) sizeof (int); z++)
                SAVE_READ_BYTE (map->info[x][y].int_3);
            for (z = 0; z < (int) sizeof (int); z++)
                SAVE_READ_BYTE (map->info[x][y].int_4);
            for (z = 0; z < (int) sizeof (int); z++)
                SAVE_READ_BYTE (map->info[x][y].int_5);
            for (z = 0; z < (int) sizeof (int); z++)
                SAVE_READ_BYTE (map->info[x][y].int_6);
            for (z = 0; z < (int) sizeof (int); z++)
                SAVE_READ_BYTE (map->info[x][y].int_7);
#undef SAVE_READ_BYTE

            if (!save_read_int (ofile, &n))
                return 0;
            map->pollution[x][y] = n;
            if (!save_read_int (ofile, &n))
                return 0;
            map->type[x][y] = (short) n;
        }
    }
    return 1;
}

#endif /* __save_parse_h__ */
