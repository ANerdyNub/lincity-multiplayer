#include <stdio.h>
#include "lcconfig.h"
#include "lcstring.h"
#include "geometry.h"
#include "module_buttons.h"
#include "cliglobs.h"
#include "mouse.h"
#include "lcintl.h"
#include "lctypes.h"
#include "lintypes.h"
#include "lclib.h"
#include "lchelp.h"
#include "fileutil.h"
#include "mod_registry.h"
#include "mod_menu.h"

#define DEBUG_MODULES 0

static int module_rows;
static int module_cols;

static Mouse_Handle * mhandle;

static Rect * mbw = &scr.module_buttons;

int module_type[NUMOF_MODULES];
char *module_graphic[NUMOF_MODULES];
int module_tflag[NUMOF_MODULES];
char module_help[NUMOF_MODULES][20];

int module_help_flag[NUMOF_MODULES];

int sbut[NUMOF_MODULES];

int selected_module = 0;
int selected_module_type;
int selected_module_group;
int selected_module_cost;

static int module_page;
static int selected_page;
static int pending_module = -1;
static int pending_page = -1;

/* Type chooser state.  The help page name is mp<group>.tmp; page changes
   rewrite the same file so the 40-char help-history entry stays stable. */
#define MOD_CHOOSER_COLUMNS 3
#define MOD_CHOOSER_ROWS    8
#define MOD_CHOOSER_PER_PAGE (MOD_CHOOSER_COLUMNS * MOD_CHOOSER_ROWS)
static int chooser_group = -1;
static int chooser_page;
static int chooser_pages;

static struct MOD_MENU_ENTRY *
current_entry (int slot)
{
    return mod_menu_entry (module_page, slot);
}

static int
entry_available (struct MOD_MENU_ENTRY *entry, int slot)
{
    return entry->base ? module_tflag[slot] : entry->available;
}

static int
entry_help_seen (struct MOD_MENU_ENTRY *entry, int slot)
{
    return entry->base ? module_help_flag[slot] : entry->help_seen;
}

static void
set_entry_help_seen (struct MOD_MENU_ENTRY *entry, int slot)
{
    if (entry->base)
        module_help_flag[slot] = 1;
    else
        entry->help_seen = 1;
}

static void
entry_help_name (struct MOD_MENU_ENTRY *entry, char *name, size_t size)
{
    if (entry->base || entry->help[0] == '\0')
        snprintf (name, size, "%s", entry->help);
    else if (entry->group >= 0)
        snprintf (name, size, "mod%d.hlp", entry->group);
    else
        name[0] = '\0';
}

static void
draw_page_indicator (void)
{
    char text[32];
    int count = mod_menu_page_count ();

    if (count <= 1)
        return;
    snprintf (text, sizeof text, "%d/%d", module_page + 1, count);
    Fgl_setfontcolors (white (20), 0);
    Fgl_write (mbw->x + 4, mbw->y + mbw->h - 8, text);
    Fgl_setfontcolors (TEXT_BG_COLOUR, TEXT_FG_COLOUR);
}

static void module_choose_type (int group, int index);
static void open_type_chooser (int group, int page);

/* The selected button only has a highlight on its own page. */
static void
unhighlight_selected (void)
{
    if (selected_page == module_page && selected_module >= 0)
        unhighlight_module_button (selected_module);
}


static int
build_module_menu (void)
{
    int i;
    int group;

    mod_menu_reset ();
    for (i = 0; i < NUMOF_MODULES; i++) {
        if (!mod_menu_add_base (inv_sbut (i), module_type[i],
                                module_graphic[i], module_help[i]))
            return 0;
    }

    /* Registry groups above the base range are mod-owned.  Group 41 is the
       legacy EOF sentinel and is never registered. */
    for (group = NUM_OF_GROUPS; group < num_groups; group++) {
        char help[24];
        int type;

        if (main_groups[group].key[0] == '\0')
            continue;
        type = mod_group_default_type (group);
        if (type < 0)
            continue;
        help[0] = '\0';
        if (main_groups[group].help[0] != '\0')
            snprintf (help, sizeof help, "mod%d.hlp", group);
        if (!mod_menu_add_mod (group, type,
                               main_groups[group].button != NULL
                                   ? main_groups[group].button
                                   : main_types[type].graphic,
                               help, main_groups[group].category,
                               main_groups[group].menu_order))
            return 0;
    }
    return mod_menu_build ();
}

