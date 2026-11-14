/* ---------------------------------------------------------------------- *
 * lintypes.h
 * This file is part of lincity.
 * Lincity is copyright (c) I J Peters 1995-1997, (c) Greg Sharp 1997-2001.
 * ---------------------------------------------------------------------- */
#ifndef __lintypes_h__
#define __lintypes_h__

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* Fixed base-game bounds.  These remain the limits for base type IDs and
   for the size of the legacy base table. */
#define NUM_OF_TYPES    400
#define NUM_OF_GROUPS    42
#define GROUP_NAME_LEN   20
#define GROUP_CATEGORY_LEN 32
#define GROUP_HELP_PATH_LEN 1024
#define TYPE_NAME_LEN    64
#define MOD_KEY_LEN      160
#define MOD_OWNER_LEN    64

/* Registry capacity.  The base game only uses the first NUM_OF_GROUPS
   groups and type IDs below NUM_OF_TYPES; mods register into the spare
   slots.  Runtime code must use num_groups/num_types, not these limits. */
#define MAX_GROUPS      256
#define MAX_TYPES       1024

struct GROUP
{
    char  name[GROUP_NAME_LEN+1];      /* name of group */
    unsigned short        no_credit;   /* TRUE if need credit to build */
    unsigned short        group;       /* This is redundant: it must match
					  the index into the table */
    unsigned short        size;
    int   colour;         /* summary map colour */
    int   cost_mul;       /* group cost multiplier */
    int   bul_cost;       /* group bulldoze cost */
    int   fire_chance;    /* probability of fire */
    int   cost;           /* group cost */
    int   tech;           /* group cost */

    /* Registry metadata, filled by mod_registry.c. */
    unsigned int traits;
    void (*simulate)(int x, int y);
    void (*mps)(int x, int y);
    void (*update_tech)(int x, int y);
    char key[MOD_KEY_LEN + 1];
    char owner[MOD_OWNER_LEN + 1];

    /* Build-menu metadata.  Page 0 supplies its own choices and ignores
       these fields; mod pages read them through mod_menu.c. */
    char category[GROUP_CATEGORY_LEN + 1];
    int menu_order;
    int default_type;
    char *button;                  /* 16x16 palette tile, or NULL */
    char help[GROUP_HELP_PATH_LEN + 1];
};

struct TYPE
{
    int group;          /* What group does this type belong to? -1 if unset */
    char* graphic;      /* Bitmap of the graphic */
    char name[TYPE_NAME_LEN + 1];
    char key[MOD_KEY_LEN + 1];
    char owner[MOD_OWNER_LEN + 1];
};

extern int num_groups;
extern int num_types;

int get_group_of_type (short selected_type);
void set_map_groups (void);
int get_type_cost (short type);
int get_group_cost (short group);
void get_type_name (short type, char * s);

/* Left unsized so unit tests can provide their own fixed-size arrays. */
extern struct GROUP main_groups[];
extern struct TYPE main_types[];

#endif /* __lintypes_h__ */
