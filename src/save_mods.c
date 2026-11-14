/* ---------------------------------------------------------------------- *
 * save_mods.c
 * Versioned mod block for save files and stable type remapping.
 * ---------------------------------------------------------------------- */
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lcconfig.h"
#include "lintypes.h"
#include "mod_loader.h"
#include "save_mods.h"

#define SAVE_HEADER_LINE 1024

struct TEXT_BUF
{
    char *data;
    size_t len;
    size_t cap;
};

/* Mapping from the saved numeric type ID to the current registry ID. */
static int loaded_type_present[MAX_TYPES];
static int loaded_type_map[MAX_TYPES];

void
save_mod_header_reset (void)
{
    memset (loaded_type_present, 0, sizeof loaded_type_present);
    memset (loaded_type_map, 0, sizeof loaded_type_map);
}

static int
buf_reserve (struct TEXT_BUF *buf, size_t need)
{
    char *grown;
    size_t cap;

    if (need <= buf->cap)
        return 1;
    cap = buf->cap == 0 ? 256 : buf->cap;
    while (cap < need) {
        if (cap > (size_t) -1 / 2)
            return 0;
        cap *= 2;
    }
    grown = realloc (buf->data, cap);
    if (grown == NULL)
        return 0;
    buf->data = grown;
    buf->cap = cap;
    return 1;
}

static int
buf_add (struct TEXT_BUF *buf, const char *text)
{
    size_t len = strlen (text);

    if (!buf_reserve (buf, buf->len + len + 1))
        return 0;
    memcpy (buf->data + buf->len, text, len + 1);
    buf->len += len;
    return 1;
}

static int
buf_addf (struct TEXT_BUF *buf, const char *format, ...)
{
    va_list args;
    va_list copy;
    int needed;

    va_start (args, format);
    va_copy (copy, args);
    needed = vsnprintf (NULL, 0, format, args);
    va_end (args);
    if (needed < 0) {
        va_end (copy);
        return 0;
    }
    if (!buf_reserve (buf, buf->len + (size_t) needed + 1)) {
        va_end (copy);
        return 0;
    }
    vsnprintf (buf->data + buf->len, (size_t) needed + 1, format, copy);
    va_end (copy);
    buf->len += (size_t) needed;
    return 1;
}

char *
save_mod_header_format (void)
{
    struct TEXT_BUF buf;
    size_t i;
    size_t mod_count = mod_loader_enabled_count ();
    int type_count = 0;

    memset (&buf, 0, sizeof buf);
    for (i = 0; i < (size_t) num_types; i++) {
        if (main_types[i].group >= 0 && main_types[i].key[0] != '\0')
            type_count++;
    }

    if (!buf_addf (&buf, "#lcm %d\n", SAVE_MOD_HEADER_VERSION)
        || !buf_addf (&buf, "mods %d\n", (int) mod_count))
        goto fail;

    for (i = 0; i < mod_count; i++) {
        const char *id = mod_loader_enabled_id (i);
        const char *version = mod_loader_enabled_version (i);

        if (id == NULL || version == NULL)
            goto fail;
        if (!buf_addf (&buf, "mod %s %s %d\n", id, version,
                       mod_loader_enabled_api (i)))
            goto fail;
    }

    if (!buf_addf (&buf, "types %d\n", type_count))
        goto fail;
    for (i = 0; i < (size_t) num_types; i++) {
        if (main_types[i].group < 0 || main_types[i].key[0] == '\0')
            continue;
        if (!buf_addf (&buf, "type %d %s\n", (int) i, main_types[i].key))
            goto fail;
    }
    if (!buf_add (&buf, "end\n"))
        goto fail;
    return buf.data;

fail:
    free (buf.data);
    return NULL;
}

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

/* 0 = EOF, -1 = line too long, 1 = one line without its terminator. */
static int
read_line (FILE *in, char *line, size_t size)
{
    size_t len;

    if (fgets (line, (int) size, in) == NULL)
        return 0;
    len = strlen (line);
    if (len == size - 1 && line[len - 1] != '\n') {
        int c = fgetc (in);

        if (c != EOF) {
            ungetc (c, in);
            return -1;
        }
    }
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';
    return 1;
}

static int
split_tokens (char *line, char *tokens[], int max)
{
    int count = 0;
    char *p = line;

    while (*p != '\0') {
        while (*p != '\0' && isspace ((unsigned char) *p))
            p++;
        if (*p == '\0')
            break;
        if (count >= max)
            return -1;
        tokens[count++] = p;
        while (*p != '\0' && !isspace ((unsigned char) *p))
            p++;
        if (*p != '\0')
            *p++ = '\0';
    }
    return count;
}

static int
parse_int_field (const char *text, int min, int max, int *out)
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
enabled_index (const char *id)
{
    size_t i;
    size_t count = mod_loader_enabled_count ();

    for (i = 0; i < count; i++) {
        const char *enabled = mod_loader_enabled_id (i);

        if (enabled != NULL && strcmp (enabled, id) == 0)
            return (int) i;
    }
    return -1;
}

