/* ---------------------------------------------------------------------- *
 * mod_loader.c
 * Discover data mods and optional native plugins, validate them without
 * touching the registry, then commit atomically.  Native plugins require an
 * explicit opt-in because they run arbitrary code; they are never unloaded
 * while a city is loaded.
 * ---------------------------------------------------------------------- */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "engglobs.h"
#include "lctypes.h"
#include "lincity_plugin.h"
#include "mod_ini.h"
#include "mod_loader.h"

#define GROUPS_PER_MOD 32
#define TYPES_PER_MOD  64
#define CONF_MAX       1024
#define PLUGIN_MAX_BEHAVIORS 8

#if defined(WIN32)
#define MOD_PLUGIN_KEY "plugin-windows-x86_64"
#else
#define MOD_PLUGIN_KEY "plugin-linux-x86_64"
#endif

struct GROUP_SPEC
{
    char key[MOD_KEY_LEN + 1];
    char name[MOD_NAME_MAX + 1];
    char behavior[MOD_KEY_LEN + 1];
    int group_id;       /* 0 means allocate automatically. */
    int type_id;        /* requested ID for the group's primary type */
    int size;
    int no_credit;
    int has_colour;
    int colour;
    int cost_mul;
    int bul_cost;
    int fire_chance;
    int cost;
    int tech;
    char category[GROUP_CATEGORY_LEN + 1];
    int order;
    int has_order;
    char button[MOD_PATH_MAX + 1];
    int has_button;
    char *button_data;
    char help[MOD_PATH_MAX + 1];
    int has_help;
    char help_path[MOD_PATH_MAX + 1];
    char graphic[MOD_PATH_MAX + 1];
    char *graphic_data;
    const struct MOD_BEHAVIOR *behavior_def;
};

struct TYPE_SPEC
{
    char key[MOD_KEY_LEN + 1];
    char name[TYPE_NAME_LEN + 1];
    char group_key[MOD_KEY_LEN + 1];
    int type_id;
    int owns_graphic;
    char graphic[MOD_PATH_MAX + 1];
    char *graphic_data;
};

struct MOD_SPEC
{
    char id[MOD_ID_MAX + 1];
    char name[MOD_NAME_MAX + 1];
    char version[MOD_VERSION_MAX + 1];
    int api;
    char dir[MOD_PATH_MAX + 1];
    int enabled;
    int order;
    int group_count;
    int type_count;
    struct GROUP_SPEC groups[GROUPS_PER_MOD];
    struct TYPE_SPEC types[TYPES_PER_MOD];
    char plugin_path[MOD_PATH_MAX + 1];
    int has_plugin;
};

struct CONF_ENTRY
{
    char id[MOD_ID_MAX + 1];
    int enabled;
};

static int loaded;
static char enabled_ids[MOD_LIST_MAX][MOD_ID_MAX + 1];
static char enabled_versions[MOD_LIST_MAX][MOD_VERSION_MAX + 1];
static int enabled_apis[MOD_LIST_MAX];
static size_t enabled_total;

#if defined(WIN32)
typedef HMODULE PLUGIN_HANDLE;
#else
typedef void *PLUGIN_HANDLE;
#endif

static int native_enabled;
static PLUGIN_HANDLE plugin_handles[MOD_LIST_MAX];
static const lc_plugin_v1 *plugin_descs[MOD_LIST_MAX];
static size_t plugin_handle_count;
static const lc_behavior_v1 *plugin_behaviors[PLUGIN_MAX_BEHAVIORS];
static lc_host *plugin_behavior_hosts[PLUGIN_MAX_BEHAVIORS];
static size_t plugin_behavior_count;

/* One host table per native plugin slot.  The public lc_host must be the
   first member so a callback can be tied back to its owning plugin. */
struct PLUGIN_HOST
{
    lc_host api;
    size_t slot;
};
static struct PLUGIN_HOST plugin_hosts[PLUGIN_MAX_BEHAVIORS];
static unsigned char plugin_group_owner[MAX_GROUPS];

static void
set_error (char *err, size_t err_size, const char *format, ...)
{
    va_list args;

    if (err == NULL || err_size == 0)
        return;
    va_start (args, format);
    vsnprintf (err, err_size, format, args);
    va_end (args);
}

static void
copy_text (char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0)
        return;
    snprintf (dst, dst_size, "%s", src == NULL ? "" : src);
}

static void
copy_lower (char *dst, size_t dst_size, const char *src)
{
    size_t i;

    if (dst_size == 0)
        return;
    if (src == NULL)
        src = "";
    for (i = 0; i + 1 < dst_size && src[i] != '\0'; i++)
        dst[i] = (char) tolower ((unsigned char) src[i]);
    dst[i] = '\0';
}

/* Build the registry's stable "<mod-id>.<local-key>" key without relying
   on snprintf's truncation behaviour. */
static int
build_key (char *out, size_t out_size, const char *owner, const char *key)
{
    size_t owner_len;
    size_t key_len;

    if (out == NULL || out_size == 0 || owner == NULL || key == NULL)
        return 0;
    owner_len = strlen (owner);
    key_len = strlen (key);
    if (owner_len + 1 + key_len >= out_size)
        return 0;
    memcpy (out, owner, owner_len);
    out[owner_len] = '.';
    memcpy (out + owner_len + 1, key, key_len + 1);
    return 1;
}

static int
token_ok (const char *text)
{
    size_t i;

    if (text == NULL || text[0] == '\0' || strlen (text) > MOD_ID_MAX)
        return 0;
    for (i = 0; text[i] != '\0'; i++) {
        unsigned char c = (unsigned char) text[i];
        if (!(isalnum (c) || c == '_' || c == '-' || c == '.'))
            return 0;
    }
    return 1;
}

static int
version_ok (const char *text)
{
    size_t i;

    if (text == NULL || text[0] == '\0'
        || strlen (text) > MOD_VERSION_MAX)
        return 0;
    for (i = 0; text[i] != '\0'; i++) {
        unsigned char c = (unsigned char) text[i];
        if (isspace (c) || iscntrl (c))
            return 0;
    }
    return 1;
}

static int
parse_int (const char *text, int min, int max, int *out)
{
    char *end;
    long value;

    if (text == NULL || text[0] == '\0')
        return 0;
    errno = 0;
    value = strtol (text, &end, 10);
    if (errno != 0 || end == text || *end != '\0'
        || value < min || value > max)
        return 0;
    *out = (int) value;
    return 1;
}

