#include <stdio.h>
#include <string.h>

#include "engglobs.h"

int marketx[MAX_NUMOF_MARKETS], markety[MAX_NUMOF_MARKETS];
int substationx[MAX_NUMOF_SUBSTATIONS], substationy[MAX_NUMOF_SUBSTATIONS];
int numof_markets;
int numof_substations;

int
main (void)
{
    int i;

    for (i = 0; i < MAX_NUMOF_MARKETS; i++) {
        marketx[i] = i;
        markety[i] = i + 1;
    }
    numof_markets = MAX_NUMOF_MARKETS;
    remove_a_market (MAX_NUMOF_MARKETS - 1, MAX_NUMOF_MARKETS);
    if (numof_markets != MAX_NUMOF_MARKETS - 1) {
        fprintf (stderr, "FAIL: last market removal left count %d\n",
                 numof_markets);
        return 1;
    }
    if (marketx[0] != 0 || markety[0] != 1) {
        fprintf (stderr, "FAIL: market prefix changed\n");
        return 1;
    }
    remove_a_market (999, 999);
    if (numof_markets != MAX_NUMOF_MARKETS - 1) {
        fprintf (stderr, "FAIL: missing market changed count\n");
        return 1;
    }
    while (numof_markets > 0)
        remove_a_market (marketx[0], markety[0]);
    remove_a_market (0, 0);
    if (numof_markets != 0) {
        fprintf (stderr, "FAIL: empty market list changed count\n");
        return 1;
    }

    for (i = 0; i < MAX_NUMOF_SUBSTATIONS; i++) {
        substationx[i] = i;
        substationy[i] = i + 1;
    }
    numof_substations = MAX_NUMOF_SUBSTATIONS;
    remove_a_substation (MAX_NUMOF_SUBSTATIONS - 1, MAX_NUMOF_SUBSTATIONS);
    if (numof_substations != MAX_NUMOF_SUBSTATIONS - 1) {
        fprintf (stderr, "FAIL: last substation removal left count %d\n",
                 numof_substations);
        return 1;
    }
    if (substationx[0] != 0 || substationy[0] != 1) {
        fprintf (stderr, "FAIL: substation prefix changed\n");
        return 1;
    }
    remove_a_substation (999, 999);
    if (numof_substations != MAX_NUMOF_SUBSTATIONS - 1) {
        fprintf (stderr, "FAIL: missing substation changed count\n");
        return 1;
    }
    while (numof_substations > 0)
        remove_a_substation (substationx[0], substationy[0]);
    remove_a_substation (0, 0);
    if (numof_substations != 0) {
        fprintf (stderr, "FAIL: empty substation list changed count\n");
        return 1;
    }

    puts ("remove: last market/substation and missing entries are safe");
    return 0;
}