void
init_modules (void)
{
    int i;

    /* Get max number of rows for space allotted */
    module_rows = (mbw->h / (SELECT_BUTTON_INTERVAL));
    module_cols = (mbw->w / (SELECT_BUTTON_INTERVAL));

    /* Tell the mouse where we are */
    mhandle = mouse_register(&scr.module_buttons,&module_buttons_handler);

#if (DEBUG_MODULES)
    printf("debug_modules:\t\tr=%d\tc=%d\n",module_rows, module_cols);
#endif

    /* sbut converts a group into a column major index of the button array. */
    sbut[0] = 16;		/* buldoze */
    sbut[1] = 13;		/* powerline */
    sbut[2] = 15;		/* solar power */
    sbut[3] = 14;		/* substation */
    sbut[4] = 0;		/* residence */
    sbut[5] = 1;		/* farm */
    sbut[6] = 2;		/* market */
    sbut[7] = 19;		/* track */
    sbut[8] = 10;		/* coalmine */
    sbut[9] = 28;		/* rail */
    sbut[10] = 29;		/* coal power */
    sbut[11] = 25;		/* road */
    sbut[12] = 27;		/* light industry */
    sbut[13] = 11;		/* university */
    sbut[14] = 3;		/* commune */
    sbut[15] = 4;		/* oremine */
    sbut[16] = 5;		/* tip */
    sbut[17] = 9;		/* export */
    sbut[18] = 12;		/* heavy industry */
    sbut[19] = 6;		/* parkland */
    sbut[20] = 30;		/* recycle */
    sbut[21] = 20;		/* water */
    sbut[22] = 26;		/* health */
    sbut[23] = 31;		/* rocket */
    sbut[24] = 24;		/* windmill */
    sbut[25] = 17;		/* monument */
    sbut[26] = 21;		/* school */
    sbut[27] = 22;		/* blacksmith */
    sbut[28] = 8;		/* mill */
    sbut[29] = 18;		/* pottery */
    sbut[30] = 23;		/* fire station */
    sbut[31] = 7;		/* cricket  */


    /* load graphics and initialize help */

    module_graphic[sbut[0]] = load_graphic ("buldoze-button.csi");
    module_type[sbut[0]] = CST_GREEN;
    snprintf (module_help[sbut[0]], sizeof(module_help[sbut[0]]), "%s", "bulldoze.hlp");

    module_graphic[sbut[1]] = load_graphic ("powerline-button.csi");
    module_type[sbut[1]] = CST_POWERL_H_L;
    snprintf (module_help[sbut[1]], sizeof(module_help[sbut[1]]), "%s", "powerline.hlp");

    module_graphic[sbut[2]] = load_graphic ("powerssolar-button.csi");
    module_type[sbut[2]] = CST_POWERS_SOLAR;
    snprintf (module_help[sbut[2]], sizeof(module_help[sbut[2]]), "%s", "powerssolar.hlp");

    module_graphic[sbut[3]] = load_graphic ("substation-button.csi");
    module_type[sbut[3]] = CST_SUBSTATION_R;
    snprintf (module_help[sbut[3]], sizeof(module_help[sbut[3]]), "%s", "substation.hlp");

    module_graphic[sbut[4]] = load_graphic ("residence-button.csi");
    module_type[sbut[4]] = CST_RESIDENCE_LL;
    snprintf (module_help[sbut[4]], sizeof(module_help[sbut[4]]), "%s", "residential.hlp");

    module_graphic[sbut[5]] = load_graphic ("organic-farm-button.csi");
    module_type[sbut[5]] = CST_FARM_O0;
    snprintf (module_help[sbut[5]], sizeof(module_help[sbut[5]]), "%s", "farm.hlp");

    module_graphic[sbut[6]] = load_graphic ("market-button.csi");
    module_type[sbut[6]] = CST_MARKET_EMPTY;
    snprintf (module_help[sbut[6]], sizeof(module_help[sbut[6]]), "%s", "market.hlp");

    module_help_flag[sbut[7]] = 1;        /* No help for track */
    module_graphic[sbut[7]] = load_graphic ("track-button.csi");
    module_type[sbut[7]] = CST_TRACK_LR;
    snprintf (module_help[sbut[7]], sizeof(module_help[sbut[7]]), "%s", "track.hlp");

    module_graphic[sbut[8]] = load_graphic ("coalmine-button.csi");
    module_type[sbut[8]] = CST_COALMINE_EMPTY;
    snprintf (module_help[sbut[8]], sizeof(module_help[sbut[8]]), "%s", "coalmine.hlp");

    module_graphic[sbut[9]] = load_graphic ("rail-button.csi");
    module_type[sbut[9]] = CST_RAIL_LR;
    snprintf (module_help[sbut[9]], sizeof(module_help[sbut[9]]), "%s", "rail.hlp");

    module_graphic[sbut[10]] = load_graphic ("powerscoal-button.csi");
    module_type[sbut[10]] = CST_POWERS_COAL_EMPTY;
    snprintf (module_help[sbut[10]], sizeof(module_help[sbut[10]]), "%s", "powerscoal.hlp");

    module_graphic[sbut[11]] = load_graphic ("road-button.csi");
    module_type[sbut[11]] = CST_ROAD_LR;
    snprintf (module_help[sbut[11]], sizeof(module_help[sbut[11]]), "%s", "road.hlp");

    module_graphic[sbut[12]] = load_graphic ("industryl-button.csi");
    module_type[sbut[12]] = CST_INDUSTRY_L_C;
    snprintf (module_help[sbut[12]], sizeof(module_help[sbut[12]]), "%s", "industryl.hlp");

    module_graphic[sbut[13]] = load_graphic ("university-button.csi");
    module_type[sbut[13]] = CST_UNIVERSITY;
    snprintf (module_help[sbut[13]], sizeof(module_help[sbut[13]]), "%s", "university.hlp");

    module_graphic[sbut[14]] = load_graphic ("commune-button.csi");
    module_type[sbut[14]] = CST_COMMUNE_1;
    snprintf (module_help[sbut[14]], sizeof(module_help[sbut[14]]), "%s", "commune.hlp");

    module_graphic[sbut[15]] = load_graphic ("oremine-button.csi");
    module_type[sbut[15]] = CST_OREMINE_1;
    snprintf (module_help[sbut[15]], sizeof(module_help[sbut[15]]), "%s", "oremine.hlp");

    module_graphic[sbut[16]] = load_graphic ("tip-button.csi");
    module_type[sbut[16]] = CST_TIP_0;
    snprintf (module_help[sbut[16]], sizeof(module_help[sbut[16]]), "%s", "tip.hlp");

    module_graphic[sbut[17]] = load_graphic ("port-button.csi");
    module_type[sbut[17]] = CST_EX_PORT;
    snprintf (module_help[sbut[17]], sizeof(module_help[sbut[17]]), "%s", "port.hlp");

    module_graphic[sbut[18]] = load_graphic ("industryh-button.csi");
    module_type[sbut[18]] = CST_INDUSTRY_H_C;
    snprintf (module_help[sbut[18]], sizeof(module_help[sbut[18]]), "%s", "industryh.hlp");

    module_graphic[sbut[19]] = load_graphic ("parkland-button.csi");
    module_type[sbut[19]] = CST_PARKLAND_PLANE;
    snprintf (module_help[sbut[19]], sizeof(module_help[sbut[19]]), "%s", "park.hlp");

    module_graphic[sbut[20]] = load_graphic ("recycle-button.csi");
    module_type[sbut[20]] = CST_RECYCLE;
    snprintf (module_help[sbut[20]], sizeof(module_help[sbut[20]]), "%s", "recycle.hlp");

    module_graphic[sbut[21]] = load_graphic ("water-button.csi");
    module_type[sbut[21]] = CST_WATER;
    snprintf (module_help[sbut[21]], sizeof(module_help[sbut[21]]), "%s", "river.hlp");

    module_graphic[sbut[22]] = load_graphic ("health-button.csi");
    module_type[sbut[22]] = CST_HEALTH;
    snprintf (module_help[sbut[22]], sizeof(module_help[sbut[22]]), "%s", "health.hlp");

    module_graphic[sbut[23]] = load_graphic ("rocket-button.csi");
    module_type[sbut[23]] = CST_ROCKET_1;
    snprintf (module_help[sbut[23]], sizeof(module_help[sbut[23]]), "%s", "rocket.hlp");

    module_graphic[sbut[24]] = load_graphic ("windmill-button.csi");
    module_type[sbut[24]] = CST_WINDMILL_1_R;
    snprintf (module_help[sbut[24]], sizeof(module_help[sbut[24]]), "%s", "windmill.hlp");

    module_graphic[sbut[25]] = load_graphic ("monument-button.csi");
    module_type[sbut[25]] = CST_MONUMENT_0;
    snprintf (module_help[sbut[25]], sizeof(module_help[sbut[25]]), "%s", "monument.hlp");

    module_graphic[sbut[26]] = load_graphic ("school-button.csi");
    module_type[sbut[26]] = CST_SCHOOL;
    snprintf (module_help[sbut[26]], sizeof(module_help[sbut[26]]), "%s", "school.hlp");

    module_graphic[sbut[27]] = load_graphic ("blacksmith-button.csi");
    module_type[sbut[27]] = CST_BLACKSMITH_0;
    snprintf (module_help[sbut[27]], sizeof(module_help[sbut[27]]), "%s", "blacksmith.hlp");

    module_graphic[sbut[28]] = load_graphic ("mill-button.csi");
    module_type[sbut[28]] = CST_MILL_0;
    snprintf (module_help[sbut[28]], sizeof(module_help[sbut[28]]), "%s", "mill.hlp");

    module_graphic[sbut[29]] = load_graphic ("pottery-button.csi");
    module_type[sbut[29]] = CST_POTTERY_0;
    snprintf (module_help[sbut[29]], sizeof(module_help[sbut[29]]), "%s", "pottery.hlp");

    module_graphic[sbut[30]] = load_graphic ("firestation-button.csi");
    module_type[sbut[30]] = CST_FIRESTATION_1;
    snprintf (module_help[sbut[30]], sizeof(module_help[sbut[30]]), "%s", "firestation.hlp");

    module_graphic[sbut[31]] = load_graphic ("cricket-button.csi");
    module_type[sbut[31]] = CST_CRICKET_1;
    snprintf (module_help[sbut[31]], sizeof(module_help[sbut[31]]), "%s", "cricket.hlp");



    /* disable all the buttons */
    for (i = 0; i < NUMOF_MODULES; i++) {
	module_tflag[i] = 0;
    }

    if (!build_module_menu ()) {
        int j;

        fprintf (stderr, "modules: cannot build mod menu pages; "
                         "showing the base page only\n");
        mod_menu_reset ();
        for (j = 0; j < NUMOF_MODULES; j++)
            mod_menu_add_base (inv_sbut (j), module_type[j],
                               module_graphic[j], module_help[j]);
        mod_menu_build ();
    }

    module_page = 0;
    selected_page = 0;
    selected_module = sbut[7];
    /* GCS: Move to later in initialization because I don't want to draw_main_window_box() yet */
#if defined (commentout)
    set_selected_module (CST_TRACK_LR);
#endif
}


