/* ---------------------------------------------------------------------- *
 * mod_registry.c
 * Runtime group/type registry for the base game and mods.
 * ---------------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>
#include "lcconfig.h"
#include "lintypes.h"
#include "mod_registry.h"

int num_groups;
int num_types;

static void
copy_text (char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0)
        return;
    if (src == NULL)
        src = "";
    snprintf (dst, dst_size, "%s", src);
}

void
mod_registry_reset (void)
{
    int i;

    memset (main_groups, 0, sizeof (struct GROUP) * MAX_GROUPS);
    for (i = 0; i < MAX_TYPES; i++) {
        main_types[i].group = -1;
        main_types[i].graphic = NULL;
        main_types[i].key[0] = '\0';
        main_types[i].owner[0] = '\0';
    }
    num_groups = 0;
    num_types = 0;
}

int
mod_register_group_keyed (int id, const char *key, const char *owner,
                          const struct GROUP *desc)
{
    char auto_key[MOD_KEY_LEN + 1];

    if (id < 0 || id >= MAX_GROUPS || desc == NULL)
        return 0;
    if (desc->size < 1 || desc->size > 4)
        return 0;

    if (key == NULL || key[0] == '\0') {
        snprintf (auto_key, sizeof auto_key, "base.group.%d", id);
        key = auto_key;
    }
    if (strlen (key) > MOD_KEY_LEN)
        return 0;
    if (main_groups[id].key[0] != '\0')
        return 0;
    if (mod_find_group_by_key (key) >= 0)
        return 0;

    main_groups[id] = *desc;
    main_groups[id].group = (unsigned short) id;
    main_groups[id].traits = 0;
    main_groups[id].simulate = NULL;
    main_groups[id].mps = NULL;
    main_groups[id].update_tech = NULL;
    copy_text (main_groups[id].key, sizeof main_groups[id].key, key);
    copy_text (main_groups[id].owner, sizeof main_groups[id].owner,
               owner == NULL || owner[0] == '\0' ? "base" : owner);
    if (id >= num_groups)
        num_groups = id + 1;
    return 1;
}

int
mod_register_group (int id, const struct GROUP *desc)
{
    return mod_register_group_keyed (id, NULL, "base", desc);
}

int
mod_set_group_behavior (int id, unsigned int traits,
                        void (*simulate)(int, int),
                        void (*mps)(int, int),
                        void (*update_tech)(int, int))
{
    if (id < 0 || id >= num_groups)
        return 0;

    main_groups[id].traits = traits;
    main_groups[id].simulate = simulate;
    main_groups[id].mps = mps;
    main_groups[id].update_tech = update_tech;
    return 1;
}

int
mod_register_type_keyed (int id, const char *key, const char *owner,
                         int group, char *graphic)
{
    char auto_key[MOD_KEY_LEN + 1];

    if (id < 0 || id >= MAX_TYPES)
        return 0;
    if (group < 0 || group >= num_groups)
        return 0;

    if (key == NULL || key[0] == '\0') {
        snprintf (auto_key, sizeof auto_key, "base.type.%d", id);
        key = auto_key;
    }
    if (strlen (key) > MOD_KEY_LEN)
        return 0;
    if (main_types[id].group >= 0)
        return 0;
    if (mod_find_type_by_key (key) >= 0)
        return 0;

    main_types[id].group = group;
    main_types[id].graphic = graphic;
    copy_text (main_types[id].key, sizeof main_types[id].key, key);
    copy_text (main_types[id].owner, sizeof main_types[id].owner,
               owner == NULL || owner[0] == '\0' ? "base" : owner);
    if (id >= num_types)
        num_types = id + 1;
    return 1;
}

int
mod_register_type (int id, int group, char *graphic)
{
    return mod_register_type_keyed (id, NULL, "base", group, graphic);
}

int
mod_find_group_by_key (const char *key)
{
    int i;

    if (key == NULL || key[0] == '\0')
        return -1;
    for (i = 0; i < num_groups; i++) {
        if (main_groups[i].key[0] != '\0'
            && strcmp (main_groups[i].key, key) == 0)
            return i;
    }
    return -1;
}

int
mod_find_type_by_key (const char *key)
{
    int i;

    if (key == NULL || key[0] == '\0')
        return -1;
    for (i = 0; i < num_types; i++) {
        if (main_types[i].key[0] != '\0'
            && strcmp (main_types[i].key, key) == 0)
            return i;
    }
    return -1;
}

int
mod_alloc_group_id (void)
{
    int id;

    /* NUM_OF_GROUPS - 1 (41) is the legacy "EOF" sentinel; real mod
       groups start at NUM_OF_GROUPS (42). */
    for (id = NUM_OF_GROUPS; id < MAX_GROUPS; id++) {
        if (main_groups[id].size == 0)
            return id;
    }
    return -1;
}

int
mod_alloc_type_id (void)
{
    int id;

    for (id = NUM_OF_TYPES; id < MAX_TYPES; id++) {
        if (main_types[id].group < 0)
            return id;
    }
    return -1;
}

void
mod_registry_snapshot (struct MOD_REGISTRY_STATE *state)
{
    if (state == NULL)
        return;
    memcpy (state->groups, main_groups, sizeof state->groups);
    memcpy (state->types, main_types, sizeof state->types);
    state->num_groups = num_groups;
    state->num_types = num_types;
}

void
mod_registry_restore (const struct MOD_REGISTRY_STATE *state)
{
    if (state == NULL)
        return;
    memcpy (main_groups, state->groups, sizeof state->groups);
    memcpy (main_types, state->types, sizeof state->types);
    num_groups = state->num_groups;
    num_types = state->num_types;
}
