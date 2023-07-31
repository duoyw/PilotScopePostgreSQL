/*-------------------------------------------------------------------------
 *
 * subplanquery.h
 *	  prototypes for subplanquery.c.
 *
 * Copyright (c) 2023, Damo Academy of Alibaba Group
 * 
 *-------------------------------------------------------------------------
 */

#ifndef subplanquery__h
#define subplanquery__h

#include "pilotscope_config.h"

extern char sub_query[SUBQUERY_MAXL];
extern void get_join_rel (PlannerInfo *root, 
					RelOptInfo *join_rel,
					RelOptInfo *outer_rel,
					RelOptInfo *inner_rel,
					List *restrictlist_in);
extern void get_single_rel (PlannerInfo *root, RelOptInfo *rel); 

#endif