/* update_avail_modules: Determine which modules are available, based on
   tech level */

void
update_avail_modules (int popup)
{
    int page;
    int slot;

    for (page = 0; page < mod_menu_page_count (); page++) {
	int count = mod_menu_page_size (page);

	for (slot = 0; slot < count; slot++)
	{
	    struct MOD_MENU_ENTRY *entry = mod_menu_entry (page, slot);
	    int *flag = entry->base ? &module_tflag[slot] : &entry->available;
	    int f = *flag;
	    int g = entry->group;

	    if (tech_level >= main_groups[g].tech * MAX_TECH_LEVEL/1000)
	    {
		if (*flag == 0)
		    if (popup && entry->base)
			activate_module (slot);
		f = 1;
	    }
	    else if (*flag != 0 &&
		     tech_level
		     < ((main_groups[g].tech - (main_groups[g].tech/10)) * MAX_TECH_LEVEL/1000) )
		f = 0;
	    if (*flag != f)
	    {
		*flag = f;
		if (page == module_page)
		    draw_module (slot, entry->graphic);
	    }
	}
    }
    /* XXX: Why is this here? Should be with rest of tech gained messages! */
    /* GCS: Because the modern windmill tech reuses the same module number
       as the primative windmill? */
    if (tech_level > MODERN_WINDMILL_TECH && modern_windmill_flag == 0)
    {
	ok_dial_box ("mod_wind_up.mes", GOOD, 0L);
	modern_windmill_flag = 1;
    }
}

