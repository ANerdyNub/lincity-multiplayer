/* ---------------------------------------------------------------------- *
 * mod_loader.h
 * Data-mod discovery, manifest parsing, and atomic registration.
 * ---------------------------------------------------------------------- */
#ifndef __mod_loader_h__
#define __mod_loader_h__

#include <stddef.h>
#include "lintypes.h"
#include "mod_registry.h"

#define MOD_ID_MAX      64
#define MOD_NAME_MAX    128
#define MOD_VERSION_MAX 32
#define MOD_PATH_MAX    1024
#define MOD_LIST_MAX    128

/* A manifest as reported by --list-mods. */
struct MOD_LISTING
{
    char id[MOD_ID_MAX + 1];
    char name[MOD_NAME_MAX + 1];
    char version[MOD_VERSION_MAX + 1];
    int enabled;
};

typedef const struct MOD_BEHAVIOR *(*mod_behavior_lookup_fn)(const char *name);
typedef char *(*mod_graphic_load_fn)(const char *path, char *err,
                                     size_t err_size);

void mod_loader_init (void);
void mod_loader_shutdown (void);

/* Discover every mod under dir.  conf may be NULL to use dir/mods.conf.
   Returns a malloc'ed list; the caller frees it with
   mod_loader_free_listing(). */
int mod_loader_scan (const char *dir, const char *conf,
                     struct MOD_LISTING **mods, size_t *count,
                     char *err, size_t err_size);
void mod_loader_free_listing (struct MOD_LISTING *mods);

/* Load and atomically register every enabled mod.  lookup resolves
   built-in behavior names; load_graphic returns a malloc'ed 16x16 tile. */
int mod_loader_load (const char *dir, const char *conf,
                     mod_behavior_lookup_fn lookup,
                     mod_graphic_load_fn load_graphic,
                     char *err, size_t err_size);

/* Enabled mods in load order, for saves and the multiplayer handshake. */
size_t mod_loader_enabled_count (void);
const char *mod_loader_enabled_id (size_t index);
const char *mod_loader_enabled_version (size_t index);
int mod_loader_enabled_api (size_t index);

#endif /* __mod_loader_h__ */
