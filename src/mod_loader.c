/* ---------------------------------------------------------------------- *
 * mod_loader.c
 * Discover data-only mods, validate them without touching the registry,
 * then commit atomically.  Native plugin loading arrives in a later
 * milestone; plugin:* behavior references are rejected for now.
 * ---------------------------------------------------------------------- */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "mod_ini.h"
#include "mod_loader.h"

#define GROUPS_PER_MOD 32
#define TYPES_PER_MOD  64
#define CONF_MAX       1024

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
    char graphic[MOD_PATH_MAX + 1];
    char *graphic_data;
    const struct MOD_BEHAVIOR *behavior_def;
};

struct TYPE_SPEC
{
    char key[MOD_KEY_LEN + 1];
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
    char dir[MOD_PATH_MAX + 1];
    int enabled;
    int order;
    int group_count;
    int type_count;
    struct GROUP_SPEC groups[GROUPS_PER_MOD];
    struct TYPE_SPEC types[TYPES_PER_MOD];
};

struct CONF_ENTRY
{
    char id[MOD_ID_MAX + 1];
    int enabled;
};

static int loaded;
static char enabled_ids[MOD_LIST_MAX][MOD_ID_MAX + 1];
static char enabled_versions[MOD_LIST_MAX][MOD_VERSION_MAX + 1];
static size_t enabled_total;

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
        "bul_cost", "fire_chance", "cost", "tech", "type", "graphic"
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
                set_error (err, err_size,
                           "%s: native plugin loading is not enabled",
                           path);
                mod_ini_free (&ini);
                return 0;
            }
            if (!known && strncmp (key, "plugin-", 7) != 0) {
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
    if (mod->group_count == 0) {
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
            for (j = 0; j < mods[i].group_count; j++)
                free (mods[i].groups[j].graphic_data);
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

    for (i = 0; i < enabled_count; i++) {
        struct MOD_SPEC *mod = &mods[load_order[i]];
        for (j = 0; j < mod->group_count; j++) {
            struct GROUP_SPEC *group = &mod->groups[j];
            int id = group->group_id;

            if (id == 0) {
                for (id = NUM_OF_GROUPS; id < MAX_GROUPS; id++) {
                    if (!used_groups[id])
                        break;
                }
                if (id >= MAX_GROUPS) {
                    set_error (err, err_size,
                               "mod %s: no free group IDs", mod->id);
                    return 0;
                }
            } else if (id < NUM_OF_GROUPS || id >= MAX_GROUPS
                       || used_groups[id]) {
                set_error (err, err_size,
                           "mod %s: group '%s' uses reserved or duplicate ID %d",
                           mod->id, group->key, id);
                return 0;
            }
            used_groups[id] = 1;
            group->group_id = id;
        }
        for (j = 0; j < mod->type_count; j++) {
            struct TYPE_SPEC *type = &mod->types[j];
            int id = type->type_id;

            if (id == 0) {
                for (id = NUM_OF_TYPES; id < MAX_TYPES; id++) {
                    if (!used_types[id])
                        break;
                }
                if (id >= MAX_TYPES) {
                    set_error (err, err_size,
                               "mod %s: no free type IDs", mod->id);
                    return 0;
                }
            } else if (id < NUM_OF_TYPES || id >= MAX_TYPES
                       || used_types[id]) {
                set_error (err, err_size,
                           "mod %s: type '%s' uses reserved or duplicate ID %d",
                           mod->id, type->key, id);
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

    if (!preview_ids (mods, load_order, enabled, err, err_size)
        || !load_graphics (mods, load_order, enabled, load_graphic,
                           err, err_size)) {
        free_specs (mods, count, 1);
        return 0;
    }
    if (!commit_mods (mods, load_order, enabled, lookup, err, err_size)) {
        free_specs (mods, count, 1);
        return 0;
    }

    enabled_total = 0;
    for (i = 0; i < enabled; i++) {
        struct MOD_SPEC *mod = &mods[load_order[i]];

        copy_text (enabled_ids[enabled_total],
                   sizeof enabled_ids[enabled_total], mod->id);
        copy_text (enabled_versions[enabled_total],
                   sizeof enabled_versions[enabled_total], mod->version);
        enabled_total++;
    }

    loaded = 1;
    free_specs (mods, count, 0);
    return 1;
}

void
mod_loader_init (void)
{
    loaded = 0;
    enabled_total = 0;
}

void
mod_loader_shutdown (void)
{
    loaded = 0;
    enabled_total = 0;
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