/* Display message for module when it is activated (see above) */

void
activate_module (int module)
{
    module = inv_sbut (module);

    if (module == GROUP_WINDMILL)
	ok_dial_box ("windmillup.mes", GOOD, 0L);
    else if (module == GROUP_COAL_POWER)
	ok_dial_box ("coalpowerup.mes", GOOD, 0L);
    /*    else if (module == (GROUP_SOLAR_POWER - 1)) */
    else if (module == GROUP_SOLAR_POWER)
	ok_dial_box ("solarpowerup.mes", GOOD, 0L);
    else if (module == GROUP_COALMINE)
	ok_dial_box ("coalmineup.mes", GOOD, 0L);
    else if (module == GROUP_RAIL)
	ok_dial_box ("railwayup.mes", GOOD, 0L);
    else if (module == GROUP_ROAD)
	ok_dial_box ("roadup.mes", GOOD, 0L);
    else if (module == GROUP_INDUSTRY_L)
	ok_dial_box ("ltindustryup.mes", GOOD, 0L);
    else if (module == GROUP_UNIVERSITY)
	ok_dial_box ("universityup.mes", GOOD, 0L);
    else if (module == GROUP_OREMINE)
    {
	if (GROUP_OREMINE_TECH > 0)
	    ok_dial_box ("oremineup.mes", GOOD, 0L);
    }
    else if (module == GROUP_PORT)	/* exports are the same */
	ok_dial_box ("import-exportup.mes", GOOD, 0L);
    else if (module == GROUP_INDUSTRY_H)
	ok_dial_box ("hvindustryup.mes", GOOD, 0L);
    else if (module == GROUP_PARKLAND)
    {
	if (GROUP_PARKLAND_TECH > 0)
	    ok_dial_box ("parkup.mes", GOOD, 0L);
    }
    else if (module == GROUP_RECYCLE)
	ok_dial_box ("recycleup.mes", GOOD, 0L);
    else if (module == GROUP_RIVER)
    {
	if (GROUP_WATER_TECH > 0)
	    ok_dial_box ("riverup.mes", GOOD, 0L);
    }
    else if (module == GROUP_HEALTH)
	ok_dial_box ("healthup.mes", GOOD, 0L);
    else if (module == GROUP_ROCKET)
	ok_dial_box ("rocketup.mes", GOOD, 0L);
    else if (module == GROUP_SCHOOL)
    {
	if (GROUP_SCHOOL_TECH > 0)
	    ok_dial_box ("schoolup.mes", GOOD, 0L);
    }
    else if (module == GROUP_BLACKSMITH)
    {
	if (GROUP_BLACKSMITH_TECH > 0)
	    ok_dial_box ("blacksmithup.mes", GOOD, 0L);
    }
    else if (module == GROUP_MILL)
    {
	if (GROUP_MILL_TECH > 0)
	    ok_dial_box ("millup.mes", GOOD, 0L);
    }
    else if (module == GROUP_POTTERY)
    {
	if (GROUP_POTTERY_TECH > 0)
	    ok_dial_box ("potteryup.mes", GOOD, 0L);
    }
    else if (module == GROUP_FIRESTATION)
	ok_dial_box ("firestationup.mes", GOOD, 0L);
    else if (module == GROUP_CRICKET)
	ok_dial_box ("cricketup.mes", GOOD, 0L);
}
    

