/* ---------------------------------------------------------------------- *
 * mod_registry.h
 * Runtime registry shared by the base game and data/native mods.
 * ---------------------------------------------------------------------- *
 * Base IDs stay frozen.  All runtime loops, bounds checks, saves and
 * network validation use num_groups/num_types rather than the fixed
 * MAX_GROUPS/MAX_TYPES capacity values.
 * ---------------------------------------------------------------------- */
#ifndef __mod_registry_h__
#define __mod_registry_h__

#include "lintypes.h"

/* Behavior traits.  These describe what a group does, so dispatch and
   validation never need to compare against base group IDs directly. */
#define GROUP_TRAIT_MARKET          (1u << 0)
#define GROUP_TRAIT_PORT            (1u << 1)
#define GROUP_TRAIT_POWER_ENTRY     (1u << 2)  /* substation or windmill */
#define GROUP_TRAIT_SUBSTATION      (1u << 3)
#define GROUP_TRAIT_WINDMILL        (1u << 4)
#define GROUP_TRAIT_POWER_SOURCE    (1u << 5)
#define GROUP_TRAIT_POWER_COAL      (1u << 6)
#define GROUP_TRAIT_POWER_SOLAR     (1u << 7)
#define GROUP_TRAIT_POWER_LINE      (1u << 8)
#define GROUP_TRAIT_POWER_GRID      (1u << 9)  /* anything on the power grid */
#define GROUP_TRAIT_WATER           (1u << 10)
#define GROUP_TRAIT_TRANSPORT       (1u << 11) /* GROUP_IS_TRANSPORT */
#define GROUP_TRAIT_TRACK           (1u << 12)
#define GROUP_TRAIT_ROAD            (1u << 13)
#define GROUP_TRAIT_RAIL            (1u << 14)
#define GROUP_TRAIT_RESIDENCE       (1u << 15)
#define GROUP_TRAIT_COMMUNE         (1u << 16)
#define GROUP_TRAIT_TIP             (1u << 17)
#define GROUP_TRAIT_OREMINE         (1u << 18)
#define GROUP_TRAIT_COALMINE        (1u << 19)
#define GROUP_TRAIT_SHANTY          (1u << 20)
#define GROUP_TRAIT_FIRE            (1u << 21)
#define GROUP_TRAIT_BARE            (1u << 22)
#define GROUP_TRAIT_ORGANIC_FARM    (1u << 23)
#define GROUP_TRAIT_RECYCLE         (1u << 24)
#define GROUP_TRAIT_FIRESTATION     (1u << 25)
#define GROUP_TRAIT_HEALTH          (1u << 26)
#define GROUP_TRAIT_CRICKET         (1u << 27)

static inline int
mod_group_has_trait (int group, unsigned int trait)
{
    return group >= 0 && group < num_groups
        && (main_groups[group].traits & trait) != 0;
}

/* 0 = track, 1 = road, 2 = rail, -1 = not a freight transport group. */
static inline int
mod_group_transport_kind (int group)
{
    if (mod_group_has_trait (group, GROUP_TRAIT_TRACK))
        return 0;
    if (mod_group_has_trait (group, GROUP_TRAIT_ROAD))
        return 1;
    if (mod_group_has_trait (group, GROUP_TRAIT_RAIL))
        return 2;
    return -1;
}

void mod_registry_reset (void);
int mod_register_group (int id, const struct GROUP *desc);
int mod_register_group_keyed (int id, const char *key, const char *owner,
                              const struct GROUP *desc);
int mod_set_group_behavior (int id, unsigned int traits,
                            void (*simulate)(int, int),
                            void (*mps)(int, int),
                            void (*update_tech)(int, int));
int mod_register_type (int id, int group, char *graphic);
int mod_register_type_keyed (int id, const char *key, const char *owner,
                             int group, char *graphic);

/* Look up a stable key.  Returns -1 when the key is not registered. */
int mod_find_group_by_key (const char *key);
int mod_find_type_by_key (const char *key);

/* First unused ID at or above the base range, or -1 when full.  Group 41
   is reserved for the legacy "EOF" sentinel and is never returned. */
int mod_alloc_group_id (void);
int mod_alloc_type_id (void);

/* Built-in behavior descriptions shared by base registration and data mods. */
struct MOD_BEHAVIOR
{
    const char *name;
    unsigned int traits;
    void (*simulate)(int x, int y);
    void (*mps)(int x, int y);
    void (*update_tech)(int x, int y);
};

const struct MOD_BEHAVIOR *mod_builtin_behavior_lookup (const char *name);

/* Snapshot/restore for atomic mod registration. */
struct MOD_REGISTRY_STATE
{
    struct GROUP groups[MAX_GROUPS];
    struct TYPE types[MAX_TYPES];
    int num_groups;
    int num_types;
};

void mod_registry_snapshot (struct MOD_REGISTRY_STATE *state);
void mod_registry_restore (const struct MOD_REGISTRY_STATE *state);

#endif /* __mod_registry_h__ */
