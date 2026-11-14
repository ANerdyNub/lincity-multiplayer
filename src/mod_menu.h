/* ---------------------------------------------------------------------- *
 * mod_menu.h
 * Pure menu/page model shared by the module-button UI and headless tests.
 *
 * Page 0 is the base game's original 32-button page, in the exact order the
 * caller adds those entries.  Later pages are grouped by manifest category
 * and ordered by the manifest's order value, then by insertion (load) order.
 * ---------------------------------------------------------------------- */
#ifndef __mod_menu_h__
#define __mod_menu_h__

#define MOD_MENU_SLOTS        32
#define MOD_MENU_MAX_ENTRIES  512
#define MOD_MENU_MAX_PAGES    64
#define MOD_MENU_CATEGORY_LEN 32
#define MOD_MENU_HELP_LEN     40

struct MOD_MENU_ENTRY
{
    int group;
    int type;
    char *graphic;                 /* 16x16 palette tile, or NULL */
    char help[MOD_MENU_HELP_LEN + 1];
    char category[MOD_MENU_CATEGORY_LEN + 1];
    int order;
    int sequence;                  /* insertion order, for stable sorting */
    int base;                      /* 1 on page 0 */
    int available;                 /* runtime tech gate */
    int help_seen;                 /* runtime flag for mod pages */
};

struct MOD_MENU_PAGE
{
    int first;
    int count;
    int base;
    char category[MOD_MENU_CATEGORY_LEN + 1];
};

void mod_menu_reset (void);

/* Add page-0 entries in slot order.  Exactly MOD_MENU_SLOTS are required
   before mod entries are appended. */
int mod_menu_add_base (int group, int type, char *graphic, const char *help);

/* Add a registry-backed mod entry.  category may be NULL/empty for "Mods". */
int mod_menu_add_mod (int group, int type, char *graphic, const char *help,
                      const char *category, int order);

/* Sort mod entries and build pages.  Returns 0 on invalid input. */
int mod_menu_build (void);

int mod_menu_page_count (void);
int mod_menu_page_size (int page);
int mod_menu_page_is_base (int page);
const char *mod_menu_page_category (int page);
struct MOD_MENU_ENTRY *mod_menu_entry (int page, int slot);

/* Find a group on any page.  Returns 1 and fills page/slot when present. */
int mod_menu_find_group (int group, int *page, int *slot);

#endif /* __mod_menu_h__ */