/* handle_module_buttons:  mouse handler for module window */

void 
module_buttons_handler (int x, int y, int mbutton)
{
    int module;
    int row, col;
    int count = mod_menu_page_size (module_page);


    /* Figure out which row and column the click came in */
    row = (y / (SELECT_BUTTON_INTERVAL));
    if ((y % SELECT_BUTTON_INTERVAL) <= 8) 
      return;  /* in the gap */
    
    col = (x / (SELECT_BUTTON_INTERVAL));
    if ((x % SELECT_BUTTON_INTERVAL) <= 8) 
      return;
    
    module = row + (module_rows * col);
    if (module < 0 || module >= count)
	return;

    select_module (module, mbutton);
}


/* select_module: low level click handler;  display help or change to
   module, depending on mbutton (right or left click) */

void
select_module (int module, int mbutton)
{
    struct MOD_MENU_ENTRY *entry = current_entry (module);
    char help_name[MOD_MENU_HELP_LEN + 1];

    if (entry == NULL)
	return;
    if (!entry_available (entry, module) && mbutton != LC_MOUSE_RIGHTBUTTON) {
	ok_dial_box ("not_enough_tech.mes", BAD, 0L);
	return;
    }

    entry_help_name (entry, help_name, sizeof help_name);

    if (mbutton == LC_MOUSE_RIGHTBUTTON) {
	if (help_name[0] != '\0')
	    activate_help (help_name);
	return;
    }

    if (help_name[0] != '\0'
	&& !entry_help_seen (entry, module)
	&& !suppress_firsttime_module_help)
    {
	activate_help (help_name);
	set_entry_help_seen (entry, module);
	pending_module = module;
	pending_page = module_page;
	/* Deselect current module while help is shown */
	unhighlight_selected ();
	selected_page = -1;
	selected_module = -1;
	set_selected_module (CST_GREEN);
	return;
    }

    /* Mod groups with more than one type open a chooser.  Base groups keep
       their original default-type behaviour. */
    if (!entry->base && mod_group_type_count (entry->group) > 1) {
	open_type_chooser (entry->group, 0);
	return;
    }

    unhighlight_selected ();
    selected_page = module_page;
    selected_module = module;
    highlight_module_button (module);

    set_selected_module (entry->type);
}


