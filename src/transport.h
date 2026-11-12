/* ---------------------------------------------------------------------- *
 * transport.h
 * This file is part of lincity.
 * Lincity is copyright (c) I J Peters 1995-1997, (c) Greg Sharp 1997-2001.
 * ---------------------------------------------------------------------- */

#ifndef __TRANSPORT_H__
#define __TRANSPORT_H__

#include "engglobs.h"
#include "mod_registry.h"

#define XY_IS_TRANSPORT(x,y) \
    mod_group_has_trait (MP_GROUP(x,y), GROUP_TRAIT_TRANSPORT)


#endif
