/* ---------------------------------------------------------------------- *
 * ldsvguts.c
 * This file is part of lincity.
 * Lincity is copyright (c) I J Peters 1995-1997, (c) Greg Sharp 1997-2001.
 * ---------------------------------------------------------------------- */
#include "lcconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include "lcstring.h"
#include "lcintl.h"
#include "screen.h"
#include "mouse.h"
#include "module_buttons.h"
#include "stats.h"
#include "save_parse.h"

#include <fcntl.h>
#include <sys/types.h>

#if defined (TIME_WITH_SYS_TIME)
#include <time.h>
#include <sys/time.h>
#else
#if defined (HAVE_SYS_TIME_H)
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif

#if defined (WIN32)
#include <winsock.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#endif

#ifdef __EMX__
#define chown(x,y,z)
#endif

#if defined (HAVE_DIRENT_H)
#include <dirent.h>
#define NAMLEN(dirent) strlen((dirent)->d_name)
#else
#define dirent direct
#define NAMLEN(dirent) (dirent)->d_namlen
#if defined (HAVE_SYS_NDIR_H)
#include <sys/ndir.h>
#endif
#if defined (HAVE_SYS_DIR_H)
#include <sys/dir.h>
#endif
#if defined (HAVE_NDIR_H)
#include <ndir.h>
#endif
#endif

#include <ctype.h>
#include "common.h"
#ifdef LC_X11
#include <X11/cursorfont.h>
#endif
#include "lctypes.h"
#include "lin-city.h"
#include "cliglobs.h"
#include "engglobs.h"
#include "fileutil.h"
#include "power.h"
#include "pbar.h"
#include "stats.h"
#include "validation.h"
#include "save_mods.h"

#if defined (WIN32) && !defined (NDEBUG)
#define START_FAST_SPEED 1
#define SKIP_OPENING_SCENE 1
#endif

#define SI_BLACK 252
#define SI_RED 253
#define SI_GREEN 254
#define SI_YELLOW 255

#define MP_SANITY_CHECK 1

/* ---------------------------------------------------------------------- *
 * Private Fn Prototypes
 * ---------------------------------------------------------------------- */
void dump_screen (void);
int verify_city (char *cname);

/* ---------------------------------------------------------------------- *
 * Private Global Variables
 * ---------------------------------------------------------------------- */

char save_names[10][42];

/* ---------------------------------------------------------------------- *
 * Public functions
 * ---------------------------------------------------------------------- */
void
remove_scene (char *cname)
{
    char *s;
    int l;
    if ((l = strlen (cname)) < 2)	/* Thanks to Chris J. Kiick */
	return;

    if ((s = (char *) malloc (lc_save_dir_len + l + 16)) == 0)
	malloc_failure ();
    snprintf (s, lc_save_dir_len + l + 16, "%s%c%s", lc_save_dir, PATH_SLASH, cname);
    remove (s);
    free (s);
}