/* highlight_module_button: draw a nice border around a selected module */

void
highlight_module_button (int module)
{
    int x, y;
    int row, col;

    if (selected_page != module_page || module < 0
        || module >= mod_menu_page_size (module_page))
        return;

    row = module % module_rows;
    col = module / module_rows;

    x = 8 + (col * 24) + mbw->x;
    y = 8 + (row * 24) + mbw->y;

    hide_mouse ();
    draw_small_bezel(x + 1, y + 1,
		     SELECT_BUTTON_WIDTH - 2, SELECT_BUTTON_WIDTH - 2,
		     yellow(0));
    redraw_mouse ();
}


/* unhighlight_module_button: replace highlight border with regular one */

void
unhighlight_module_button (int module)
{
    int x, y;

    int row, col;

    if (module < 0 || module >= mod_menu_page_size (module_page))
        return;

    row = module % module_rows;
    col = module / module_rows;

    x = 8 + (col * 24) + mbw->x;
    y = 8 + (row * 24) + mbw->y;

    hide_mouse ();
    draw_small_bezel(x + 1, y + 1, 
		     SELECT_BUTTON_WIDTH - 2, SELECT_BUTTON_WIDTH - 2,
		     blue(0));
    redraw_mouse ();
}


/* draw_selected_module_cost: write info about the module to the status area */

void
draw_selected_module_cost ()
{
    Rect* b = &scr.select_message;
    char s[100];
    char coststr[20];
    char bldzstr[20];

    selected_module_cost = get_type_cost (selected_module_type);

    commify(coststr,20,selected_module_cost);
    commify(bldzstr,20,main_groups[selected_module_group].bul_cost);

    if (selected_module_group == GROUP_BARE) 
	snprintf (s, 100, _("Bulldoze - cost varies"));
    else
	snprintf (s, 100, _("%s %s  Bulldoze %s"),
		  _(main_groups[selected_module_group].name),
		  coststr, bldzstr);

    Fgl_fillbox (b->x, b->y, 42 * 8, 8, TEXT_BG_COLOUR);
    Fgl_write (b->x, b->y, s);
}


/* draw_modules:  fill a box and then draw all graphics */

