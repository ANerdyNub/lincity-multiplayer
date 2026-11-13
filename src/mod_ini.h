/* ---------------------------------------------------------------------- *
 * mod_ini.h
 * Small, strict INI reader used by mod manifests.
 * ---------------------------------------------------------------------- */
#ifndef __mod_ini_h__
#define __mod_ini_h__

#include <stddef.h>

#define MOD_INI_SECTION_MAX 96
#define MOD_INI_KEY_MAX     64
#define MOD_INI_VALUE_MAX   1024

struct MOD_INI_ENTRY
{
    char section[MOD_INI_SECTION_MAX + 1];
    char key[MOD_INI_KEY_MAX + 1];
    char value[MOD_INI_VALUE_MAX + 1];
    int line;
};

struct MOD_INI
{
    struct MOD_INI_ENTRY *entries;
    size_t count;
    size_t capacity;
};

void mod_ini_init (struct MOD_INI *ini);
void mod_ini_free (struct MOD_INI *ini);
int mod_ini_parse (const char *path, struct MOD_INI *ini,
                   char *err, size_t err_size);
int mod_ini_has_section (const struct MOD_INI *ini, const char *section);
const char *mod_ini_get (const struct MOD_INI *ini, const char *section,
                         const char *key);
size_t mod_ini_section_count (const struct MOD_INI *ini,
                              const char *section);

#endif /* __mod_ini_h__ */