void
save_city_raw (char *cname)
{
    int x, y, z, n, p;
    gzFile ofile = gzopen (cname, "wb");
    if (ofile == NULL) {
	printf (_("Save file <%s> - "), cname);
	do_error (_("Can't open save file!"));
    }

    gzprintf (ofile, "%d\n", (int) VERSION_INT);
    {
	char *mod_header = save_mod_header_format ();

	if (mod_header == NULL || gzputs (ofile, mod_header) < 0) {
	    free (mod_header);
	    gzclose (ofile);
	    do_error (_("Can't write the mod save header!"));
	}
	free (mod_header);
    }
    prog_box (_("Saving scene"), 0);
    check_endian ();
    for (x = 0; x < WORLD_SIDE_LEN; x++) {
	for (y = 0; y < WORLD_SIDE_LEN; y++) {
	    for (z = 0; z < sizeof(int); z++) {
		n = *(((unsigned char *) &MP_INFO(x,y).population) + z);
		gzprintf (ofile, "%d\n", n);
	    }
	    for (z = 0; z < sizeof(int); z++) {
		n = *(((unsigned char *) &MP_INFO(x,y).flags) + z);
		gzprintf (ofile, "%d\n", n);
	    }
	    for (z = 0; z < sizeof(unsigned short); z++) {
		n = *(((unsigned char *) &MP_INFO(x,y).coal_reserve) + z);
		gzprintf (ofile, "%d\n", n);
	    }
	    for (z = 0; z < sizeof(unsigned short); z++) {
		n = *(((unsigned char *) &MP_INFO(x,y).ore_reserve) + z);
		gzprintf (ofile, "%d\n", n);
	    }
	    for (z = 0; z < sizeof(int); z++) {
		n = *(((unsigned char *) &MP_INFO(x,y).int_1) + z);
		gzprintf (ofile, "%d\n", n);
	    }
	    for (z = 0; z < sizeof(int); z++) {
		n = *(((unsigned char *) &MP_INFO(x,y).int_2) + z);
		gzprintf (ofile, "%d\n", n);
	    }
	    for (z = 0; z < sizeof(int); z++) {
		n = *(((unsigned char *) &MP_INFO(x,y).int_3) + z);
		gzprintf (ofile, "%d\n", n);
	    }
	    for (z = 0; z < sizeof(int); z++) {
		n = *(((unsigned char *) &MP_INFO(x,y).int_4) + z);
		gzprintf (ofile, "%d\n", n);
	    }
	    for (z = 0; z < sizeof(int); z++) {
		n = *(((unsigned char *) &MP_INFO(x,y).int_5) + z);
		gzprintf (ofile, "%d\n", n);
	    }
	    for (z = 0; z < sizeof(int); z++) {
		n = *(((unsigned char *) &MP_INFO(x,y).int_6) + z);
		gzprintf (ofile, "%d\n", n);
	    }
	    for (z = 0; z < sizeof(int); z++) {
		n = *(((unsigned char *) &MP_INFO(x,y).int_7) + z);
		gzprintf (ofile, "%d\n", n);
	    }
	    gzprintf (ofile, "%d\n", (int) MP_POL(x,y));
	    gzprintf (ofile, "%d\n", (int) MP_TYPE(x,y));
	}
	prog_box ("", (90 * x) / WORLD_SIDE_LEN);
    }
    check_endian ();		/* we have to put the byte order back. */

    gzprintf (ofile, "%d\n", main_screen_originx);
    gzprintf (ofile, "%d\n", main_screen_originy);
    gzprintf (ofile, "%d\n", total_time);
    for (x = 0; x < MAX_NUMOF_SUBSTATIONS; x++)
    {
	gzprintf (ofile, "%d\n", substationx[x]);
	gzprintf (ofile, "%d\n", substationy[x]);
    }
    prog_box ("", 92);
    gzprintf (ofile, "%d\n", numof_substations);
    for (x = 0; x < MAX_NUMOF_MARKETS; x++)
    {
	gzprintf (ofile, "%d\n", marketx[x]);
	gzprintf (ofile, "%d\n", markety[x]);
    }
    prog_box ("", 94);
    gzprintf (ofile, "%d\n", numof_markets);
    gzprintf (ofile, "%d\n", people_pool);
    gzprintf (ofile, "%d\n", total_money);
    gzprintf (ofile, "%d\n", income_tax_rate);
    gzprintf (ofile, "%d\n", coal_tax_rate);
    gzprintf (ofile, "%d\n", dole_rate);
    gzprintf (ofile, "%d\n", transport_cost_rate);
    gzprintf (ofile, "%d\n", goods_tax_rate);
    gzprintf (ofile, "%d\n", export_tax);
    gzprintf (ofile, "%d\n", export_tax_rate);
    gzprintf (ofile, "%d\n", import_cost);
    gzprintf (ofile, "%d\n", import_cost_rate);
    gzprintf (ofile, "%d\n", tech_level);
    gzprintf (ofile, "%d\n", tpopulation);
    gzprintf (ofile, "%d\n", tstarving_population);
    gzprintf (ofile, "%d\n", tunemployed_population);
    gzprintf (ofile, "%d\n", 0); /* waste_goods is obsolete */
    gzprintf (ofile, "%d\n", power_made);
    gzprintf (ofile, "%d\n", power_used);
    gzprintf (ofile, "%d\n", coal_made);
    gzprintf (ofile, "%d\n", coal_used);
    gzprintf (ofile, "%d\n", goods_made);
    gzprintf (ofile, "%d\n", goods_used);
    gzprintf (ofile, "%d\n", ore_made);
    gzprintf (ofile, "%d\n", ore_used);
    gzprintf (ofile, "%d\n", 0); /* Removed diff_old_population, version 1.12 */

    prog_box ("", 96);
    /* Changed, version 1.12 */
    gzprintf (ofile, "%d\n", monthgraph_size);
    for (x = 0; x < monthgraph_size; x++) {
	gzprintf (ofile, "%d\n", monthgraph_pop[x]);
	gzprintf (ofile, "%d\n", monthgraph_starve[x]);
	gzprintf (ofile, "%d\n", monthgraph_nojobs[x]);
	gzprintf (ofile, "%d\n", monthgraph_ppool[x]);
#if defined (commentout)
	gzprintf (ofile, "%d\n", diffgraph_power[x]);
	gzprintf (ofile, "%d\n", diffgraph_coal[x]);
	gzprintf (ofile, "%d\n", diffgraph_goods[x]);
	gzprintf (ofile, "%d\n", diffgraph_ore[x]);
	gzprintf (ofile, "%d\n", diffgraph_population[x]);
#endif
    }
    prog_box ("", 98);
    gzprintf (ofile, "%d\n", rockets_launched);
    gzprintf (ofile, "%d\n", rockets_launched_success);
    gzprintf (ofile, "%d\n", coal_survey_done);
    for (x = 0; x < PBAR_DATA_SIZE; x++)
	for (p = 0; p < NUM_PBARS; p++)
	    gzprintf(ofile, "%d\n", pbars[p].data[x]);

    prog_box ("", 99);

    for (p = 0; p < NUM_PBARS; p++) {
	gzprintf(ofile, "%d\n", pbars[p].oldtot);
	gzprintf(ofile, "%d\n", pbars[p].diff);
    }

    gzprintf (ofile, "%d\n", cheat_flag);
    gzprintf (ofile, "%d\n", total_pollution_deaths);
    gzprintf (ofile, "%f\n", pollution_deaths_history);
    gzprintf (ofile, "%d\n", total_starve_deaths);
    gzprintf (ofile, "%f\n", starve_deaths_history);
    gzprintf (ofile, "%d\n", total_unemployed_years);
    gzprintf (ofile, "%f\n", unemployed_history);
    gzprintf (ofile, "%d\n", max_pop_ever);
    gzprintf (ofile, "%d\n", total_evacuated);
    gzprintf (ofile, "%d\n", total_births);
    for (x = 0; x < NUMOF_MODULES; x++)
	gzprintf (ofile, "%d\n", module_help_flag[x]);
    gzprintf (ofile, "%d\n", 0);	/* dummy values */

    gzprintf (ofile, "%d\n", 0);	/* backward compatibility */

    if (strlen (given_scene) > 1)
	gzprintf (ofile, "%s\n", given_scene);
    else
	gzprintf (ofile, "dummy\n");	/* 1 */

    gzprintf (ofile, "%d\n", highest_tech_level);	/* 2 */

    gzprintf (ofile, "sust %d %d %d %d %d %d %d %d %d %d\n"
	     ,sust_dig_ore_coal_count, sust_port_count
	     ,sust_old_money_count, sust_old_population_count
	     ,sust_old_tech_count, sust_fire_count
	     ,sust_old_money, sust_old_population, sust_old_tech
	     ,sustain_flag);	/* 3 */

    gzprintf (ofile, "dummy\n");	/* 4 */

    gzprintf (ofile, "dummy\n");	/* 5 */

    gzprintf (ofile, "dummy\n");	/* 6 */

    gzprintf (ofile, "dummy\n");	/* 7 */

    gzprintf (ofile, "dummy\n");	/* 8 */

    gzprintf (ofile, "dummy\n");	/* 9 */

    gzprintf (ofile, "dummy\n");	/* 10 */

    if (gzclose (ofile) != Z_OK)
	do_error ("Can't close save file!");
    prog_box ("", 100);
}

