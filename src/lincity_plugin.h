/* ---------------------------------------------------------------------- *
 * lincity_plugin.h
 * Native plugin ABI, version 1.
 * ---------------------------------------------------------------------- *
 * PROVISIONAL AND UNSTABLE.  Do not ship a plugin against this header yet.
 * The dynamic loader, descriptor checks, and restricted host accessors work
 * on Linux, and the Windows cross-build links.  Freeze v1 only after the
 * simulation-bearing fixture has passed on both targets.
 * ---------------------------------------------------------------------- */
#ifndef __lincity_plugin_h__
#define __lincity_plugin_h__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LC_PLUGIN_API_VERSION 1u
#define LC_PLUGIN_ABI_PROVISIONAL 1
#define LC_HOST_API_VERSION 1u

/* The only flag bits a plugin may write.  The host preserves every other
   bit, including base-game flags. */
#define LC_FLAG_MOD_MASK 0x70000000u

/* Existing per-cell fields exposed through lc_host. */
typedef enum {
    LC_FIELD_POPULATION = 1,
    LC_FIELD_COAL_RESERVE = 2,
    LC_FIELD_ORE_RESERVE = 3,
    LC_FIELD_INT_1 = 4,
    LC_FIELD_INT_2 = 5,
    LC_FIELD_INT_3 = 6,
    LC_FIELD_INT_4 = 7,
    LC_FIELD_INT_5 = 8,
    LC_FIELD_INT_6 = 9,
    LC_FIELD_INT_7 = 10
} lc_field_v1;

#if defined (_WIN32)
#  define LC_EXPORT __declspec(dllexport)
#else
#  define LC_EXPORT __attribute__((visibility("default")))
#endif

/* Host-owned handle.  Plugins access map data only through this table;
   raw game structs are not part of the ABI.  The host owns the table and
   its lifetime; plugins must not retain it after shutdown. */
typedef struct lc_host lc_host;
struct lc_host {
    uint32_t size;              /* sizeof (lc_host) */
    uint32_t api_version;       /* must be LC_HOST_API_VERSION */

    /* Reads are allowed on any valid map cell.  These return 1 on success. */
    int (*get_origin)(lc_host *h, int x, int y, int *origin_x, int *origin_y);
    int (*get_group)(lc_host *h, int x, int y, uint16_t *group);
    int (*get_type)(lc_host *h, int x, int y, uint16_t *type);
    int (*get_field)(lc_host *h, int x, int y, uint32_t field,
                     int32_t *value);
    int (*get_flags)(lc_host *h, int x, int y, uint32_t *flags);

    /* Writes are allowed only on the origin tile of a cell whose group the
       calling plugin registered.  They return 1 on success, 0 if denied. */
    int (*set_field)(lc_host *h, int x, int y, uint32_t field, int32_t value);
    int (*set_flags)(lc_host *h, int x, int y, uint32_t flags);
};

typedef struct {
    uint32_t size;              /* sizeof (lc_behavior_v1) */
    const char *id;             /* stable key, e.g. extra.wind_farm */
    void (*simulate)(lc_host *h, int x, int y);
    void (*mps)(lc_host *h, int x, int y);
    int  (*place)(lc_host *h, int x, int y, int type);
    void (*bulldoze)(lc_host *h, int x, int y);
} lc_behavior_v1;

typedef struct {
    uint32_t size;              /* sizeof (lc_group_desc_v1) */
    const char *key;            /* stable string key */
    const char *name;           /* display name */
    uint16_t group;             /* requested numeric ID, 0 = allocate */
    uint16_t size_sq;           /* footprint, 1..4 */
    uint32_t traits;            /* registry traits */
    int32_t colour;
    int32_t cost_mul;
    int32_t bul_cost;
    int32_t fire_chance;
    int32_t cost;
    int32_t tech;
    const char *behavior;       /* lc_behavior_v1 id, or NULL */
} lc_group_desc_v1;

typedef struct {
    uint32_t size;              /* sizeof (lc_type_desc_v1) */
    const char *key;            /* stable string key */
    uint16_t type;              /* requested numeric ID, 0 = allocate */
    uint16_t group;             /* owning group ID */
    const char *graphic;        /* icon key or path inside the mod */
} lc_type_desc_v1;

typedef struct {
    uint32_t api_version;       /* must be LC_PLUGIN_API_VERSION */
    uint32_t size;              /* sizeof (lc_plugin_v1) */
    const char *id;             /* stable plugin/mod key */
    const char *version;        /* plugin version string */
    int  (*init)(lc_host *h);
    void (*shutdown)(void);
    const lc_behavior_v1 *behaviors;
    uint32_t behavior_count;
    const lc_group_desc_v1 *groups;
    uint32_t group_count;
    const lc_type_desc_v1 *types;
    uint32_t type_count;
} lc_plugin_v1;

/* Every plugin exports exactly this entry point. */
LC_EXPORT const lc_plugin_v1 *lc_plugin_entry (void);

#ifdef __cplusplus
}
#endif

#endif /* __lincity_plugin_h__ */