static int
parse_bool (const char *text, int *out)
{
    if (text == NULL)
        return 0;
    if (strcmp (text, "1") == 0 || strcmp (text, "yes") == 0
        || strcmp (text, "true") == 0 || strcmp (text, "on") == 0) {
        *out = 1;
        return 1;
    }
    if (strcmp (text, "0") == 0 || strcmp (text, "no") == 0
        || strcmp (text, "false") == 0 || strcmp (text, "off") == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

static int
file_exists (const char *path)
{
    struct stat st;

    return path != NULL && stat (path, &st) == 0 && S_ISREG (st.st_mode);
}

static int
join_relative (char *out, size_t out_size, const char *base,
               const char *relative)
{
    const char *p;
    size_t used;

    if (relative == NULL || relative[0] == '\0')
        return 0;
    if (relative[0] == '/' || relative[0] == '\\'
        || strchr (relative, ':') != NULL
        || strchr (relative, '\\') != NULL)
        return 0;

    p = relative;
    while (*p != '\0') {
        const char *slash = strchr (p, '/');
        size_t len = slash == NULL ? strlen (p) : (size_t) (slash - p);

        if (len == 0 || (len == 1 && p[0] == '.')
            || (len == 2 && p[0] == '.' && p[1] == '.'))
            return 0;
        p = slash == NULL ? p + len : slash + 1;
    }

    used = (size_t) snprintf (out, out_size, "%s/%s", base, relative);
    return used < out_size;
}

static int
suffix_key (const char *section, const char *prefix,
            char *out, size_t out_size)
{
    size_t len = strlen (prefix);
    const char *value;

    if (strncmp (section, prefix, len) != 0)
        return 0;
    value = section + len;
    if (!token_ok (value))
        return 0;
    copy_lower (out, out_size, value);
    return 1;
}

static int
entry_seen_before (const struct MOD_INI *ini, size_t index,
                   const char *section)
{
    size_t i;

    for (i = 0; i < index; i++) {
        if (strcmp (ini->entries[i].section, section) == 0)
            return 1;
    }
    return 0;
}

static int
find_group_spec (const struct MOD_SPEC *mod, const char *key)
{
    int i;

    for (i = 0; i < mod->group_count; i++) {
        if (strcmp (mod->groups[i].key, key) == 0)
            return i;
    }
    return -1;
}

static int
find_type_spec (const struct MOD_SPEC *mod, const char *key)
{
    int i;

    for (i = 0; i < mod->type_count; i++) {
        if (strcmp (mod->types[i].key, key) == 0)
            return i;
    }
    return -1;
}

static int
group_key_known (const char *key)
{
    static const char *const keys[] = {
        "name", "behavior", "size", "no_credit", "colour", "cost_mul",
        "bul_cost", "fire_chance", "cost", "tech", "type", "graphic",
        "category", "order", "button", "help"
    };
    size_t i;

    for (i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        if (strcmp (key, keys[i]) == 0)
            return 1;
    }
    return 0;
}

static int
type_key_known (const char *key)
{
    static const char *const keys[] = {
        "name", "group", "graphic", "type"
    };
    size_t i;

    for (i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        if (strcmp (key, keys[i]) == 0)
            return 1;
    }
    return 0;
}

static int
parse_group_section (struct MOD_SPEC *mod, const struct MOD_INI *ini,
                     const char *section, char *err, size_t err_size)
{
    struct GROUP_SPEC group;
    struct TYPE_SPEC *type;
    char suffix[MOD_KEY_LEN + 1];
    const char *name;
    const char *behavior;
    const char *value;
    size_t i;

    if (!suffix_key (section, "group:", suffix, sizeof suffix)) {
        set_error (err, err_size, "mod %s: invalid group section '%s'",
                   mod->id, section);
        return 0;
    }
    if (find_group_spec (mod, suffix) >= 0) {
        set_error (err, err_size, "mod %s: duplicate group '%s'",
                   mod->id, suffix);
        return 0;
    }
    if (mod->group_count >= GROUPS_PER_MOD) {
        set_error (err, err_size, "mod %s: too many groups (max %d)",
                   mod->id, GROUPS_PER_MOD);
        return 0;
    }

    for (i = 0; i < ini->count; i++) {
        if (strcmp (ini->entries[i].section, section) == 0
            && !group_key_known (ini->entries[i].key)) {
            set_error (err, err_size,
                       "mod %s: unknown group key '%s'",
                       mod->id, ini->entries[i].key);
            return 0;
        }
    }

    memset (&group, 0, sizeof group);
    copy_text (group.key, sizeof group.key, suffix);
    copy_text (group.category, sizeof group.category, "Mods");
    name = mod_ini_get (ini, section, "name");
    behavior = mod_ini_get (ini, section, "behavior");
    value = mod_ini_get (ini, section, "graphic");
    if (name == NULL || name[0] == '\0') {
        set_error (err, err_size, "mod %s: group '%s' has no name",
                   mod->id, suffix);
        return 0;
    }
    if (behavior == NULL || behavior[0] == '\0') {
        set_error (err, err_size, "mod %s: group '%s' has no behavior",
                   mod->id, suffix);
        return 0;
    }
    if (value == NULL || value[0] == '\0') {
        set_error (err, err_size, "mod %s: group '%s' has no graphic",
                   mod->id, suffix);
        return 0;
    }
    copy_text (group.name, sizeof group.name, name);
    copy_lower (group.behavior, sizeof group.behavior, behavior);
    copy_text (group.graphic, sizeof group.graphic, value);

    if ((value = mod_ini_get (ini, section, "category")) != NULL) {
        if (value[0] == '\0' || strlen (value) > GROUP_CATEGORY_LEN) {
            set_error (err, err_size,
                       "mod %s: group '%s' has invalid category",
                       mod->id, suffix);
            return 0;
        }
        copy_text (group.category, sizeof group.category, value);
    }
    if ((value = mod_ini_get (ini, section, "order")) != NULL) {
        if (!parse_int (value, -1000000, 1000000, &group.order)) {
            set_error (err, err_size, "mod %s: group '%s' has invalid order",
                       mod->id, suffix);
            return 0;
        }
        group.has_order = 1;
    }
    if ((value = mod_ini_get (ini, section, "button")) != NULL) {
        if (value[0] == '\0' || strlen (value) > MOD_PATH_MAX) {
            set_error (err, err_size,
                       "mod %s: group '%s' has invalid button",
                       mod->id, suffix);
            return 0;
        }
        copy_text (group.button, sizeof group.button, value);
        group.has_button = 1;
    }
    if ((value = mod_ini_get (ini, section, "help")) != NULL) {
        if (value[0] == '\0' || strlen (value) > MOD_PATH_MAX) {
            set_error (err, err_size,
                       "mod %s: group '%s' has invalid help",
                       mod->id, suffix);
            return 0;
        }
        copy_text (group.help, sizeof group.help, value);
        group.has_help = 1;
    }

    group.size = 1;
    group.cost_mul = 1;
    if ((value = mod_ini_get (ini, section, "size")) != NULL
        && !parse_int (value, 1, 4, &group.size)) {
        set_error (err, err_size, "mod %s: group '%s' has invalid size",
                   mod->id, suffix);
        return 0;
    }
    if ((value = mod_ini_get (ini, section, "cost")) != NULL
        && !parse_int (value, 0, 1000000000, &group.cost)) {
        set_error (err, err_size, "mod %s: group '%s' has invalid cost",
                   mod->id, suffix);
        return 0;
    }
    if ((value = mod_ini_get (ini, section, "cost_mul")) != NULL
        && !parse_int (value, 0, 1000000, &group.cost_mul)) {
        set_error (err, err_size, "mod %s: group '%s' has invalid cost_mul",
                   mod->id, suffix);
        return 0;
    }
    if ((value = mod_ini_get (ini, section, "bul_cost")) != NULL
        && !parse_int (value, 0, 1000000000, &group.bul_cost)) {
        set_error (err, err_size, "mod %s: group '%s' has invalid bul_cost",
                   mod->id, suffix);
        return 0;
    }
    if ((value = mod_ini_get (ini, section, "fire_chance")) != NULL
        && !parse_int (value, 0, 1000000, &group.fire_chance)) {
        set_error (err, err_size,
                   "mod %s: group '%s' has invalid fire_chance",
                   mod->id, suffix);
        return 0;
    }
    if ((value = mod_ini_get (ini, section, "tech")) != NULL
        && !parse_int (value, 0, 1000000, &group.tech)) {
        set_error (err, err_size, "mod %s: group '%s' has invalid tech",
                   mod->id, suffix);
        return 0;
    }
    if ((value = mod_ini_get (ini, section, "no_credit")) != NULL
        && !parse_bool (value, &group.no_credit)) {
        set_error (err, err_size,
                   "mod %s: group '%s' has invalid no_credit",
                   mod->id, suffix);
        return 0;
    }
    if ((value = mod_ini_get (ini, section, "colour")) != NULL) {
        if (!parse_int (value, 0, 255, &group.colour)) {
            set_error (err, err_size,
                       "mod %s: group '%s' has invalid colour",
                       mod->id, suffix);
            return 0;
        }
        group.has_colour = 1;
    }
    if ((value = mod_ini_get (ini, section, "type")) != NULL
        && !parse_int (value, 0, MAX_TYPES - 1, &group.type_id)) {
        set_error (err, err_size, "mod %s: group '%s' has invalid type id",
                   mod->id, suffix);
        return 0;
    }

    mod->groups[mod->group_count++] = group;

    if (mod->type_count >= TYPES_PER_MOD) {
        set_error (err, err_size, "mod %s: too many types (max %d)",
                   mod->id, TYPES_PER_MOD);
        return 0;
    }
    type = &mod->types[mod->type_count++];
    memset (type, 0, sizeof *type);
    copy_text (type->key, sizeof type->key, suffix);
    copy_text (type->group_key, sizeof type->group_key, suffix);
    type->type_id = group.type_id;
    /* The primary type shares the group's graphic; it must not own or
       reload a second copy. */
    type->owns_graphic = 0;
    return 1;
}

static int
parse_type_section (struct MOD_SPEC *mod, const struct MOD_INI *ini,
                    const char *section, char *err, size_t err_size)
{
    struct TYPE_SPEC type;
    char suffix[MOD_KEY_LEN + 1];
    const char *value;
    size_t i;

    if (!suffix_key (section, "type:", suffix, sizeof suffix)) {
        set_error (err, err_size, "mod %s: invalid type section '%s'",
                   mod->id, section);
        return 0;
    }
    if (find_type_spec (mod, suffix) >= 0) {
        set_error (err, err_size, "mod %s: duplicate type '%s'",
                   mod->id, suffix);
        return 0;
    }
    if (mod->type_count >= TYPES_PER_MOD) {
        set_error (err, err_size, "mod %s: too many types (max %d)",
                   mod->id, TYPES_PER_MOD);
        return 0;
    }

    for (i = 0; i < ini->count; i++) {
        if (strcmp (ini->entries[i].section, section) == 0
            && !type_key_known (ini->entries[i].key)) {
            set_error (err, err_size,
                       "mod %s: unknown type key '%s'",
                       mod->id, ini->entries[i].key);
            return 0;
        }
    }

    memset (&type, 0, sizeof type);
    copy_text (type.key, sizeof type.key, suffix);
    value = mod_ini_get (ini, section, "name");
    if (value != NULL) {
        if (value[0] == '\0' || strlen (value) > TYPE_NAME_LEN) {
            set_error (err, err_size,
                       "mod %s: type '%s' has invalid name",
                       mod->id, suffix);
            return 0;
        }
        copy_text (type.name, sizeof type.name, value);
    }
    value = mod_ini_get (ini, section, "group");
    if (value == NULL || value[0] == '\0') {
        set_error (err, err_size, "mod %s: type '%s' has no group",
                   mod->id, suffix);
        return 0;
    }
    copy_lower (type.group_key, sizeof type.group_key, value);
    value = mod_ini_get (ini, section, "graphic");
    if (value != NULL && value[0] != '\0') {
        copy_text (type.graphic, sizeof type.graphic, value);
        type.owns_graphic = 1;
    }
    value = mod_ini_get (ini, section, "type");
    if (value != NULL
        && !parse_int (value, 0, MAX_TYPES - 1, &type.type_id)) {
        set_error (err, err_size, "mod %s: type '%s' has invalid type id",
                   mod->id, suffix);
        return 0;
    }
    mod->types[mod->type_count++] = type;
    return 1;
}

static int
parse_manifest (const char *path, const char *dir, struct MOD_SPEC *mod,
                char *err, size_t err_size)
{
    struct MOD_INI ini;
    const char *value;
    size_t i;
    int api;

    mod_ini_init (&ini);
    if (!mod_ini_parse (path, &ini, err, err_size)) {
        mod_ini_free (&ini);
        return 0;
    }
    if (!mod_ini_has_section (&ini, "mod")) {
        set_error (err, err_size, "%s: missing [mod] section", path);
        mod_ini_free (&ini);
        return 0;
    }

    memset (mod, 0, sizeof *mod);
    copy_text (mod->dir, sizeof mod->dir, dir);

    value = mod_ini_get (&ini, "mod", "id");
    if (value == NULL || !token_ok (value)) {
        set_error (err, err_size, "%s: invalid or missing mod id", path);
        mod_ini_free (&ini);
        return 0;
    }
    copy_lower (mod->id, sizeof mod->id, value);
    value = mod_ini_get (&ini, "mod", "name");
    if (value == NULL || value[0] == '\0'
        || strlen (value) > MOD_NAME_MAX) {
        set_error (err, err_size, "%s: invalid or missing mod name", path);
        mod_ini_free (&ini);
        return 0;
    }
    copy_text (mod->name, sizeof mod->name, value);
    value = mod_ini_get (&ini, "mod", "version");
    if (!version_ok (value)) {
        set_error (err, err_size, "%s: invalid or missing mod version",
                   path);
        mod_ini_free (&ini);
        return 0;
    }
    copy_text (mod->version, sizeof mod->version, value);
    value = mod_ini_get (&ini, "mod", "api");
    if (value == NULL || !parse_int (value, 1, 1000, &api) || api != 1) {
        set_error (err, err_size, "%s: mod api must be 1", path);
        mod_ini_free (&ini);
        return 0;
    }
    mod->api = api;

    for (i = 0; i < ini.count; i++) {
        const char *key = ini.entries[i].key;
        const char *section = ini.entries[i].section;

        if (strcmp (section, "mod") == 0) {
            static const char *const allowed[] = {
                "id", "name", "version", "api", "description"
            };
            size_t k;
            int known = 0;

            for (k = 0; k < sizeof allowed / sizeof allowed[0]; k++) {
                if (strcmp (key, allowed[k]) == 0) {
                    known = 1;
                    break;
                }
            }
            if (!known && strncmp (key, "plugin-", 7) == 0) {
                if (strcmp (key, MOD_PLUGIN_KEY) == 0) {
                    value = mod_ini_get (&ini, section, key);
                    if (value == NULL || value[0] == '\0'
                        || strlen (value) > MOD_PATH_MAX) {
                        set_error (err, err_size,
                                   "%s: invalid native plugin path", path);
                        mod_ini_free (&ini);
                        return 0;
                    }
                    copy_text (mod->plugin_path,
                               sizeof mod->plugin_path, value);
                    mod->has_plugin = 1;
                }
                known = 1;
            }
            if (!known) {
                set_error (err, err_size, "%s: unknown mod key '%s'",
                           path, key);
                mod_ini_free (&ini);
                return 0;
            }
        } else if (strncmp (section, "group:", 6) == 0) {
            if (entry_seen_before (&ini, i, section))
                continue;
            if (!parse_group_section (mod, &ini, section, err, err_size)) {
                mod_ini_free (&ini);
                return 0;
            }
        } else if (strncmp (section, "type:", 5) == 0) {
            if (entry_seen_before (&ini, i, section))
                continue;
            if (!parse_type_section (mod, &ini, section, err, err_size)) {
                mod_ini_free (&ini);
                return 0;
            }
        } else {
            set_error (err, err_size, "%s: unknown section '[%s]'",
                       path, section);
            mod_ini_free (&ini);
            return 0;
        }
    }

    mod_ini_free (&ini);

    for (i = 0; i < (size_t) mod->type_count; i++) {
        if (find_group_spec (mod, mod->types[i].group_key) < 0) {
            set_error (err, err_size,
                       "mod %s: type '%s' references unknown group '%s'",
                       mod->id, mod->types[i].key,
                       mod->types[i].group_key);
            return 0;
        }
    }
    if (mod->group_count == 0 && !mod->has_plugin) {
        set_error (err, err_size, "mod %s: no groups are defined", mod->id);
        return 0;
    }
    return 1;
}

static int
read_config (const char *path, struct CONF_ENTRY *entries, int *count,
             char *err, size_t err_size)
{
    FILE *fp;
    char buffer[CONF_MAX];
    int line = 0;

    fp = fopen (path, "r");
    if (fp == NULL) {
        set_error (err, err_size, "cannot open mods config %s", path);
        return 0;
    }

    *count = 0;
    while (fgets (buffer, sizeof buffer, fp) != NULL) {
        char *text;
        char *space;
        int enabled = 1;
        int i;

        line++;
        text = buffer;
        while (*text != '\0' && isspace ((unsigned char) *text))
            text++;
        {
            char *end = text + strlen (text);
            while (end > text && isspace ((unsigned char) end[-1]))
                end--;
            *end = '\0';
        }
        if (*text == '\0' || *text == '#' || *text == ';')
            continue;

        space = strchr (text, '=');
        if (space != NULL) {
            char *value;
            char *end;

            *space = '\0';
            value = space + 1;
            end = text + strlen (text);
            while (end > text && isspace ((unsigned char) end[-1]))
                end--;
            *end = '\0';
            while (*value != '\0' && isspace ((unsigned char) *value))
                value++;
            if (!parse_bool (value, &enabled)) {
                set_error (err, err_size,
                           "%s:%d: expected enabled/disabled value",
                           path, line);
                fclose (fp);
                return 0;
            }
        } else {
            space = text;
            while (*space != '\0' && !isspace ((unsigned char) *space))
                space++;
            if (*space != '\0') {
                char *command = text;
                char *value = space;

                *space = '\0';
                value++;
                while (*value != '\0' && isspace ((unsigned char) *value))
                    value++;
                if (strcmp (command, "enable") == 0) {
                    text = value;
                    enabled = 1;
                } else if (strcmp (command, "disable") == 0) {
                    text = value;
                    enabled = 0;
                } else {
                    set_error (err, err_size,
                               "%s:%d: unknown directive '%s'",
                               path, line, command);
                    fclose (fp);
                    return 0;
                }
            }
        }

        if (!token_ok (text)) {
            set_error (err, err_size, "%s:%d: invalid mod id", path, line);
            fclose (fp);
            return 0;
        }
        for (i = 0; i < *count; i++) {
            if (strcmp (entries[i].id, text) == 0) {
                set_error (err, err_size, "%s:%d: duplicate mod '%s'",
                           path, line, text);
                fclose (fp);
                return 0;
            }
        }
        if (*count >= MOD_LIST_MAX) {
            set_error (err, err_size, "%s: too many mod entries", path);
            fclose (fp);
            return 0;
        }
        copy_lower (entries[*count].id, sizeof entries[*count].id, text);
        entries[*count].enabled = enabled;
        (*count)++;
    }

    if (ferror (fp)) {
        set_error (err, err_size, "%s: read error", path);
        fclose (fp);
        return 0;
    }
    fclose (fp);
    return 1;
}

static int
discover_mods (const char *dir, struct MOD_SPEC **out, int *out_count,
               char *err, size_t err_size)
{
    DIR *directory;
    struct dirent *entry;
    char **names = NULL;
    size_t name_count = 0;
    size_t name_capacity = 0;
    struct MOD_SPEC *mods;
    int count = 0;
    size_t i;

    *out = NULL;
    *out_count = 0;
    directory = opendir (dir);
    if (directory == NULL) {
        set_error (err, err_size, "cannot open mod directory %s", dir);
        return 0;
    }

    while ((entry = readdir (directory)) != NULL) {
        char path[MOD_PATH_MAX + 1];
        struct stat st;

        if (entry->d_name[0] == '.')
            continue;
        if (snprintf (path, sizeof path, "%s/%s", dir, entry->d_name)
            >= (int) sizeof path)
            continue;
        if (stat (path, &st) != 0 || !S_ISDIR (st.st_mode))
            continue;

        if (name_count == name_capacity) {
            size_t capacity = name_capacity == 0 ? 16 : name_capacity * 2;
            char **grown = realloc (names, capacity * sizeof *grown);
            if (grown == NULL) {
                set_error (err, err_size, "out of memory");
                goto fail;
            }
            names = grown;
            name_capacity = capacity;
        }
        names[name_count] = malloc (strlen (entry->d_name) + 1);
        if (names[name_count] == NULL) {
            set_error (err, err_size, "out of memory");
            goto fail;
        }
        strcpy (names[name_count], entry->d_name);
        name_count++;
    }
    closedir (directory);

    for (i = 0; i + 1 < name_count; i++) {
        size_t j;
        for (j = i + 1; j < name_count; j++) {
            if (strcmp (names[i], names[j]) > 0) {
                char *swap = names[i];
                names[i] = names[j];
                names[j] = swap;
            }
        }
    }

    mods = calloc (MOD_LIST_MAX, sizeof *mods);
    if (mods == NULL) {
        set_error (err, err_size, "out of memory");
        goto fail;
    }

    for (i = 0; i < name_count; i++) {
        char ini_path[MOD_PATH_MAX + 1];
        char mod_dir[MOD_PATH_MAX + 1];
        struct stat st;
        int j;

        if (snprintf (mod_dir, sizeof mod_dir, "%s/%s", dir, names[i])
            >= (int) sizeof mod_dir
            || snprintf (ini_path, sizeof ini_path, "%s/mod.ini", mod_dir)
               >= (int) sizeof ini_path) {
            set_error (err, err_size, "mod path is too long: %s", names[i]);
            free (mods);
            goto fail;
        }
        if (stat (mod_dir, &st) != 0)
            continue;
        if (!file_exists (ini_path))
            continue;
        if (count >= MOD_LIST_MAX) {
            set_error (err, err_size, "too many mods (max %d)", MOD_LIST_MAX);
            free (mods);
            goto fail;
        }
        if (!parse_manifest (ini_path, mod_dir, &mods[count],
                             err, err_size)) {
            free (mods);
            goto fail;
        }
        for (j = 0; j < count; j++) {
            if (strcmp (mods[j].id, mods[count].id) == 0) {
                set_error (err, err_size, "duplicate mod id '%s'",
                           mods[count].id);
                free (mods);
                goto fail;
            }
        }
        count++;
    }

    for (i = 0; i < name_count; i++)
        free (names[i]);
    free (names);
    *out = mods;
    *out_count = count;
    return 1;

fail:
    for (i = 0; i < name_count; i++)
        free (names[i]);
    free (names);
    return 0;
}

static int
apply_config (struct MOD_SPEC *mods, int count, const struct CONF_ENTRY *conf,
              int conf_count, int have_config, char *err, size_t err_size)
{
    int i;

    for (i = 0; i < count; i++) {
        mods[i].enabled = !have_config;
        mods[i].order = have_config ? -1 : i;
    }
    if (!have_config)
        return 1;

    for (i = 0; i < conf_count; i++) {
        int m;
        int found = 0;

        for (m = 0; m < count; m++) {
            if (strcmp (mods[m].id, conf[i].id) == 0) {
                found = 1;
                if (conf[i].enabled) {
                    mods[m].enabled = 1;
                    mods[m].order = i;
                } else {
                    mods[m].enabled = 0;
                    mods[m].order = -1;
                }
                break;
            }
        }
        if (!found) {
            set_error (err, err_size,
                       "mods config refers to unknown mod '%s'",
                       conf[i].id);
            return 0;
        }
    }
    return 1;
}

static int
build_load_order (struct MOD_SPEC *mods, int count, int have_config,
                  const struct CONF_ENTRY *conf, int conf_count,
                  int *load_order, int *enabled_count,
                  char *err, size_t err_size)
{
    int i;
    int order;
    int total = 0;

    if (!have_config) {
        for (i = 0; i < count; i++) {
            if (mods[i].enabled)
                load_order[total++] = i;
        }
        *enabled_count = total;
        return 1;
    }

    for (order = 0; order < conf_count; order++) {
        int found = 0;

        if (!conf[order].enabled)
            continue;
        for (i = 0; i < count; i++) {
            if (mods[i].enabled && mods[i].order == order) {
                load_order[total++] = i;
                found = 1;
                break;
            }
        }
        if (!found) {
            set_error (err, err_size,
                       "mods config order is inconsistent at entry %d", order);
            return 0;
        }
    }

    for (i = 0; i < count; i++) {
        if (mods[i].enabled && mods[i].order < 0) {
            set_error (err, err_size,
                       "mod %s has no load order", mods[i].id);
            return 0;
        }
    }
    *enabled_count = total;
    return 1;
}

int
mod_loader_scan (const char *dir, const char *conf,
                 struct MOD_LISTING **mods_out, size_t *count_out,
                 char *err, size_t err_size)
{
    struct MOD_SPEC *mods = NULL;
    struct CONF_ENTRY conf_entries[MOD_LIST_MAX];
    struct MOD_LISTING *listing = NULL;
    char default_conf[MOD_PATH_MAX + 1];
    const char *conf_path = conf;
    int conf_count = 0;
    int have_config = 0;
    int count = 0;
    int i;

    *mods_out = NULL;
    *count_out = 0;

    if (dir == NULL || dir[0] == '\0')
        return 1;
    if (!discover_mods (dir, &mods, &count, err, err_size))
        return 0;

    if (conf_path == NULL) {
        if (snprintf (default_conf, sizeof default_conf, "%s/mods.conf", dir)
            < (int) sizeof default_conf
            && file_exists (default_conf)) {
            conf_path = default_conf;
            have_config = 1;
        }
    } else {
        have_config = 1;
    }

    if (have_config
        && !read_config (conf_path, conf_entries, &conf_count,
                         err, err_size)) {
        free (mods);
        return 0;
    }
    if (!apply_config (mods, count, conf_entries, conf_count,
                       have_config, err, err_size)) {
        free (mods);
        return 0;
    }

    listing = calloc ((size_t) count, sizeof *listing);
    if (listing == NULL && count > 0) {
        set_error (err, err_size, "out of memory");
        free (mods);
        return 0;
    }
    for (i = 0; i < count; i++) {
        copy_text (listing[i].id, sizeof listing[i].id, mods[i].id);
        copy_text (listing[i].name, sizeof listing[i].name, mods[i].name);
        copy_text (listing[i].version, sizeof listing[i].version,
                   mods[i].version);
        listing[i].enabled = mods[i].enabled;
    }
    free (mods);
    *mods_out = listing;
    *count_out = (size_t) count;
    return 1;
}

void
mod_loader_free_listing (struct MOD_LISTING *mods)
{
    free (mods);
}

static void
free_specs (struct MOD_SPEC *mods, int count, int free_graphics)
{
    int i;
    int j;

    if (mods == NULL)
        return;
    if (free_graphics) {
        for (i = 0; i < count; i++) {
            for (j = 0; j < mods[i].group_count; j++) {
                if (mods[i].groups[j].has_button)
                    free (mods[i].groups[j].button_data);
                free (mods[i].groups[j].graphic_data);
            }
            for (j = 0; j < mods[i].type_count; j++) {
                if (mods[i].types[j].owns_graphic)
                    free (mods[i].types[j].graphic_data);
            }
        }
    }
    free (mods);
}

static int
preview_ids (struct MOD_SPEC *mods, const int *load_order, int enabled_count,
             char *err, size_t err_size)
{
    unsigned char used_groups[MAX_GROUPS];
    unsigned char used_types[MAX_TYPES];
    int i;
    int j;

    memset (used_groups, 0, sizeof used_groups);
    memset (used_types, 0, sizeof used_types);
    for (i = 0; i < num_groups; i++)
        used_groups[i] = 1;
    /* Group NUM_OF_GROUPS - 1 (41) is the legacy "EOF" sentinel and is
       never placeable; mod IDs start at NUM_OF_GROUPS (42). */
    used_groups[NUM_OF_GROUPS - 1] = 1;
    for (i = 0; i < num_types; i++)
        used_types[i] = 1;

    /* Reserve every explicit ID before handing out any automatic one.  A
       later mod that asks for the ID an earlier mod would otherwise have
       taken must load in either order. */
    for (i = 0; i < enabled_count; i++) {
        struct MOD_SPEC *mod = &mods[load_order[i]];
        for (j = 0; j < mod->group_count; j++) {
            struct GROUP_SPEC *group = &mod->groups[j];
            int id = group->group_id;

            if (id == 0)
                continue;
            if (id < NUM_OF_GROUPS || id >= MAX_GROUPS || used_groups[id]) {
                set_error (err, err_size,
                           "mod %s: group '%s' uses reserved or duplicate ID %d",
                           mod->id, group->key, id);
                return 0;
            }
            used_groups[id] = 1;
        }
        for (j = 0; j < mod->type_count; j++) {
            struct TYPE_SPEC *type = &mod->types[j];
            int id = type->type_id;

            if (id == 0)
                continue;
            if (id < NUM_OF_TYPES || id >= MAX_TYPES || used_types[id]) {
                set_error (err, err_size,
                           "mod %s: type '%s' uses reserved or duplicate ID %d",
                           mod->id, type->key, id);
                return 0;
            }
            used_types[id] = 1;
        }
    }

    for (i = 0; i < enabled_count; i++) {
        struct MOD_SPEC *mod = &mods[load_order[i]];
        for (j = 0; j < mod->group_count; j++) {
            struct GROUP_SPEC *group = &mod->groups[j];
            int id = group->group_id;

            if (id != 0)
                continue;
            for (id = NUM_OF_GROUPS; id < MAX_GROUPS; id++) {
                if (!used_groups[id])
                    break;
            }
            if (id >= MAX_GROUPS) {
                set_error (err, err_size,
                           "mod %s: no free group IDs", mod->id);
                return 0;
            }
            used_groups[id] = 1;
            group->group_id = id;
        }
        for (j = 0; j < mod->type_count; j++) {
            struct TYPE_SPEC *type = &mod->types[j];
            int id = type->type_id;

            if (id != 0)
                continue;
            for (id = NUM_OF_TYPES; id < MAX_TYPES; id++) {
                if (!used_types[id])
                    break;
            }
            if (id >= MAX_TYPES) {
                set_error (err, err_size,
                           "mod %s: no free type IDs", mod->id);
                return 0;
            }
            used_types[id] = 1;
            type->type_id = id;
        }
    }
    return 1;
}

static int
load_graphics (struct MOD_SPEC *mods, const int *load_order, int enabled_count,
               mod_graphic_load_fn load_graphic,
               char *err, size_t err_size)
{
    int i;
    int j;

    for (i = 0; i < enabled_count; i++) {
        struct MOD_SPEC *mod = &mods[load_order[i]];
        for (j = 0; j < mod->group_count; j++) {
            struct GROUP_SPEC *group = &mod->groups[j];
            char path[MOD_PATH_MAX + 1];
            char graphic_err[256];

            if (!join_relative (path, sizeof path, mod->dir, group->graphic)) {
                set_error (err, err_size,
                           "mod %s: group '%s' has an unsafe graphic path",
                           mod->id, group->key);
                return 0;
            }
            if (!file_exists (path)) {
                set_error (err, err_size,
                           "mod %s: group '%s' graphic is missing: %s",
                           mod->id, group->key, group->graphic);
                return 0;
            }
            graphic_err[0] = '\0';
            group->graphic_data = load_graphic (path, graphic_err,
                                                sizeof graphic_err);
            if (group->graphic_data == NULL) {
                set_error (err, err_size,
                           "mod %s: group '%s' graphic failed: %s",
                           mod->id, group->key,
                           graphic_err[0] != '\0' ? graphic_err
                                                  : group->graphic);
                return 0;
            }

            if (!group->has_button) {
                group->button_data = group->graphic_data;
            } else {
                if (!join_relative (path, sizeof path, mod->dir, group->button)) {
                    set_error (err, err_size,
                               "mod %s: group '%s' has an unsafe button path",
                               mod->id, group->key);
                    return 0;
                }
                if (!file_exists (path)) {
                    set_error (err, err_size,
                               "mod %s: group '%s' button is missing: %s",
                               mod->id, group->key, group->button);
                    return 0;
                }
                graphic_err[0] = '\0';
                group->button_data = load_graphic (path, graphic_err,
                                                   sizeof graphic_err);
                if (group->button_data == NULL) {
                    set_error (err, err_size,
                               "mod %s: group '%s' button failed: %s",
                               mod->id, group->key,
                               graphic_err[0] != '\0' ? graphic_err
                                                      : group->button);
                    return 0;
                }
            }

            if (group->has_help) {
                if (!join_relative (group->help_path,
                                    sizeof group->help_path, mod->dir,
                                    group->help)) {
                    set_error (err, err_size,
                               "mod %s: group '%s' has an unsafe help path",
                               mod->id, group->key);
                    return 0;
                }
                if (!file_exists (group->help_path)) {
                    set_error (err, err_size,
                               "mod %s: group '%s' help page is missing: %s",
                               mod->id, group->key, group->help);
                    return 0;
                }
            }
        }
        for (j = 0; j < mod->type_count; j++) {
            struct TYPE_SPEC *type = &mod->types[j];
            struct GROUP_SPEC *group;
            char path[MOD_PATH_MAX + 1];
            char graphic_err[256];
            int group_index = find_group_spec (mod, type->group_key);

            if (!type->owns_graphic) {
                group = &mod->groups[group_index];
                type->graphic_data = group->graphic_data;
                continue;
            }
            if (!join_relative (path, sizeof path, mod->dir, type->graphic)) {
                set_error (err, err_size,
                           "mod %s: type '%s' has an unsafe graphic path",
                           mod->id, type->key);
                return 0;
            }
            if (!file_exists (path)) {
                set_error (err, err_size,
                           "mod %s: type '%s' graphic is missing: %s",
                           mod->id, type->key, type->graphic);
                return 0;
            }
            graphic_err[0] = '\0';
            type->graphic_data = load_graphic (path, graphic_err,
                                               sizeof graphic_err);
            if (type->graphic_data == NULL) {
                set_error (err, err_size,
                           "mod %s: type '%s' graphic failed: %s",
                           mod->id, type->key,
                           graphic_err[0] != '\0' ? graphic_err
                                                  : type->graphic);
                return 0;
            }
        }
    }
    return 1;
}

static void
plugin_close_handle (PLUGIN_HANDLE handle)
{
    if (handle == NULL)
        return;
#if defined(WIN32)
    FreeLibrary (handle);
#else
    dlclose (handle);
#endif
}

static int
plugin_map_coord_ok (int x, int y)
{
    return x >= 0 && x < WORLD_SIDE_LEN && y >= 0 && y < WORLD_SIDE_LEN;
}

static struct PLUGIN_HOST *
plugin_host_from_api (lc_host *h)
{
    return (struct PLUGIN_HOST *) h;
}

static int
plugin_host_get_origin (lc_host *h, int x, int y, int *ox, int *oy)
{
    if (h == NULL || ox == NULL || oy == NULL
        || !plugin_map_coord_ok (x, y))
        return 0;
    if (MP_TYPE (x, y) == CST_USED) {
        *ox = MP_INFO (x, y).int_1;
        *oy = MP_INFO (x, y).int_2;
    } else {
        *ox = x;
        *oy = y;
    }
    return plugin_map_coord_ok (*ox, *oy)
        && MP_TYPE (*ox, *oy) != CST_USED;
}

static int
plugin_host_get_group (lc_host *h, int x, int y, uint16_t *group)
{
    if (h == NULL || group == NULL || !plugin_map_coord_ok (x, y))
        return 0;
    *group = (uint16_t) MP_GROUP (x, y);
    return 1;
}

static int
plugin_host_get_type (lc_host *h, int x, int y, uint16_t *type)
{
    if (h == NULL || type == NULL || !plugin_map_coord_ok (x, y))
        return 0;
    *type = (uint16_t) MP_TYPE (x, y);
    return 1;
}

static int32_t *
plugin_field_ptr (Map_Point_Info *info, uint32_t field)
{
    switch (field) {
    case LC_FIELD_POPULATION: return &info->population;
    case LC_FIELD_INT_1: return &info->int_1;
    case LC_FIELD_INT_2: return &info->int_2;
    case LC_FIELD_INT_3: return &info->int_3;
    case LC_FIELD_INT_4: return &info->int_4;
    case LC_FIELD_INT_5: return &info->int_5;
    case LC_FIELD_INT_6: return &info->int_6;
    case LC_FIELD_INT_7: return &info->int_7;
    default: return NULL;
    }
}

static int
plugin_host_get_field (lc_host *h, int x, int y, uint32_t field,
                       int32_t *value)
{
    if (h == NULL || value == NULL || !plugin_map_coord_ok (x, y))
        return 0;
    if (field == LC_FIELD_COAL_RESERVE) {
        *value = MP_INFO (x, y).coal_reserve;
        return 1;
    }
    if (field == LC_FIELD_ORE_RESERVE) {
        *value = MP_INFO (x, y).ore_reserve;
        return 1;
    }
    {
        int32_t *p = plugin_field_ptr (&MP_INFO (x, y), field);
        if (p == NULL)
            return 0;
        *value = *p;
    }
    return 1;
}

static int
plugin_host_get_flags (lc_host *h, int x, int y, uint32_t *flags)
{
    if (h == NULL || flags == NULL || !plugin_map_coord_ok (x, y))
        return 0;
    *flags = (uint32_t) MP_INFO (x, y).flags;
    return 1;
}

static int
plugin_host_owned_origin (lc_host *h, int x, int y)
{
    int group;

    if (h == NULL || !plugin_map_coord_ok (x, y)
        || MP_TYPE (x, y) == CST_USED)
        return 0;
    group = MP_GROUP (x, y);
    if (group < 0 || group >= MAX_GROUPS)
        return 0;
    return plugin_group_owner[group] == (unsigned char)
        (plugin_host_from_api (h)->slot + 1);
}

static int
plugin_host_set_field (lc_host *h, int x, int y, uint32_t field, int32_t value)
{
    Map_Point_Info *info;
    int32_t *p;

    if (!plugin_host_owned_origin (h, x, y))
        return 0;
    info = &MP_INFO (x, y);
    if (field == LC_FIELD_COAL_RESERVE
        || field == LC_FIELD_ORE_RESERVE) {
        if (value < 0 || value > 65535)
            return 0;
        if (field == LC_FIELD_COAL_RESERVE)
            info->coal_reserve = (unsigned short) value;
        else
            info->ore_reserve = (unsigned short) value;
        return 1;
    }
    p = plugin_field_ptr (info, field);
    if (p == NULL)
        return 0;
    *p = value;
    return 1;
}

static int
plugin_host_set_flags (lc_host *h, int x, int y, uint32_t flags)
{
    if (!plugin_host_owned_origin (h, x, y))
        return 0;
    MP_INFO (x, y).flags = (int)
        ((MP_INFO (x, y).flags & ~FLAG_MOD_MASK)
         | (flags & FLAG_MOD_MASK));
    return 1;
}

static void
plugin_host_prepare (size_t slot)
{
    struct PLUGIN_HOST *host = &plugin_hosts[slot];

    memset (host, 0, sizeof *host);
    host->slot = slot;
    host->api.size = sizeof host->api;
    host->api.api_version = LC_HOST_API_VERSION;
    host->api.get_origin = plugin_host_get_origin;
    host->api.get_group = plugin_host_get_group;
    host->api.get_type = plugin_host_get_type;
    host->api.get_field = plugin_host_get_field;
    host->api.get_flags = plugin_host_get_flags;
    host->api.set_field = plugin_host_set_field;
    host->api.set_flags = plugin_host_set_flags;
}

static void
plugin_clear_all (void)
{
    while (plugin_handle_count > 0) {
        size_t i = --plugin_handle_count;

        if (plugin_descs[i] != NULL && plugin_descs[i]->shutdown != NULL)
            plugin_descs[i]->shutdown ();
        plugin_descs[i] = NULL;
        plugin_close_handle (plugin_handles[i]);
        plugin_handles[i] = NULL;
    }
    memset (plugin_behaviors, 0, sizeof plugin_behaviors);
    memset (plugin_behavior_hosts, 0, sizeof plugin_behavior_hosts);
    memset (plugin_group_owner, 0, sizeof plugin_group_owner);
    memset (plugin_hosts, 0, sizeof plugin_hosts);
    plugin_behavior_count = 0;
}

#define PLUGIN_ADAPTER(n)                                                     \
    static void plugin_simulate_##n (int x, int y)                             \
    {                                                                         \
        if (plugin_behaviors[n] != NULL                                   \
            && plugin_behaviors[n]->simulate != NULL)                         \
            plugin_behaviors[n]->simulate (plugin_behavior_hosts[n], x, y);   \
    }                                                                         \
    static void plugin_mps_##n (int x, int y)                                  \
    {                                                                         \
        if (plugin_behaviors[n] != NULL                                   \
            && plugin_behaviors[n]->mps != NULL)                              \
            plugin_behaviors[n]->mps (plugin_behavior_hosts[n], x, y);        \
    }

PLUGIN_ADAPTER(0)
PLUGIN_ADAPTER(1)
PLUGIN_ADAPTER(2)
PLUGIN_ADAPTER(3)
PLUGIN_ADAPTER(4)
PLUGIN_ADAPTER(5)
PLUGIN_ADAPTER(6)
PLUGIN_ADAPTER(7)

static void (*plugin_simulate_for (size_t index)) (int, int)
{
    switch (index) {
    case 0: return plugin_simulate_0;
    case 1: return plugin_simulate_1;
    case 2: return plugin_simulate_2;
    case 3: return plugin_simulate_3;
    case 4: return plugin_simulate_4;
    case 5: return plugin_simulate_5;
    case 6: return plugin_simulate_6;
    case 7: return plugin_simulate_7;
    default: return NULL;
    }
}

static void (*plugin_mps_for (size_t index)) (int, int)
{
    switch (index) {
    case 0: return plugin_mps_0;
    case 1: return plugin_mps_1;
    case 2: return plugin_mps_2;
    case 3: return plugin_mps_3;
    case 4: return plugin_mps_4;
    case 5: return plugin_mps_5;
    case 6: return plugin_mps_6;
    case 7: return plugin_mps_7;
    default: return NULL;
    }
}

static int
plugin_behavior_index (const lc_plugin_v1 *desc, const char *id,
                       size_t *index)
{
    uint32_t i;

    if (desc == NULL || id == NULL || index == NULL)
        return 0;
    for (i = 0; i < desc->behavior_count; i++) {
        if (desc->behaviors[i].id != NULL
            && strcmp (desc->behaviors[i].id, id) == 0) {
            *index = i;
            return 1;
        }
    }
    return 0;
}

static int
plugin_open_handle (const char *path, PLUGIN_HANDLE *handle,
                    char *err, size_t err_size)
{
#if defined(WIN32)
    HMODULE h = LoadLibraryA (path);

    if (h == NULL) {
        set_error (err, err_size, "cannot load native plugin %s (error %lu)",
                   path, (unsigned long) GetLastError ());
        return 0;
    }
    *handle = h;
#else
    void *h = dlopen (path, RTLD_NOW | RTLD_LOCAL);

    if (h == NULL) {
        const char *why = dlerror ();
        set_error (err, err_size, "cannot load native plugin %s: %s", path,
                   why == NULL ? "unknown dlopen error" : why);
        return 0;
    }
    *handle = h;
#endif
    return 1;
}

static int
plugin_load_one (const struct MOD_SPEC *mod, mod_graphic_load_fn load_graphic,
                 char *err, size_t err_size)
{
    typedef const lc_plugin_v1 *(*PLUGIN_ENTRY) (void);
    char path[MOD_PATH_MAX + 1];
    PLUGIN_HANDLE handle = NULL;
    const lc_plugin_v1 *desc;
    PLUGIN_ENTRY entry;
    unsigned short group_ids[GROUPS_PER_MOD];
    unsigned short type_ids[TYPES_PER_MOD];
    int type_groups[TYPES_PER_MOD];
    size_t behavior_indices[GROUPS_PER_MOD];
    char *graphics[TYPES_PER_MOD];
    size_t behavior_base = plugin_behavior_count;
    size_t slot = plugin_handle_count;
    size_t group_count;
    size_t type_count;
    size_t i;
    size_t j;

    if (slot >= PLUGIN_MAX_BEHAVIORS) {
        set_error (err, err_size, "too many native plugins");
        return 0;
    }
    plugin_host_prepare (slot);

    if (mod == NULL || !mod->has_plugin || mod->plugin_path[0] == '\0') {
        set_error (err, err_size, "mod %s: native plugin path is missing",
                   mod == NULL ? "?" : mod->id);
        return 0;
    }
    if (!join_relative (path, sizeof path, mod->dir, mod->plugin_path)) {
        set_error (err, err_size, "mod %s: unsafe native plugin path '%s'",
                   mod->id, mod->plugin_path);
        return 0;
    }
    if (!file_exists (path)) {
        set_error (err, err_size, "mod %s: native plugin is missing: %s",
                   mod->id, mod->plugin_path);
        return 0;
    }
    if (!plugin_open_handle (path, &handle, err, err_size))
        return 0;

#if defined(WIN32)
    entry = (PLUGIN_ENTRY) GetProcAddress (handle, "lc_plugin_entry");
#else
    entry = (PLUGIN_ENTRY) dlsym (handle, "lc_plugin_entry");
#endif
    if (entry == NULL) {
        set_error (err, err_size, "mod %s: native plugin has no lc_plugin_entry",
                   mod->id);
        plugin_close_handle (handle);
        return 0;
    }
    desc = entry ();
    if (desc == NULL) {
        set_error (err, err_size, "mod %s: lc_plugin_entry returned NULL",
                   mod->id);
        plugin_close_handle (handle);
        return 0;
    }
    if (desc->api_version != LC_PLUGIN_API_VERSION) {
        set_error (err, err_size,
                   "mod %s: native plugin API %u is not %u",
                   mod->id, desc->api_version, LC_PLUGIN_API_VERSION);
        plugin_close_handle (handle);
        return 0;
    }
    if (desc->size != sizeof (lc_plugin_v1)) {
        set_error (err, err_size,
                   "mod %s: native plugin ABI size %u is not %u",
                   mod->id, desc->size, (unsigned) sizeof (lc_plugin_v1));
        plugin_close_handle (handle);
        return 0;
    }
    if (desc->id == NULL || strcmp (desc->id, mod->id) != 0) {
        set_error (err, err_size,
                   "mod %s: native plugin id does not match the manifest",
                   mod->id);
        plugin_close_handle (handle);
        return 0;
    }
    if (desc->version == NULL || strcmp (desc->version, mod->version) != 0) {
        set_error (err, err_size,
                   "mod %s: native plugin version does not match the manifest",
                   mod->id);
        plugin_close_handle (handle);
        return 0;
    }
    if (desc->behavior_count > PLUGIN_MAX_BEHAVIORS
        || desc->behavior_count + plugin_behavior_count
           > PLUGIN_MAX_BEHAVIORS) {
        set_error (err, err_size, "mod %s: too many native behaviors", mod->id);
        plugin_close_handle (handle);
        return 0;
    }
    if (desc->behavior_count > 0 && desc->behaviors == NULL) {
        set_error (err, err_size, "mod %s: native behavior table is missing",
                   mod->id);
        plugin_close_handle (handle);
        return 0;
    }
    for (i = 0; i < desc->behavior_count; i++) {
        const lc_behavior_v1 *behavior = &desc->behaviors[i];

        if (behavior->size != sizeof (lc_behavior_v1)
            || behavior->id == NULL || !token_ok (behavior->id)) {
            set_error (err, err_size,
                       "mod %s: native behavior %zu is invalid", mod->id, i);
            plugin_close_handle (handle);
            return 0;
        }
        for (j = 0; j < i; j++) {
            if (strcmp (desc->behaviors[j].id, behavior->id) == 0) {
                set_error (err, err_size,
                           "mod %s: duplicate native behavior '%s'",
                           mod->id, behavior->id);
                plugin_close_handle (handle);
                return 0;
            }
        }
    }

    group_count = desc->group_count;
    type_count = desc->type_count;
    if (group_count > GROUPS_PER_MOD || type_count > TYPES_PER_MOD
        || (group_count > 0 && desc->groups == NULL)
        || (type_count > 0 && desc->types == NULL) || group_count == 0
        || type_count == 0) {
        set_error (err, err_size,
                   "mod %s: native plugin group/type table is invalid",
                   mod->id);
        plugin_close_handle (handle);
        return 0;
    }
    memset (graphics, 0, sizeof graphics);

    for (i = 0; i < group_count; i++) {
        const lc_group_desc_v1 *group = &desc->groups[i];
        size_t behavior_index;
        int id;

        if (group->size != sizeof (lc_group_desc_v1)
            || group->key == NULL || !token_ok (group->key)
            || group->name == NULL || group->name[0] == '\0'
            || group->size_sq < 1 || group->size_sq > 4
            || group->behavior == NULL
            || !plugin_behavior_index (desc, group->behavior,
                                       &behavior_index)) {
            set_error (err, err_size,
                       "mod %s: native group %zu is invalid", mod->id, i);
            goto fail;
        }
        for (j = 0; j < i; j++) {
            if (strcmp (desc->groups[j].key, group->key) == 0) {
                set_error (err, err_size,
                           "mod %s: duplicate native group '%s'",
                           mod->id, group->key);
                goto fail;
            }
        }
        behavior_indices[i] = behavior_base + behavior_index;
        id = group->group;
        if (id == 0) {
            for (id = NUM_OF_GROUPS; id < MAX_GROUPS; id++) {
                if (main_groups[id].size == 0) {
                    size_t k;

                    for (k = 0; k < i; k++)
                        if (group_ids[k] == id)
                            break;
                    if (k == i)
                        break;
                }
            }
        }
        if (id < NUM_OF_GROUPS || id >= MAX_GROUPS
            || main_groups[id].size != 0) {
            set_error (err, err_size,
                       "mod %s: native group '%s' uses unavailable ID %d",
                       mod->id, group->key, id);
            goto fail;
        }
        for (j = 0; j < i; j++) {
            if (group_ids[j] == (unsigned short) id) {
                set_error (err, err_size,
                           "mod %s: duplicate native group ID %d",
                           mod->id, id);
                goto fail;
            }
        }
        group_ids[i] = (unsigned short) id;
    }

    for (i = 0; i < type_count; i++) {
        const lc_type_desc_v1 *type = &desc->types[i];
        int group_id = type->group;
        int id;

        if (type->size != sizeof (lc_type_desc_v1)
            || type->key == NULL || !token_ok (type->key)
            || type->graphic == NULL || type->graphic[0] == '\0') {
            set_error (err, err_size,
                       "mod %s: native type %zu is invalid", mod->id, i);
            goto fail;
        }
        for (j = 0; j < i; j++) {
            if (strcmp (desc->types[j].key, type->key) == 0) {
                set_error (err, err_size,
                           "mod %s: duplicate native type '%s'",
                           mod->id, type->key);
                goto fail;
            }
        }
        {
            size_t k;

            for (k = 0; k < group_count; k++)
                if (group_ids[k] == (unsigned short) group_id)
                    break;
            if (k == group_count) {
                set_error (err, err_size,
                           "mod %s: native type '%s' references unknown group %d",
                           mod->id, type->key, group_id);
                goto fail;
            }
        }
        id = type->type;
        if (id == 0) {
            for (id = NUM_OF_TYPES; id < MAX_TYPES; id++) {
                size_t k;

                if (main_types[id].group < 0) {
                    for (k = 0; k < i; k++)
                        if (type_ids[k] == id)
                            break;
                    if (k == i)
                        break;
                }
            }
        }
        if (id < NUM_OF_TYPES || id >= MAX_TYPES
            || main_types[id].group >= 0) {
            set_error (err, err_size,
                       "mod %s: native type '%s' uses unavailable ID %d",
                       mod->id, type->key, id);
            goto fail;
        }
        for (j = 0; j < i; j++) {
            if (type_ids[j] == (unsigned short) id) {
                set_error (err, err_size,
                           "mod %s: duplicate native type ID %d",
                           mod->id, id);
                goto fail;
            }
        }
        type_ids[i] = (unsigned short) id;
        type_groups[i] = group_id;
        {
            char graphic_path[MOD_PATH_MAX + 1];
            char graphic_err[256];

            if (!join_relative (graphic_path, sizeof graphic_path, mod->dir,
                                type->graphic)) {
                set_error (err, err_size,
                           "mod %s: native type '%s' has unsafe graphic '%s'",
                           mod->id, type->key, type->graphic);
                goto fail;
            }
            if (!file_exists (graphic_path)) {
                set_error (err, err_size,
                           "mod %s: native type '%s' graphic is missing: %s",
                           mod->id, type->key, type->graphic);
                goto fail;
            }
            graphic_err[0] = '\0';
            graphics[i] = load_graphic (graphic_path, graphic_err,
                                        sizeof graphic_err);
            if (graphics[i] == NULL) {
                set_error (err, err_size,
                           "mod %s: native type '%s' graphic failed: %s",
                           mod->id, type->key,
                           graphic_err[0] == '\0' ? type->graphic
                                                 : graphic_err);
                goto fail;
            }
        }
    }

    for (i = 0; i < group_count; i++) {
        const lc_group_desc_v1 *group = &desc->groups[i];
        struct GROUP grp;
        char key[MOD_KEY_LEN + 1];
        int primary = -1;
        size_t k;

        for (k = 0; k < type_count; k++) {
            if (type_groups[k] == group_ids[i]) {
                primary = (int) k;
                break;
            }
        }
        if (primary < 0) {
            set_error (err, err_size,
                       "mod %s: native group '%s' has no type",
                       mod->id, group->key);
            goto fail;
        }
        if (!build_key (key, sizeof key, mod->id, group->key)) {
            set_error (err, err_size, "mod %s: native group key is too long",
                       mod->id);
            goto fail;
        }
        memset (&grp, 0, sizeof grp);
        copy_text (grp.name, sizeof grp.name, group->name);
        grp.size = (unsigned short) group->size_sq;
        grp.colour = group->colour;
        grp.cost_mul = group->cost_mul;
        grp.bul_cost = group->bul_cost;
        grp.fire_chance = group->fire_chance;
        grp.cost = group->cost;
        grp.tech = group->tech;
        copy_text (grp.category, sizeof grp.category, "Mods");
        grp.menu_order = 1000 + (int) i;
        grp.default_type = type_ids[primary];
        grp.button = graphics[primary];
        if (!mod_register_group_keyed (group_ids[i], key, mod->id, &grp)) {
            set_error (err, err_size,
                       "mod %s: failed to register native group '%s'",
                       mod->id, group->key);
            goto fail;
        }
        if (!mod_set_group_behavior (group_ids[i], group->traits,
                                     plugin_simulate_for (behavior_indices[i]),
                                     plugin_mps_for (behavior_indices[i]),
                                     NULL)) {
            set_error (err, err_size,
                       "mod %s: failed to attach native behavior '%s'",
                       mod->id, group->behavior);
            goto fail;
        }
    }

    for (i = 0; i < type_count; i++) {
        const lc_type_desc_v1 *type = &desc->types[i];
        char key[MOD_KEY_LEN + 1];

        if (!build_key (key, sizeof key, mod->id, type->key)) {
            set_error (err, err_size, "mod %s: native type key is too long",
                       mod->id);
            goto fail;
        }
        if (!mod_register_type_keyed (type_ids[i], key, mod->id,
                                      type_groups[i], graphics[i])) {
            set_error (err, err_size,
                       "mod %s: failed to register native type '%s'",
                       mod->id, type->key);
            goto fail;
        }
    }

    for (i = 0; i < desc->behavior_count; i++) {
        plugin_behaviors[behavior_base + i] = &desc->behaviors[i];
        plugin_behavior_hosts[behavior_base + i] = &plugin_hosts[slot].api;
    }
    plugin_behavior_count = behavior_base + desc->behavior_count;

    if (desc->init != NULL && desc->init (&plugin_hosts[slot].api) != 0) {
        set_error (err, err_size, "mod %s: native plugin init failed",
                   mod->id);
        goto fail;
    }
    for (i = 0; i < group_count; i++)
        plugin_group_owner[group_ids[i]] = (unsigned char) (slot + 1);
    plugin_handles[slot] = handle;
    plugin_descs[slot] = desc;
    plugin_handle_count++;
    return 1;

fail:
    plugin_behavior_count = behavior_base;
    memset (plugin_behavior_hosts, 0, sizeof plugin_behavior_hosts);
    for (i = 0; i < type_count; i++)
        free (graphics[i]);
    plugin_close_handle (handle);
    return 0;
}

static int
commit_mods (struct MOD_SPEC *mods, const int *load_order, int enabled_count,
             mod_behavior_lookup_fn lookup,
             char *err, size_t err_size)
{
    struct MOD_REGISTRY_STATE *state;
    int i;
    int j;

    state = malloc (sizeof *state);
    if (state == NULL) {
        set_error (err, err_size, "out of memory");
        return 0;
    }
    mod_registry_snapshot (state);

    for (i = 0; i < enabled_count; i++) {
        struct MOD_SPEC *mod = &mods[load_order[i]];
        for (j = 0; j < mod->group_count; j++) {
            struct GROUP_SPEC *group = &mod->groups[j];
            struct GROUP desc;
            char key[MOD_KEY_LEN + 1];
            const char *behavior = group->behavior;

            if (strncmp (behavior, "plugin:", 7) == 0) {
                set_error (err, err_size,
                           "mod %s: native behavior '%s' is not available",
                           mod->id, behavior);
                goto fail;
            }
            group->behavior_def = lookup (behavior);
            if (group->behavior_def == NULL) {
                set_error (err, err_size,
                           "mod %s: unknown behavior '%s'",
                           mod->id, behavior);
                goto fail;
            }
            if (!group->behavior_def->mod_safe) {
                set_error (err, err_size,
                           "mod %s: behavior '%s' is not safe for mods",
                           mod->id, behavior);
                goto fail;
            }
            memset (&desc, 0, sizeof desc);
            copy_text (desc.name, sizeof desc.name, group->name);
            desc.no_credit = (unsigned short) group->no_credit;
            desc.size = (unsigned short) group->size;
            desc.colour = group->has_colour ? group->colour : 76;
            desc.cost_mul = group->cost_mul;
            desc.bul_cost = group->bul_cost;
            desc.fire_chance = group->fire_chance;
            desc.cost = group->cost;
            desc.tech = group->tech;
            copy_text (desc.category, sizeof desc.category, group->category);
            desc.menu_order = group->has_order ? group->order : 1000 + i;
            desc.button = group->button_data;
            copy_text (desc.help, sizeof desc.help, group->help_path);
            {
                int primary = find_type_spec (mod, group->key);

                if (primary < 0) {
                    set_error (err, err_size,
                               "mod %s: group '%s' has no primary type",
                               mod->id, group->key);
                    goto fail;
                }
                desc.default_type = mod->types[primary].type_id;
            }
            if (!build_key (key, sizeof key, mod->id, group->key)) {
                set_error (err, err_size,
                           "mod %s: group key is too long", mod->id);
                goto fail;
            }
            if (!mod_register_group_keyed (group->group_id, key, mod->id,
                                           &desc)) {
                set_error (err, err_size,
                           "mod %s: failed to register group '%s'",
                           mod->id, group->key);
                goto fail;
            }
            mod_set_group_behavior (group->group_id,
                                    group->behavior_def->traits,
                                    group->behavior_def->simulate,
                                    group->behavior_def->mps,
                                    group->behavior_def->update_tech);
        }

        for (j = 0; j < mod->type_count; j++) {
            struct TYPE_SPEC *type = &mod->types[j];
            int group_index = find_group_spec (mod, type->group_key);
            char key[MOD_KEY_LEN + 1];

            if (!build_key (key, sizeof key, mod->id, type->key)) {
                set_error (err, err_size,
                           "mod %s: type key is too long", mod->id);
                goto fail;
            }
            if (!mod_register_type_keyed (type->type_id, key, mod->id,
                                          mod->groups[group_index].group_id,
                                          type->graphic_data)) {
                set_error (err, err_size,
                           "mod %s: failed to register type '%s'",
                           mod->id, type->key);
                goto fail;
            }
            if (type->name[0] != '\0'
                && !mod_set_type_name (type->type_id, type->name)) {
                set_error (err, err_size,
                           "mod %s: failed to name type '%s'",
                           mod->id, type->key);
                goto fail;
            }
        }
    }

    free (state);
    return 1;

fail:
    mod_registry_restore (state);
    free (state);
    return 0;
}

int
mod_loader_load (const char *dir, const char *conf,
                 mod_behavior_lookup_fn lookup,
                 mod_graphic_load_fn load_graphic,
                 char *err, size_t err_size)
{
    struct MOD_SPEC *mods = NULL;
    struct CONF_ENTRY conf_entries[MOD_LIST_MAX];
    int load_order[MOD_LIST_MAX];
    char default_conf[MOD_PATH_MAX + 1];
    const char *conf_path = conf;
    int conf_count = 0;
    int have_config = 0;
    int count = 0;
    int enabled = 0;
    int i;

    if (loaded) {
        set_error (err, err_size, "mods are already loaded");
        return 0;
    }
    if (dir == NULL || dir[0] == '\0')
        return 1;
    if (lookup == NULL || load_graphic == NULL) {
        set_error (err, err_size, "mod loader callbacks are missing");
        return 0;
    }
    if (!discover_mods (dir, &mods, &count, err, err_size))
        return 0;

    if (conf_path == NULL) {
        if (snprintf (default_conf, sizeof default_conf, "%s/mods.conf", dir)
            < (int) sizeof default_conf
            && file_exists (default_conf)) {
            conf_path = default_conf;
            have_config = 1;
        }
    } else {
        have_config = 1;
    }
    if (have_config
        && !read_config (conf_path, conf_entries, &conf_count,
                         err, err_size)) {
        free_specs (mods, count, 0);
        return 0;
    }
    if (!apply_config (mods, count, conf_entries, conf_count,
                       have_config, err, err_size)) {
        free_specs (mods, count, 0);
        return 0;
    }

    if (!build_load_order (mods, count, have_config, conf_entries, conf_count,
                           load_order, &enabled, err, err_size)) {
        free_specs (mods, count, 0);
        return 0;
    }
    if (enabled == 0) {
        free_specs (mods, count, 0);
        return 1;
    }

    for (i = 0; i < enabled; i++) {
        struct MOD_SPEC *mod = &mods[load_order[i]];

        if (mod->has_plugin && !native_enabled) {
            set_error (err, err_size,
                       "mod %s: native plugin requires --native-plugins",
                       mod->id);
            free_specs (mods, count, 0);
            return 0;
        }
    }

    /* Native plugins reserve IDs before data-mod preview so both kinds of
       mod share one collision-free allocation.  Keep a snapshot around the
       whole operation so a data-mod failure also unregisters the plugin. */
    {
        struct MOD_REGISTRY_STATE *outer = malloc (sizeof *outer);

        if (outer == NULL) {
            set_error (err, err_size, "out of memory");
            free_specs (mods, count, 0);
            return 0;
        }
        mod_registry_snapshot (outer);
        for (i = 0; i < enabled; i++) {
            struct MOD_SPEC *mod = &mods[load_order[i]];

            if (mod->has_plugin
                && !plugin_load_one (mod, load_graphic, err, err_size)) {
                mod_registry_restore (outer);
                plugin_clear_all ();
                free (outer);
                free_specs (mods, count, 0);
                return 0;
            }
        }
        if (!preview_ids (mods, load_order, enabled, err, err_size)) {
            mod_registry_restore (outer);
            plugin_clear_all ();
            free (outer);
            free_specs (mods, count, 0);
            return 0;
        }
        if (!load_graphics (mods, load_order, enabled, load_graphic,
                            err, err_size)) {
            mod_registry_restore (outer);
            plugin_clear_all ();
            free (outer);
            free_specs (mods, count, 1);
            return 0;
        }
        if (!commit_mods (mods, load_order, enabled, lookup, err, err_size)) {
            mod_registry_restore (outer);
            plugin_clear_all ();
            free (outer);
            free_specs (mods, count, 1);
            return 0;
        }
        free (outer);
    }

    enabled_total = 0;
    for (i = 0; i < enabled; i++) {
        struct MOD_SPEC *mod = &mods[load_order[i]];

        copy_text (enabled_ids[enabled_total],
                   sizeof enabled_ids[enabled_total], mod->id);
        copy_text (enabled_versions[enabled_total],
                   sizeof enabled_versions[enabled_total], mod->version);
        enabled_apis[enabled_total] = mod->api;
        enabled_total++;
    }

    loaded = 1;
    free_specs (mods, count, 0);
    return 1;
}

void
mod_loader_enable_native (int enabled)
{
    native_enabled = enabled != 0;
}

void
mod_loader_init (void)
{
    loaded = 0;
    enabled_total = 0;
    native_enabled = 0;
    plugin_clear_all ();
    memset (enabled_apis, 0, sizeof enabled_apis);
}

void
mod_loader_shutdown (void)
{
    loaded = 0;
    enabled_total = 0;
    native_enabled = 0;
    plugin_clear_all ();
    memset (enabled_apis, 0, sizeof enabled_apis);
}

size_t
mod_loader_enabled_count (void)
{
    return enabled_total;
}

const char *
mod_loader_enabled_id (size_t index)
{
    return index < enabled_total ? enabled_ids[index] : NULL;
}

const char *
mod_loader_enabled_version (size_t index)
{
    return index < enabled_total ? enabled_versions[index] : NULL;
}

int
mod_loader_enabled_api (size_t index)
{
    return index < enabled_total ? enabled_apis[index] : 0;
}

size_t
mod_loader_enabled_plugin_count (void)
{
    return loaded ? plugin_handle_count : 0;
}

const char *
mod_loader_enabled_plugin_id (size_t index)
{
    if (!loaded || index >= plugin_handle_count || plugin_descs[index] == NULL)
        return NULL;
    return plugin_descs[index]->id;
}

const char *
mod_loader_enabled_plugin_version (size_t index)
{
    if (!loaded || index >= plugin_handle_count || plugin_descs[index] == NULL)
        return NULL;
    return plugin_descs[index]->version;
}

int
mod_loader_enabled_plugin_api (size_t index)
{
    if (!loaded || index >= plugin_handle_count || plugin_descs[index] == NULL)
        return 0;
    return (int) plugin_descs[index]->api_version;
}