/* Write a gzip member to a sibling temp file and rename it into place.  This
   keeps a failed write from destroying the previous scene and avoids putting
   the user's path through a shell. */
static void
save_city_atomic (const char *cname, const char *tmp)
{
    save_city_raw ((char *) tmp);
    if (rename (tmp, cname) != 0) {
	remove (tmp);
	do_error ("Can't replace the save file!");
    }
}

void
save_city_to_path (char *cname)
{
    size_t l = strlen (cname);
    char *tmp = (char *) malloc (l + 8);

    if (tmp == NULL)
	malloc_failure ();
    snprintf (tmp, l + 8, "%s.tmp", cname);
    save_city_atomic (cname, tmp);
    free (tmp);
}

void
save_city (char *cname)
{
    char *s, *s2;
    int l;

    if ((l = strlen (cname)) < 2)
	return;
    if ((s = (char *) malloc (lc_save_dir_len + l + 16)) == 0)
	malloc_failure ();
    if ((s2 = (char *) malloc (lc_save_dir_len + l + 32)) == 0)
	malloc_failure ();

    snprintf (s, lc_save_dir_len + l + 16, "%s%c%s", lc_save_dir, PATH_SLASH, cname);
    snprintf (s2, lc_save_dir_len + l + 32, "%s%c%s", lc_save_dir, PATH_SLASH, "tmp-save");
    save_city_atomic (s, s2);

    free (s);
    free (s2);
}

#define LOAD_INT(dst) do { \
    if (fscanf (ofile, "%d", &(dst)) != 1) goto load_corrupt; \
} while (0)
#define LOAD_FLOAT(dst) do { \
    if (fscanf (ofile, "%f", &(dst)) != 1) goto load_corrupt; \
} while (0)

/* Everything outside the map that load_city() overwrites before it knows the
   whole file is valid.  A truncated or invalid save must not leave the
   running city with half of a new one. */
struct load_scalar_backup
{
    int people_pool, total_money;
    int income_tax_rate, coal_tax_rate, dole_rate, transport_cost_rate;
    int goods_tax_rate, export_tax, export_tax_rate, import_cost;
    int import_cost_rate, tech_level, highest_tech_level;
    int modern_windmill_flag;
    int tpopulation, tstarving_population, tunemployed_population;
    int power_made, power_used, coal_made, coal_used;
    int goods_made, goods_used, ore_made, ore_used;
    int housed_population;
    int monthgraph_size;
    int *monthgraph_pop, *monthgraph_starve;
    int *monthgraph_nojobs, *monthgraph_ppool;
    int rockets_launched, rockets_launched_success, coal_survey_done;
    struct pbar_st pbars[NUM_PBARS];
    int cheat_flag, total_pollution_deaths, total_starve_deaths;
    float pollution_deaths_history, starve_deaths_history;
    int total_unemployed_years;
    float unemployed_history;
    int max_pop_ever, total_evacuated, total_births;
    int module_help_flag[NUMOF_MODULES];
    char given_scene[LC_PATH_MAX];
    int sust_dig_ore_coal_tip_flag, sust_port_flag, sustain_flag;
    int sust_dig_ore_coal_count, sust_port_count, sust_old_money;
    int sust_old_money_count, sust_old_population, sust_old_population_count;
    int sust_old_tech, sust_old_tech_count, sust_fire_count;
};

