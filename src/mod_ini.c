/* ---------------------------------------------------------------------- *
 * mod_ini.c
 * Strict INI reader for mod manifests.  Sections and keys are lowered so
 * lookups are stable; values keep their case.
 * ---------------------------------------------------------------------- */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mod_ini.h"

static void
set_error (char *err, size_t err_size, const char *path, int line,
           const char *message)
{
    if (err == NULL || err_size == 0)
        return;
    if (line > 0)
        snprintf (err, err_size, "%s:%d: %s", path, line, message);
    else
        snprintf (err, err_size, "%s: %s", path, message);
}

static char *
trim (char *text)
{
    char *end;

    while (*text != '\0' && isspace ((unsigned char) *text))
        text++;
    end = text + strlen (text);
    while (end > text && isspace ((unsigned char) end[-1]))
        end--;
    *end = '\0';
    return text;
}

static void
lower (char *text)
{
    while (*text != '\0') {
        *text = (char) tolower ((unsigned char) *text);
        text++;
    }
}

static void
strip_comment (char *text)
{
    int quoted = 0;
    char *p;

    for (p = text; *p != '\0'; p++) {
        if (*p == '"')
            quoted = !quoted;
        else if (!quoted && (*p == '#' || *p == ';')
                 && (p == text || isspace ((unsigned char) p[-1]))) {
            *p = '\0';
            return;
        }
    }
}

static void
unquote (char *text)
{
    size_t len = strlen (text);

    if (len >= 2 && text[0] == '"' && text[len - 1] == '"') {
        memmove (text, text + 1, len - 2);
        text[len - 2] = '\0';
    }
}

static int
append_entry (struct MOD_INI *ini, const char *section, const char *key,
              const char *value, int line, char *err, size_t err_size,
              const char *path)
{
    struct MOD_INI_ENTRY *entry;
    size_t i;

    for (i = 0; i < ini->count; i++) {
        if (strcmp (ini->entries[i].section, section) == 0
            && strcmp (ini->entries[i].key, key) == 0) {
            set_error (err, err_size, path, line, "duplicate key");
            return 0;
        }
    }

    if (ini->count == ini->capacity) {
        size_t capacity = ini->capacity == 0 ? 16 : ini->capacity * 2;
        struct MOD_INI_ENTRY *grown = realloc (ini->entries,
                                               capacity * sizeof *grown);
        if (grown == NULL) {
            set_error (err, err_size, path, line, "out of memory");
            return 0;
        }
        ini->entries = grown;
        ini->capacity = capacity;
    }

    entry = &ini->entries[ini->count++];
    snprintf (entry->section, sizeof entry->section, "%s", section);
    snprintf (entry->key, sizeof entry->key, "%s", key);
    snprintf (entry->value, sizeof entry->value, "%s", value);
    entry->line = line;
    return 1;
}

void
mod_ini_init (struct MOD_INI *ini)
{
    if (ini != NULL)
        memset (ini, 0, sizeof *ini);
}

void
mod_ini_free (struct MOD_INI *ini)
{
    if (ini == NULL)
        return;
    free (ini->entries);
    memset (ini, 0, sizeof *ini);
}

int
mod_ini_parse (const char *path, struct MOD_INI *ini,
               char *err, size_t err_size)
{
    FILE *fp;
    char buffer[2048];
    char section[MOD_INI_SECTION_MAX + 1] = "";
    int line = 0;

    if (path == NULL || ini == NULL) {
        set_error (err, err_size, path == NULL ? "(null)" : path, 0,
                   "invalid parser arguments");
        return 0;
    }

    fp = fopen (path, "r");
    if (fp == NULL) {
        set_error (err, err_size, path, 0, "cannot open file");
        return 0;
    }

    while (fgets (buffer, sizeof buffer, fp) != NULL) {
        char *text;
        char *eq;
        size_t len;

        line++;
        len = strlen (buffer);
        if (len == sizeof buffer - 1 && buffer[len - 1] != '\n'
            && !feof (fp)) {
            set_error (err, err_size, path, line, "line too long");
            fclose (fp);
            return 0;
        }
        if (line == 1 && len >= 3 && (unsigned char) buffer[0] == 0xef
            && (unsigned char) buffer[1] == 0xbb
            && (unsigned char) buffer[2] == 0xbf) {
            memmove (buffer, buffer + 3, strlen (buffer + 3) + 1);
        }

        text = trim (buffer);
        if (*text == '\0' || *text == '#' || *text == ';')
            continue;

        if (*text == '[') {
            char *close = strchr (text + 1, ']');
            char *rest;

            if (close == NULL) {
                set_error (err, err_size, path, line,
                           "unterminated section header");
                fclose (fp);
                return 0;
            }
            *close = '\0';
            rest = trim (close + 1);
            if (*rest != '\0' && *rest != '#' && *rest != ';') {
                set_error (err, err_size, path, line,
                           "unexpected text after section header");
                fclose (fp);
                return 0;
            }
            text = trim (text + 1);
            lower (text);
            if (*text == '\0') {
                set_error (err, err_size, path, line, "empty section name");
                fclose (fp);
                return 0;
            }
            snprintf (section, sizeof section, "%s", text);
            continue;
        }

        eq = strchr (text, '=');
        if (eq == NULL) {
            set_error (err, err_size, path, line,
                       "expected key = value or [section]");
            fclose (fp);
            return 0;
        }
        if (section[0] == '\0') {
            set_error (err, err_size, path, line,
                       "key outside a section");
            fclose (fp);
            return 0;
        }
        *eq = '\0';
        {
            char *key = trim (text);
            char *value = trim (eq + 1);

            strip_comment (value);
            value = trim (value);
            unquote (value);
            lower (key);
            if (*key == '\0') {
                set_error (err, err_size, path, line, "empty key");
                fclose (fp);
                return 0;
            }
            if (!append_entry (ini, section, key, value, line,
                               err, err_size, path)) {
                fclose (fp);
                return 0;
            }
        }
    }

    if (ferror (fp)) {
        set_error (err, err_size, path, 0, "read error");
        fclose (fp);
        return 0;
    }
    fclose (fp);
    return 1;
}

int
mod_ini_has_section (const struct MOD_INI *ini, const char *section)
{
    size_t i;

    if (ini == NULL || section == NULL)
        return 0;
    for (i = 0; i < ini->count; i++) {
        if (strcmp (ini->entries[i].section, section) == 0)
            return 1;
    }
    return 0;
}

const char *
mod_ini_get (const struct MOD_INI *ini, const char *section, const char *key)
{
    size_t i;

    if (ini == NULL || section == NULL || key == NULL)
        return NULL;
    for (i = 0; i < ini->count; i++) {
        if (strcmp (ini->entries[i].section, section) == 0
            && strcmp (ini->entries[i].key, key) == 0)
            return ini->entries[i].value;
    }
    return NULL;
}

size_t
mod_ini_section_count (const struct MOD_INI *ini, const char *section)
{
    size_t i;
    size_t count = 0;

    if (ini == NULL || section == NULL)
        return 0;
    for (i = 0; i < ini->count; i++) {
        if (strcmp (ini->entries[i].section, section) == 0)
            count++;
    }
    return count;
}