int
save_mod_header_read (FILE *in, char *err, size_t err_size)
{
    char line[SAVE_HEADER_LINE];
    char *tokens[8];
    char mod_ids[MOD_LIST_MAX][MOD_ID_MAX + 1];
    int n;
    int count;
    int i;
    int j;
    int value;

    save_mod_header_reset ();
    if (err != NULL && err_size > 0)
        err[0] = '\0';

    /* load_city() reads the version with fscanf("%d"), which stops before
       the newline.  Skip that empty remainder (and any leading blanks)
       before the first marker line. */
    do {
        if (read_line (in, line, sizeof line) != 1) {
            set_error (err, err_size,
                       "save mod header is missing or truncated");
            return 0;
        }
    } while (line[0] == '\0');
    n = split_tokens (line, tokens, 8);
    if (n != 2 || strcmp (tokens[0], "#lcm") != 0
        || !parse_int_field (tokens[1], 0, 1000, &value)
        || value != SAVE_MOD_HEADER_VERSION) {
        set_error (err, err_size, "unsupported save mod header");
        return 0;
    }

    if (read_line (in, line, sizeof line) != 1) {
        set_error (err, err_size, "save mod header is missing or truncated");
        return 0;
    }
    n = split_tokens (line, tokens, 8);
    if (n != 2 || strcmp (tokens[0], "mods") != 0
        || !parse_int_field (tokens[1], 0, MOD_LIST_MAX, &count)) {
        set_error (err, err_size, "invalid save mod count");
        return 0;
    }
    for (i = 0; i < count; i++) {
        const char *id;
        const char *version;
        int enabled;
        int api;

        if (read_line (in, line, sizeof line) != 1) {
            set_error (err, err_size,
                       "save mod header is missing or truncated");
            return 0;
        }
        n = split_tokens (line, tokens, 8);
        if (n != 4 || strcmp (tokens[0], "mod") != 0) {
            set_error (err, err_size, "invalid save mod entry");
            return 0;
        }
        id = tokens[1];
        version = tokens[2];
        if (id[0] == '\0' || strlen (id) > MOD_ID_MAX
            || version[0] == '\0' || strlen (version) > MOD_VERSION_MAX
            || !parse_int_field (tokens[3], 1, 1000, &api)) {
            set_error (err, err_size, "invalid save mod entry");
            return 0;
        }
        for (j = 0; j < i; j++) {
            if (strcmp (mod_ids[j], id) == 0) {
                set_error (err, err_size,
                           "save lists mod %s more than once", id);
                return 0;
            }
        }
        snprintf (mod_ids[i], sizeof mod_ids[i], "%s", id);

        enabled = enabled_index (id);
        if (enabled < 0) {
            set_error (err, err_size,
                       "save requires mod %s@%s, which is not enabled",
                       id, version);
            return 0;
        }
        if (strcmp (version, mod_loader_enabled_version (enabled)) != 0) {
            set_error (err, err_size,
                       "save requires mod %s@%s, but version %s is enabled",
                       id, version,
                       mod_loader_enabled_version (enabled));
            return 0;
        }
        if (api != mod_loader_enabled_api (enabled)) {
            set_error (err, err_size,
                       "save requires mod %s api %d, but api %d is enabled",
                       id, api, mod_loader_enabled_api (enabled));
            return 0;
        }
    }

    if (read_line (in, line, sizeof line) != 1) {
        set_error (err, err_size, "save mod header is missing or truncated");
        return 0;
    }
    n = split_tokens (line, tokens, 8);
    if (n != 2 || strcmp (tokens[0], "types") != 0
        || !parse_int_field (tokens[1], 0, MAX_TYPES, &count)) {
        set_error (err, err_size, "invalid save type mapping count");
        return 0;
    }
    for (i = 0; i < count; i++) {
        const char *key;
        int saved;
        int current;
        int seen;

        if (read_line (in, line, sizeof line) != 1) {
            set_error (err, err_size,
                       "save mod header is missing or truncated");
            return 0;
        }
        n = split_tokens (line, tokens, 8);
        if (n != 3 || strcmp (tokens[0], "type") != 0
            || !parse_int_field (tokens[1], 0, MAX_TYPES - 1, &saved)) {
            set_error (err, err_size, "invalid save type mapping entry");
            return 0;
        }
        key = tokens[2];
        if (key[0] == '\0' || strlen (key) > MOD_KEY_LEN) {
            set_error (err, err_size, "invalid save type mapping entry");
            return 0;
        }
        if (loaded_type_present[saved]) {
            set_error (err, err_size,
                       "save maps type %d more than once", saved);
            return 0;
        }
        current = mod_find_type_by_key (key);
        if (current < 0) {
            set_error (err, err_size,
                       "save uses type %d with key '%s', "
                       "which is not registered", saved, key);
            return 0;
        }
        for (seen = 0; seen < MAX_TYPES; seen++) {
            if (loaded_type_present[seen]
                && loaded_type_map[seen] == current) {
                set_error (err, err_size,
                           "save maps more than one type to key '%s'", key);
                return 0;
            }
        }
        loaded_type_present[saved] = 1;
        loaded_type_map[saved] = current;
    }

    if (read_line (in, line, sizeof line) != 1) {
        set_error (err, err_size, "save mod header is missing or truncated");
        return 0;
    }
    n = split_tokens (line, tokens, 8);
    if (n != 1 || strcmp (tokens[0], "end") != 0) {
        set_error (err, err_size, "save mod header has no end marker");
        return 0;
    }
    return 1;
}

int
save_mod_remap_types (short type[][WORLD_SIDE_LEN], char *err,
                      size_t err_size)
{
    int x;
    int y;

    for (x = 0; x < WORLD_SIDE_LEN; x++) {
        for (y = 0; y < WORLD_SIDE_LEN; y++) {
            int saved = type[x][y];

            if (saved < 0 || saved >= MAX_TYPES
                || !loaded_type_present[saved]) {
                set_error (err, err_size,
                           "save uses type %d at %d,%d with no mapping "
                           "in the mod header", saved, x, y);
                return 0;
            }
        }
    }

    for (x = 0; x < WORLD_SIDE_LEN; x++)
        for (y = 0; y < WORLD_SIDE_LEN; y++)
            type[x][y] = (short) loaded_type_map[type[x][y]];
    return 1;
}