static void
load_scalar_backup_save (struct load_scalar_backup *b)
{
    int n = monthgraph_size * (int) sizeof (int);

    b->people_pool = people_pool;
    b->total_money = total_money;
    b->income_tax_rate = income_tax_rate;
    b->coal_tax_rate = coal_tax_rate;
    b->dole_rate = dole_rate;
    b->transport_cost_rate = transport_cost_rate;
    b->goods_tax_rate = goods_tax_rate;
    b->export_tax = export_tax;
    b->export_tax_rate = export_tax_rate;
    b->import_cost = import_cost;
    b->import_cost_rate = import_cost_rate;
    b->tech_level = tech_level;
    b->highest_tech_level = highest_tech_level;
    b->modern_windmill_flag = modern_windmill_flag;
    b->tpopulation = tpopulation;
    b->tstarving_population = tstarving_population;
    b->tunemployed_population = tunemployed_population;
    b->power_made = power_made;
    b->power_used = power_used;
    b->coal_made = coal_made;
    b->coal_used = coal_used;
    b->goods_made = goods_made;
    b->goods_used = goods_used;
    b->ore_made = ore_made;
    b->ore_used = ore_used;
    b->housed_population = housed_population;
    b->monthgraph_size = monthgraph_size;
    b->monthgraph_pop = (int *) malloc (n);
    b->monthgraph_starve = (int *) malloc (n);
    b->monthgraph_nojobs = (int *) malloc (n);
    b->monthgraph_ppool = (int *) malloc (n);
    if (!b->monthgraph_pop || !b->monthgraph_starve
	|| !b->monthgraph_nojobs || !b->monthgraph_ppool)
	malloc_failure ();
    memcpy (b->monthgraph_pop, monthgraph_pop, n);
    memcpy (b->monthgraph_starve, monthgraph_starve, n);
    memcpy (b->monthgraph_nojobs, monthgraph_nojobs, n);
    memcpy (b->monthgraph_ppool, monthgraph_ppool, n);
    b->rockets_launched = rockets_launched;
    b->rockets_launched_success = rockets_launched_success;
    b->coal_survey_done = coal_survey_done;
    memcpy (b->pbars, pbars, sizeof (pbars));
    b->cheat_flag = cheat_flag;
    b->total_pollution_deaths = total_pollution_deaths;
    b->pollution_deaths_history = pollution_deaths_history;
    b->total_starve_deaths = total_starve_deaths;
    b->starve_deaths_history = starve_deaths_history;
    b->total_unemployed_years = total_unemployed_years;
    b->unemployed_history = unemployed_history;
    b->max_pop_ever = max_pop_ever;
    b->total_evacuated = total_evacuated;
    b->total_births = total_births;
    memcpy (b->module_help_flag, module_help_flag, sizeof (module_help_flag));
    snprintf (b->given_scene, sizeof (b->given_scene), "%s", given_scene);
    b->sust_dig_ore_coal_tip_flag = sust_dig_ore_coal_tip_flag;
    b->sust_port_flag = sust_port_flag;
    b->sustain_flag = sustain_flag;
    b->sust_dig_ore_coal_count = sust_dig_ore_coal_count;
    b->sust_port_count = sust_port_count;
    b->sust_old_money = sust_old_money;
    b->sust_old_money_count = sust_old_money_count;
    b->sust_old_population = sust_old_population;
    b->sust_old_population_count = sust_old_population_count;
    b->sust_old_tech = sust_old_tech;
    b->sust_old_tech_count = sust_old_tech_count;
    b->sust_fire_count = sust_fire_count;
}

static void
load_scalar_backup_restore (struct load_scalar_backup *b)
{
    people_pool = b->people_pool;
    total_money = b->total_money;
    income_tax_rate = b->income_tax_rate;
    coal_tax_rate = b->coal_tax_rate;
    dole_rate = b->dole_rate;
    transport_cost_rate = b->transport_cost_rate;
    goods_tax_rate = b->goods_tax_rate;
    export_tax = b->export_tax;
    export_tax_rate = b->export_tax_rate;
    import_cost = b->import_cost;
    import_cost_rate = b->import_cost_rate;
    tech_level = b->tech_level;
    highest_tech_level = b->highest_tech_level;
    modern_windmill_flag = b->modern_windmill_flag;
    tpopulation = b->tpopulation;
    tstarving_population = b->tstarving_population;
    tunemployed_population = b->tunemployed_population;
    power_made = b->power_made;
    power_used = b->power_used;
    coal_made = b->coal_made;
    coal_used = b->coal_used;
    goods_made = b->goods_made;
    goods_used = b->goods_used;
    ore_made = b->ore_made;
    ore_used = b->ore_used;
    housed_population = b->housed_population;
    if (b->monthgraph_size == monthgraph_size) {
	memcpy (monthgraph_pop, b->monthgraph_pop,
		b->monthgraph_size * sizeof (int));
	memcpy (monthgraph_starve, b->monthgraph_starve,
		b->monthgraph_size * sizeof (int));
	memcpy (monthgraph_nojobs, b->monthgraph_nojobs,
		b->monthgraph_size * sizeof (int));
	memcpy (monthgraph_ppool, b->monthgraph_ppool,
		b->monthgraph_size * sizeof (int));
    }
    rockets_launched = b->rockets_launched;
    rockets_launched_success = b->rockets_launched_success;
    coal_survey_done = b->coal_survey_done;
    memcpy (pbars, b->pbars, sizeof (pbars));
    cheat_flag = b->cheat_flag;
    total_pollution_deaths = b->total_pollution_deaths;
    pollution_deaths_history = b->pollution_deaths_history;
    total_starve_deaths = b->total_starve_deaths;
    starve_deaths_history = b->starve_deaths_history;
    total_unemployed_years = b->total_unemployed_years;
    unemployed_history = b->unemployed_history;
    max_pop_ever = b->max_pop_ever;
    total_evacuated = b->total_evacuated;
    total_births = b->total_births;
    memcpy (module_help_flag, b->module_help_flag, sizeof (module_help_flag));
    snprintf (given_scene, sizeof (b->given_scene), "%s", b->given_scene);
    sust_dig_ore_coal_tip_flag = b->sust_dig_ore_coal_tip_flag;
    sust_port_flag = b->sust_port_flag;
    sustain_flag = b->sustain_flag;
    sust_dig_ore_coal_count = b->sust_dig_ore_coal_count;
    sust_port_count = b->sust_port_count;
    sust_old_money = b->sust_old_money;
    sust_old_money_count = b->sust_old_money_count;
    sust_old_population = b->sust_old_population;
    sust_old_population_count = b->sust_old_population_count;
    sust_old_tech = b->sust_old_tech;
    sust_old_tech_count = b->sust_old_tech_count;
    sust_fire_count = b->sust_fire_count;
}

