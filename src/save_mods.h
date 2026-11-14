/* ---------------------------------------------------------------------- *
 * save_mods.h
 * Versioned mod block at the top of a save file.
 * ---------------------------------------------------------------------- *
 * Saves persist type IDs.  A version-113-or-newer save carries a small
 * block that records the enabled mods and maps every saved type ID to its
 * stable registry key.  Loading remaps those IDs by key, so sparse mod type
 * IDs and a different mod load order do not change what is on the map.
 *
 * Older saves have no block and load as base-only.
 * ---------------------------------------------------------------------- */
#ifndef __save_mods_h__
#define __save_mods_h__

#include <stddef.h>
#include <stdio.h>

#include "lin-city.h"

/* Save-format version that introduced the mod block.  VERSION_INT must be
   at least this value. */
#define SAVE_MOD_HEADER_SAVE_VERSION 113

/* Version of the block's own syntax, written as "#lcm <version>". */
#define SAVE_MOD_HEADER_VERSION 1

/* Format the block for a new save into a malloc'ed NUL-terminated string.
   Returns NULL on allocation failure. */
char *save_mod_header_format (void);

/* Read and validate the block from a save whose version is at least
   SAVE_MOD_HEADER_SAVE_VERSION.  The mod list must match the enabled mods
   and every mapped key must be registered.  On failure the header state is
   reset, err explains why, and nothing outside this module changes. */
int save_mod_header_read (FILE *in, char *err, size_t err_size);

/* Remap saved type IDs in map->type by stable key.  Uses the mapping from
   the last successful save_mod_header_read().  The check pass runs before
   any cell is written, so a failure leaves the array untouched. */
int save_mod_remap_types (short type[][WORLD_SIDE_LEN], char *err,
                          size_t err_size);

/* Forget any mapping from a previous load.  Called for old saves and at the
   start of save_mod_header_read(). */
void save_mod_header_reset (void);

#endif /* __save_mods_h__ */