void
draw_modules (void)
{
    int i;
    int count = mod_menu_page_size (module_page);

    Fgl_fillbox (mbw->x, mbw->y, mbw->w, mbw->h, white (20));

    for (i = 0; i < count; i++) {
        struct MOD_MENU_ENTRY *entry = mod_menu_entry (module_page, i);

        if (entry != NULL)
            draw_module (i, entry->graphic);
    }

    draw_page_indicator ();
    if (selected_page == module_page)
        highlight_module_button (selected_module);
}

void
draw_module (int module, char *graphic)
{
    int x, y, xx, yy;
    int row, col;
    struct MOD_MENU_ENTRY *entry = current_entry (module);

    if (entry == NULL)
        return;

    row = module % module_rows;
    col = module / module_rows;

    x = 8 + (col * 24);
    y = 8 + (row * 24);

    hide_mouse ();

    if (graphic != NULL)
        Fgl_putbox (x + mbw->x, y + mbw->y, 16, 16, graphic);
    unhighlight_module_button (module);

    /* Modules we don't have enough tech for are greyed out. */
    if (!entry_available (entry, module))
    {
	for (yy = -3; yy < 19; yy++)
	    for (xx = -3; xx < 19; xx += 2)
		Fgl_setpixel (x + xx + (yy % 2) + mbw->x,
			      y + yy + mbw->y, white (15));
    }
    redraw_mouse ();
}


/* inv_sbut:  the order and the number of the module aren't the same; 
   inv_sbut converts between a geographical location and an array location */

int
inv_sbut (int button)
{
    int i, j;
    for (i = 0; i < 32; i++)	
	if (sbut[i] == button)
	{
	    j = i;
	    return (j);
	}
    /* XXX: do we need this debug stuff? */
    /* GCS: This is a fatal error.  Probably OK. */
    printf ("Button=%d\n", button);
    for (i = 0; i < 32; i++)
	printf ("%5d", sbut[i]);
    printf ("\n");
    do_error ("An inv_sbut error has happened.");
    return (-1);			/* can't get here */
}

void
check_pending_module (void)
{
    if (pending_module >= 0 && !help_flag) {
	int m = pending_module;
	int p = pending_page;
	pending_module = -1;
	pending_page = -1;
	if (p >= 0 && p < mod_menu_page_count ())
	    module_page = p;
	select_module (m, LC_MOUSE_LEFTBUTTON);
    }
}

/* Change the visible build-menu page.  Page 0 stays the base page. */
void
module_change_page (int delta)
{
    int count = mod_menu_page_count ();

    if (count <= 1 || delta == 0)
        return;
    module_page += delta;
    while (module_page < 0)
        module_page += count;
    module_page %= count;
    draw_modules ();
}

/* Switch to the page holding group and select its button. */
void
module_select_group (int group)
{
    int page;
    int slot;

    if (!mod_menu_find_group (group, &page, &slot))
        return;
    module_page = page;
    selected_page = page;
    selected_module = slot;
    draw_modules ();
}

static void
chooser_name (int group, char *name, size_t size)
{
    snprintf (name, size, "mp%d.tmp", group);
}

static void
write_type_chooser (int group, int page)
{
    char path[LC_PATH_MAX];
    char name[24];
    FILE *fp;
    int first = page * MOD_CHOOSER_PER_PAGE;
    int i;

    chooser_name (group, name, sizeof name);
    snprintf (path, sizeof path, "%s%c%s", lc_save_dir, PATH_SLASH, name);
    fp = fopen (path, "w");
    if (fp == NULL) {
        do_error ("Can't write mod type chooser");
        return;
    }

    fprintf (fp, "text -1 12 Choose a type for %s\n",
             _(main_groups[group].name));
    fprintf (fp, "text -1 28 =====================================\n");

    for (i = 0; i < MOD_CHOOSER_PER_PAGE; i++) {
        int type = mod_group_type_at (group, first + i);
        char label[TYPE_NAME_LEN + 1];
        char tile[16];
        int col;
        int row;
        int x;
        int y;

        if (type < 0)
            break;
        col = i % MOD_CHOOSER_COLUMNS;
        row = i / MOD_CHOOSER_COLUMNS;
        x = 24 + col * 140;
        y = 50 + row * 38;
        snprintf (tile, sizeof tile, "mptile%d", i);
        if (main_types[type].graphic != NULL) {
            help_register_tile (tile, main_types[type].graphic);
            fprintf (fp, "icon %d %d %s\n", x, y, tile);
        }
        fprintf (fp, "button %d %d 18 18 return%d\n", x, y, i + 1);
        get_type_name ((short) type, label);
        label[15] = '\0';
        fprintf (fp, "tbutton %d %d return%d %s\n", x + 22, y + 3,
                 i + 1, label);
    }

    if (chooser_pages > 1) {
        if (page > 0)
            fprintf (fp, "tbutton 8 368 return98 < Prev\n");
        fprintf (fp, "text 170 370 Page %d/%d\n", page + 1, chooser_pages);
        if (page + 1 < chooser_pages)
            fprintf (fp, "tbutton 390 368 return99 Next >\n");
    }

    fclose (fp);
}