static void
load_scalar_backup_free (struct load_scalar_backup *b)
{
    free (b->monthgraph_pop);
    free (b->monthgraph_starve);
    free (b->monthgraph_nojobs);
    free (b->monthgraph_ppool);
}

int
load_city (char *cname)
{
    int i, x, y, p, ver;
    int num_pbars, pbar_data_size;
    int pbar_tmp;
    int dummy;
    int load_originx = 0, load_originy = 0, load_total_time = 0;
    int load_num_substations = 0, load_num_markets = 0;
    int scene_is_dummy = 0;
    int load_substationx[MAX_NUMOF_SUBSTATIONS];
    int load_substationy[MAX_NUMOF_SUBSTATIONS];
    int load_marketx[MAX_NUMOF_MARKETS];
    int load_markety[MAX_NUMOF_MARKETS];
    Map *backup = NULL;
    struct load_scalar_backup scalar_backup;
    FILE *ofile;
    char s[256];
    char header_err[256];

    if ((ofile = fopen_read_gzipped (cname)) == NULL) {
	if (headless_mode) {
	    fprintf (stderr, "load_city: cannot open %s\n", cname);
	    return 0;
	}
	printf (_("Can't open <%s> (gzipped)"), cname);
	do_error ("Can't open it!");
	return 0;
    }
    if (fscanf (ofile, "%d", &ver) != 1) {
	fclose_read_gzipped (ofile);
	if (!headless_mode)
	    ok_dial_box ("error.mes", BAD, 0L);
	return 0;
    }
    if (!save_version_ok (ver)) {
	if (!headless_mode) {
	    if (ver < MIN_LOAD_VERSION)
		ok_dial_box ("too-old.mes", BAD, 0L);
	    else
		ok_dial_box ("error.mes", BAD, 0L);
	}
	fclose_read_gzipped (ofile);
	return 0;
    }

    /* Validate the versioned mod block before the map or any running-city
       state is touched.  Old saves have no block and fall through. */
    if (ver >= SAVE_MOD_HEADER_SAVE_VERSION) {
	if (!save_mod_header_read (ofile, header_err, sizeof header_err)) {
	    fprintf (stderr, "load_city: cannot load %s: %s\n",
		     cname, header_err);
	    fclose_read_gzipped (ofile);
	    if (!headless_mode)
		ok_dial_box ("error.mes", BAD, 0L);
	    return 0;
	}
    } else {
	save_mod_header_reset ();
    }

    backup = (Map *) malloc (sizeof *backup);
    if (backup == NULL)
	malloc_failure ();
    memcpy (backup, &map, sizeof *backup);
    load_scalar_backup_save (&scalar_backup);

    init_pbars();
    num_pbars = NUM_PBARS;
    pbar_data_size = PBAR_DATA_SIZE;

    /* Reset the daily/monthly/yearly accumulators before reading the saved
       scalars.  Moving this after the parse would erase the loaded
       tpopulation/tstarving/tunemployed values; the saved tpopulation is
       also used below to derive housed_population for the first month. */
    init_inventory();

    print_time_for_year();
    prog_box (_("Loading scene"), 0);

    if (!save_read_map (ofile, &map))
	goto load_corrupt;
    check_endian ();
    if (ver >= SAVE_MOD_HEADER_SAVE_VERSION
	&& !save_mod_remap_types (map.type, header_err, sizeof header_err)) {
	fprintf (stderr, "load_city: cannot remap %s: %s\n",
		 cname, header_err);
	goto load_corrupt;
    }
    set_map_groups ();

    LOAD_INT (load_originx);
    LOAD_INT (load_originy);
    if (!map_coord_ok (load_originx, load_originy))
	goto load_corrupt;
    if (load_originx > WORLD_SIDE_LEN - scr.main_win.w / 16 - 1)
	load_originx = WORLD_SIDE_LEN - scr.main_win.w / 16 - 1;

    if (load_originy > WORLD_SIDE_LEN - scr.main_win.h / 16 - 1)
	load_originy = WORLD_SIDE_LEN - scr.main_win.h / 16 - 1;

    LOAD_INT (load_total_time);
    if (ver <= MM_MS_C_VER)
	i = OLD_MAX_NUMOF_SUBSTATIONS;
    else
	i = MAX_NUMOF_SUBSTATIONS;
    for (x = 0; x < i; x++)
    {
	LOAD_INT (load_substationx[x]);
	LOAD_INT (load_substationy[x]);
    }
    prog_box ("", 92);
    LOAD_INT (load_num_substations);
    if (load_num_substations < 0 || load_num_substations > i)
	goto load_corrupt;
    if (ver <= MM_MS_C_VER)
	i = OLD_MAX_NUMOF_MARKETS;
    else
	i = MAX_NUMOF_MARKETS;
    for (x = 0; x < i; x++)
    {
	LOAD_INT (load_marketx[x]);
	LOAD_INT (load_markety[x]);
    }
    prog_box ("", 94);
    LOAD_INT (load_num_markets);
    if (load_num_markets < 0 || load_num_markets > i)
	goto load_corrupt;
    if (!map_state_ok (map.type, map.group, map.info,
		       load_num_markets, load_marketx, load_markety,
		       load_num_substations, load_substationx,
		       load_substationy)) {
	fprintf (stderr, "load_city: invalid map or saved counts in %s\n", cname);
	goto load_corrupt;
    }
    LOAD_INT (people_pool);
    LOAD_INT (total_money);
    LOAD_INT (income_tax_rate);
    LOAD_INT (coal_tax_rate);
    LOAD_INT (dole_rate);
    LOAD_INT (transport_cost_rate);
    LOAD_INT (goods_tax_rate);
    LOAD_INT (export_tax);
    LOAD_INT (export_tax_rate);
    LOAD_INT (import_cost);
    LOAD_INT (import_cost_rate);
    LOAD_INT (tech_level);
    if (tech_level > MODERN_WINDMILL_TECH)
	modern_windmill_flag = 1;
    LOAD_INT (tpopulation);
    LOAD_INT (tstarving_population);
    LOAD_INT (tunemployed_population);
    LOAD_INT (x);  /* waste_goods obsolete */
    LOAD_INT (power_made);
    LOAD_INT (power_used);
    LOAD_INT (coal_made);
    LOAD_INT (coal_used);
    LOAD_INT (goods_made);
    LOAD_INT (goods_used);
    LOAD_INT (ore_made);
    LOAD_INT (ore_used);
    LOAD_INT (dummy); /* &diff_old_population */

    /* Update variables calculated from those above */
    housed_population = tpopulation / NUMOF_DAYS_IN_MONTH;

    prog_box ("", 96);
    /* Get size of monthgraph array */
    if (ver <= MG_C_VER) {
	i = 120;
    } else {
	LOAD_INT (i);
    }
    if (i < 0 || i > 100000)
	goto load_corrupt;
    for (x = 0; x < i; x++) {
	/* If more entries in file than will fit on screen, 
	   then we need to skip past them. */
	if (x >= monthgraph_size) {
	    LOAD_INT (dummy); /* &monthgraph_pop[x] */
	    LOAD_INT (dummy); /* &monthgraph_starve[x] */
	    LOAD_INT (dummy); /* &monthgraph_nojobs[x] */
	    LOAD_INT (dummy); /* &monthgraph_ppool[x] */
	} else {
	    LOAD_INT (monthgraph_pop[x]);
	    LOAD_INT (monthgraph_starve[x]);
	    LOAD_INT (monthgraph_nojobs[x]);
	    LOAD_INT (monthgraph_ppool[x]);
	}
	/* If our save file is old, skip past obsolete diffgraph entries */
	if (ver <= MG_C_VER) {
	    LOAD_INT (dummy); /* &diffgraph_power[x] */
	    LOAD_INT (dummy); /* &diffgraph_coal[x] */
	    LOAD_INT (dummy); /* &diffgraph_goods[x] */
	    LOAD_INT (dummy); /* &diffgraph_ore[x] */
	    LOAD_INT (dummy); /* &diffgraph_population[x] */
	}
    }
    /* If screen bigger than number of entries in file, pad with zeroes */
    while (x < monthgraph_size) {
	monthgraph_pop[x] = 0;
	monthgraph_starve[x] = 0;
	monthgraph_nojobs[x] = 0;
	monthgraph_ppool[x] = 0;
	x++;
    }
    prog_box ("", 98);
    LOAD_INT (rockets_launched);
    LOAD_INT (rockets_launched_success);
    LOAD_INT (coal_survey_done);
    
    for (x = 0; x < pbar_data_size; x++) {
	for (p = 0; p < num_pbars; p++) {
	    LOAD_INT (pbar_tmp);
	    update_pbar(p,pbar_tmp,1);
/*	    LOAD_INT (pbars[p].data[x]); */
	}
    }

    for (p = 0; p < num_pbars; p++)
	pbars[p].data_size = pbar_data_size;

    prog_box ("", 99);

    for (p = 0; p < num_pbars; p++) {
	LOAD_INT (pbars[p].oldtot);
	LOAD_INT (pbars[p].diff);
    }


    LOAD_INT (cheat_flag);
    LOAD_INT (total_pollution_deaths);
    LOAD_FLOAT (pollution_deaths_history);
    LOAD_INT (total_starve_deaths);
    LOAD_FLOAT (starve_deaths_history);
    LOAD_INT (total_unemployed_years);
    LOAD_FLOAT (unemployed_history);
    LOAD_INT (max_pop_ever);
    LOAD_INT (total_evacuated);
    LOAD_INT (total_births);
    for (x = 0; x < NUMOF_MODULES; x++)
	LOAD_INT (module_help_flag[x]);
    LOAD_INT (x);	/* just dummy reads */
    LOAD_INT (x);	/* for backwards compatibility. */

    /* 10 dummy strings, for missed out things, have been put in save. */
    /* Input from this point uses them. */
    /* XXX: WCK: Huh? Missed out things? */

    if (fscanf (ofile, "%128s", given_scene) != 1)
	goto load_corrupt;
    if (strncmp (given_scene, "dummy", 5) == 0 || strlen (given_scene) < 3) {
	given_scene[0] = 0;
	scene_is_dummy = 1;
    }
    if (fscanf (ofile, "%128s", s) != 1)
	goto load_corrupt;
    if (!scene_is_dummy) {
	if (sscanf (s, "%d", &highest_tech_level) != 1)
	    goto load_corrupt;
    } else
	highest_tech_level = 0;
    if (fgets (s, 80, ofile) == NULL)	/* this is the CR */
	goto load_corrupt;

    if (fgets (s, 80, ofile) == NULL)
	goto load_corrupt;
    if (sscanf (s, "sust %d %d %d %d %d %d %d %d %d %d"
		,&sust_dig_ore_coal_count, &sust_port_count
		,&sust_old_money_count, &sust_old_population_count
		,&sust_old_tech_count, &sust_fire_count
		,&sust_old_money, &sust_old_population, &sust_old_tech
		,&sustain_flag) == 10)
    {
	sust_dig_ore_coal_tip_flag = sust_port_flag = 1;
	/* GCS FIX: Check after loading file if screen is drawn OK */
	/* draw_sustainable_window (); */
    }
    else
	sustain_flag = sust_dig_ore_coal_count = sust_port_count
		= sust_old_money_count = sust_old_population_count
		= sust_old_tech_count = sust_fire_count
		= sust_old_money = sust_old_population = sust_old_tech = 0;
    fclose_read_gzipped (ofile);
    free (backup);
    backup = NULL;
    load_scalar_backup_free (&scalar_backup);

    main_screen_originx = load_originx;
    main_screen_originy = load_originy;
    total_time = load_total_time;
    numof_substations = load_num_substations;
    numof_markets = load_num_markets;
    memcpy (substationx, load_substationx, sizeof (substationx));
    memcpy (substationy, load_substationy, sizeof (substationy));
    memcpy (marketx, load_marketx, sizeof (marketx));
    memcpy (markety, load_markety, sizeof (markety));

    for (x = 0; x < WORLD_SIDE_LEN; x++)
	for (y = 0; y < WORLD_SIDE_LEN; y++)
	    if (MP_GROUP(x,y) == GROUP_MARKET)
		inventory(x,y);

    numof_shanties = count_groups (GROUP_SHANTY);
    numof_communes = count_groups (GROUP_COMMUNE);
    prog_box ("", 100);

    /* set up the university intake. */
    x = count_groups (GROUP_UNIVERSITY);
    if (x > 0) {
	university_intake_rate
		= (count_groups (GROUP_SCHOOL) * 20) / x;
	if (university_intake_rate > 100)
	    university_intake_rate = 100;
    }
    else
	university_intake_rate = 50;
    for (x = 0; x < WORLD_SIDE_LEN; x++)
    {
	for (y = 0; y < WORLD_SIDE_LEN; y++)
	{
	    update_tech_dep (x, y);
	}
    }

    if (!headless_mode) {
	module_select_group (GROUP_TRACK);
	set_selected_module (CST_TRACK_LR);
	print_total_money ();
    }
    reset_animation_times ();
    reset_power_grid_mapping ();
    map_power_grid (); /* WCK:  Is this safe to do here? */
    fprintf (stderr, "load_city: loaded %s\n", cname);
    return 1;

load_corrupt:
    fprintf (stderr, "load_city: truncated or invalid save file: %s\n", cname);
    if (backup) {
	memcpy (&map, backup, sizeof *backup);
	free (backup);
    }
    load_scalar_backup_restore (&scalar_backup);
    load_scalar_backup_free (&scalar_backup);
    fclose_read_gzipped (ofile);
    if (!headless_mode)
	ok_dial_box ("error.mes", BAD, 0L);
    return 0;
}

