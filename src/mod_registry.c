/* ---------------------------------------------------------------------- *
 * mod_registry.c
 * Runtime group/type registry for the base game and mods.
 * ---------------------------------------------------------------------- */
#include <string.h>
#include "lcconfig.h"
#include "lintypes.h"
#include "mod_registry.h"

int num_groups;
int num_types;

void
mod_registry_reset (void)
{
    int i;

    memset (main_groups, 0, sizeof (struct GROUP) * MAX_GROUPS);
    for (i = 0; i < MAX_TYPES; i++) {
        main_types[i].group = -1;
        main_types[i].graphic = NULL;
    }
    num_groups = 0;
    num_types = 0;
}

int
mod_register_group (int id, const struct GROUP *desc)
{
    if (id < 0 || id >= MAX_GROUPS || desc == NULL)
        return 0;
    if (desc->size < 1 || desc->size > 4)
        return 0;

    main_groups[id] = *desc;
    main_groups[id].group = (unsigned short) id;
    main_groups[id].traits = 0;
    main_groups[id].simulate = NULL;
    main_groups[id].mps = NULL;
    main_groups[id].update_tech = NULL;
    if (id >= num_groups)
        num_groups = id + 1;
    return 1;
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
mod_register_type (int id, int group, char *graphic)
{
    if (id < 0 || id >= MAX_TYPES)
        return 0;
    if (group < 0 || group >= num_groups)
        return 0;

    main_types[id].group = group;
    main_types[id].graphic = graphic;
    if (id >= num_types)
        num_types = id + 1;
    return 1;
}
