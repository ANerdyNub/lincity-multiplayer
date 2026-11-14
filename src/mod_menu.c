/* ---------------------------------------------------------------------- *
 * mod_menu.c
 * Pure menu/page model for the build panel.
 *
 * Page 0 is always the base game's original 32 buttons, in insertion order.
 * Mod entries are sorted by category name, then by manifest order, then by
 * insertion (load) order.  Consecutive entries in the same category are
 * paginated into groups of MOD_MENU_SLOTS.
 * ---------------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>

#include "mod_menu.h"

static struct MOD_MENU_ENTRY entries[MOD_MENU_MAX_ENTRIES];
static struct MOD_MENU_PAGE pages[MOD_MENU_MAX_PAGES];
static int entry_count;
static int base_count;
static int page_count;

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
mod_menu_reset (void)
{
    memset (entries, 0, sizeof entries);
    memset (pages, 0, sizeof pages);
    entry_count = 0;
    base_count = 0;
    page_count = 0;
}

int
mod_menu_add_base (int group, int type, char *graphic, const char *help)
{
    struct MOD_MENU_ENTRY *entry;

    /* Base entries always come first and exactly fill page 0. */
    if (base_count >= MOD_MENU_SLOTS || entry_count != base_count)
        return 0;
    if (entry_count >= MOD_MENU_MAX_ENTRIES)
        return 0;

    entry = &entries[entry_count];
    memset (entry, 0, sizeof *entry);
    entry->group = group;
    entry->type = type;
    entry->graphic = graphic;
    copy_text (entry->help, sizeof entry->help, help);
    copy_text (entry->category, sizeof entry->category, "Base");
    entry->order = base_count;
    entry->sequence = entry_count;
    entry->base = 1;
    entry->available = 1;
    entry->help_seen = 0;
    entry_count++;
    base_count++;
    return 1;
}

int
mod_menu_add_mod (int group, int type, char *graphic, const char *help,
                  const char *category, int order)
{
    struct MOD_MENU_ENTRY *entry;

    if (base_count != MOD_MENU_SLOTS || entry_count >= MOD_MENU_MAX_ENTRIES)
        return 0;

    entry = &entries[entry_count];
    memset (entry, 0, sizeof *entry);
    entry->group = group;
    entry->type = type;
    entry->graphic = graphic;
    copy_text (entry->help, sizeof entry->help, help);
    copy_text (entry->category, sizeof entry->category,
               (category != NULL && category[0] != '\0') ? category : "Mods");
    entry->order = order;
    entry->sequence = entry_count;
    entry->base = 0;
    entry->available = 1;
    entry->help_seen = 0;
    entry_count++;
    return 1;
}

/* Stable insertion sort over the mod slice.  Sequence numbers break ties,
   so equal categories and order values keep their load order. */
static void
sort_mod_entries (void)
{
    int i;
    int j;
    struct MOD_MENU_ENTRY key;

    for (i = base_count + 1; i < entry_count; i++) {
        key = entries[i];
        j = i - 1;
        while (j >= base_count) {
            int cmp = strcmp (entries[j].category, key.category);

            if (cmp < 0 || (cmp == 0 && entries[j].order <= key.order))
                break;
            entries[j + 1] = entries[j];
            j--;
        }
        entries[j + 1] = key;
    }
}

static int
append_page (int first, int count, int base, const char *category)
{
    struct MOD_MENU_PAGE *page;

    if (page_count >= MOD_MENU_MAX_PAGES || count <= 0)
        return 0;
    page = &pages[page_count];
    page->first = first;
    page->count = count;
    page->base = base;
    copy_text (page->category, sizeof page->category, category);
    page_count++;
    return 1;
}

int
mod_menu_build (void)
{
    int i;

    page_count = 0;
    if (base_count != MOD_MENU_SLOTS)
        return 0;
    if (entry_count > MOD_MENU_MAX_ENTRIES)
        return 0;

    if (!append_page (0, MOD_MENU_SLOTS, 1, "Base"))
        return 0;

    sort_mod_entries ();

    i = base_count;
    while (i < entry_count) {
        const char *category = entries[i].category;
        int end = i;

        while (end < entry_count
               && strcmp (entries[end].category, category) == 0)
            end++;

        while (i < end) {
            int count = end - i;

            if (count > MOD_MENU_SLOTS)
                count = MOD_MENU_SLOTS;
            if (!append_page (i, count, 0, category)) {
                page_count = 0;
                return 0;
            }
            i += count;
        }
    }
    return 1;
}

int
mod_menu_page_count (void)
{
    return page_count;
}

int
mod_menu_page_size (int page)
{
    if (page < 0 || page >= page_count)
        return 0;
    return pages[page].count;
}

const char *
mod_menu_page_category (int page)
{
    if (page < 0 || page >= page_count)
        return NULL;
    return pages[page].category;
}

int
mod_menu_page_is_base (int page)
{
    if (page < 0 || page >= page_count)
        return 0;
    return pages[page].base;
}

struct MOD_MENU_ENTRY *
mod_menu_entry (int page, int slot)
{
    if (page < 0 || page >= page_count)
        return NULL;
    if (slot < 0 || slot >= pages[page].count)
        return NULL;
    return &entries[pages[page].first + slot];
}

int
mod_menu_find_group (int group, int *page, int *slot)
{
    int p;

    for (p = 0; p < page_count; p++) {
        int s;

        for (s = 0; s < pages[p].count; s++) {
            if (entries[pages[p].first + s].group == group) {
                if (page != NULL)
                    *page = p;
                if (slot != NULL)
                    *slot = s;
                return 1;
            }
        }
    }
    return 0;
}