#undef LOAD_INT
#undef LOAD_FLOAT

void
load_saved_city (char *s)
{
    char *cname = (char *) malloc (strlen (lc_save_dir) + strlen (s) + 2);
    snprintf (cname, strlen (lc_save_dir) + strlen (s) + 2, "%s%c%s", lc_save_dir, PATH_SLASH, s);
    load_city (cname);
    free (cname);
}

void
reset_animation_times (void)
{
    int x, y;
    for (y = 0; y < WORLD_SIDE_LEN; y++)
	for (x = 0; x < WORLD_SIDE_LEN; x++) {
	    if (MP_GROUP_IS_RESIDENCE(x,y))
		MP_INFO(x,y).int_3 = 0;
	    else if (MP_GROUP(x,y) == GROUP_WINDMILL)
		MP_INFO(x,y).int_4 = 0;
	    else if (MP_GROUP(x,y) == GROUP_BLACKSMITH)
		MP_INFO(x,y).int_4 = 0;
	    else if (MP_GROUP(x,y) == GROUP_MILL)
		MP_INFO(x,y).int_4 = 0;
	    else if (MP_GROUP(x,y) == GROUP_POTTERY)
		MP_INFO(x,y).int_4 = 0;
	    else if (MP_GROUP(x,y) == GROUP_CRICKET)
		MP_INFO(x,y).int_4 = 0;
	    else if (MP_GROUP(x,y) == GROUP_FIRESTATION)
		MP_INFO(x,y).int_4 = 0;
	    else if (MP_GROUP(x,y) == GROUP_FIRE)
	    {
		MP_INFO(x,y).int_1 = 0;
		MP_INFO(x,y).int_3 = 0;
	    }
	    else if (MP_GROUP(x,y) == GROUP_COMMUNE)
		MP_INFO(x,y).int_1 = 0;
	    else if (MP_GROUP(x,y) == GROUP_ROCKET)
		MP_INFO(x,y).int_5 = 0;
	    else if (MP_GROUP(x,y) == GROUP_INDUSTRY_H)
		MP_INFO(x,y).int_6 = 0;
	    else if (MP_GROUP(x,y) == GROUP_INDUSTRY_L)
		MP_INFO(x,y).int_7 = 0;
	}
}