static void
open_type_chooser (int group, int page)
{
    char name[24];
    int count = mod_group_type_count (group);

    if (count <= 0)
        return;
    if (count == 1) {
        module_choose_type (group, 0);
        return;
    }
    chooser_group = group;
    chooser_pages = (count + MOD_CHOOSER_PER_PAGE - 1)
        / MOD_CHOOSER_PER_PAGE;
    if (page < 0)
        page = 0;
    if (page >= chooser_pages)
        page = chooser_pages - 1;
    chooser_page = page;
    write_type_chooser (group, page);
    chooser_name (group, name, sizeof name);
    block_help_exit = 1;
    activate_help (name);
}

/* Called by lchelp.c when a chooser page returns.  Returns 0 when the page
   is not ours, 1 when help should close, and 2 when the page was rewritten
   and should be drawn again. */
int
module_chooser_page_result (const char *page, int value)
{
    int i;
    int parsed = 0;
    int next;

    if (page == NULL || strncmp (page, "mp", 2) != 0)
        return 0;
    for (i = 2; page[i] >= '0' && page[i] <= '9'; i++)
        parsed = parsed * 10 + (page[i] - '0');
    if (i == 2 || strcmp (page + i, ".tmp") != 0)
        return 0;
    if (chooser_group < 0 || parsed != chooser_group)
        return 1;

    if (value >= 1 && value <= MOD_CHOOSER_PER_PAGE) {
        int index = chooser_page * MOD_CHOOSER_PER_PAGE + value - 1;

        if (index < mod_group_type_count (chooser_group))
            module_choose_type (chooser_group, index);
        return 1;
    }
    if (value == 98 || value == 99) {
        next = chooser_page + (value == 99 ? 1 : -1);
        if (next < 0 || next >= chooser_pages)
            return 1;
        chooser_page = next;
        write_type_chooser (chooser_group, chooser_page);
        return 2;
    }
    return 1;
}

static void
module_choose_type (int group, int index)
{
    int type = mod_group_type_at (group, index);
    int page;
    int slot;

    if (type < 0)
        return;
    if (mod_menu_find_group (group, &page, &slot)) {
        module_page = page;
        selected_page = page;
        selected_module = slot;
    }
    set_selected_module (type);
    draw_modules ();
}

void
set_selected_module (int type)
{
    selected_module_type = type;
    selected_module_group = get_group_of_type(selected_module_type);
    if (selected_module_type == CST_RESIDENCE_LL) {
	choose_residence ();
    }

#ifdef LC_X11  /* XXX: WCK: shouldn't be any platform specific code here */
    if (selected_module_group == GROUP_BARE) 
	XDefineCursor (display.dpy, display.win, pirate_cursor);
    else
	XDefineCursor (display.dpy, display.win, None);
#endif

    draw_selected_module_cost();

    if (selected_module_type == CST_GREEN) {
	draw_main_window_box (red (8));
    } else {
	draw_main_window_box (green (8));
	monument_bul_flag = 0;
	river_bul_flag = 0;
	shanty_bul_flag = 0;
    }


    selected_module_type = type;
    selected_module_group = get_group_of_type(selected_module_type);
    draw_selected_module_cost ();   /* sets module cost */
}