/* Returns 1 if the city is proper version */
int 
verify_city (char *cname)
{
    FILE* fp;
    char* s;
    int v;

    if (strlen(cname) == 0) {
	return 0;
    }
    if ((s = (char *) malloc (lc_save_dir_len + strlen(cname) + 2)) == 0)
	malloc_failure ();
    snprintf (s, lc_save_dir_len + strlen(cname) + 2, "%s%c%s", lc_save_dir, PATH_SLASH, cname);
    if (!file_exists(s)) {
	free (s);
	return 0;
    }
    fp = fopen_read_gzipped (s);
    if (fp == NULL) {
	v = 0;
    } else {
	if (1 != fscanf (fp, "%d", &v))
	    v = 0;
	fclose_read_gzipped (fp);
    }
    free (s);
    return save_version_ok (v);
}

#ifdef MP_SANITY_CHECK
void
sanity_check (void)
{
    static int flag = 0;
    int x, y, xx, yy;
    for (x = 0; x < WORLD_SIDE_LEN; x++)
	for (y = 0; y < WORLD_SIDE_LEN; y++) {
	    if (MP_TYPE(x,y) == CST_USED) {
		xx = MP_INFO(x,y).int_1;
		yy = MP_INFO(x,y).int_2;
		if (xx < (x - 4) || yy < (y - 4) || xx > x || yy > y ||
		    xx < 0 || xx > WORLD_SIDE_LEN ||
		    yy < 0 || yy > WORLD_SIDE_LEN) {
		    printf ("Sanity failed at %d %d, points to %d %d\n", x, y, xx, yy);
		    if (flag == 0)
			yn_dial_box ("MP sanity check error",
				     "Please mail  lincity-users@lists.sourceforge.net",
				     "telling me what you just did.",
				     "Do you think I'll find this bug?");
		    flag = 1;
		}
	    }
	}
}
#endif


void
check_endian (void)
{
    static int flag = 0;
    char *cs;
    int t, x, y;
    t = 0;
    cs = (char *) &t;
    *cs = 1;
    if (t == 1)			/* little endian */
	return;
    printf ("t=%x\n", t);
    if (flag == 0) {
	flag = 1;
    }
    for (y = 0; y < WORLD_SIDE_LEN; y++) {
	for (x = 0; x < WORLD_SIDE_LEN; x++) {
	    eswap32 (&(MP_INFO(x,y).population));
	    eswap32 (&(MP_INFO(x,y).flags));
	    if (sizeof (short) == 2) {
		eswap16 (&(MP_INFO(x,y).coal_reserve));
		eswap16 (&(MP_INFO(x,y).ore_reserve));
	    } else if (sizeof (short) == 4) {
		eswap32 ((int *) &(MP_INFO(x,y).coal_reserve));
		eswap32 ((int *) &(MP_INFO(x,y).ore_reserve));
	    } else {
		printf ("Strange size (%zu) for short, please mail me.\n",
			sizeof (short));
	    }
	    eswap32 (&(MP_INFO(x,y).int_1));
	    eswap32 (&(MP_INFO(x,y).int_2));
	    eswap32 (&(MP_INFO(x,y).int_3));
	    eswap32 (&(MP_INFO(x,y).int_4));
	    eswap32 (&(MP_INFO(x,y).int_5));
	    eswap32 (&(MP_INFO(x,y).int_6));
	    eswap32 (&(MP_INFO(x,y).int_7));
	}
    }
}

void
eswap32 (int *i)
{
  char *cs, c1, c2, c3, c4;
  cs = (char *) i;
  c1 = *cs;
  c2 = *(cs + 1);
  c3 = *(cs + 2);
  c4 = *(cs + 3);
  *(cs++) = c4;
  *(cs++) = c3;
  *(cs++) = c2;
  *cs = c1;
}

void
eswap16 (unsigned short *i)
{
  char *cs, c1, c2;
  cs = (char *) i;
  c1 = *cs;
  c2 = *(cs + 1);
  *(cs++) = c2;
  *cs = c1;
}

void
do_autosave (void)
{
    if (strlen (save_names[0]) < 2)
	return;
    save_city (save_names[0]);
}
