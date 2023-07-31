/*-------------------------------------------------------------------------
 *
 * utils.h
 *	  prototypes for utils.c.
 *
 * Copyright (c) 2023, Damo Academy of Alibaba Group
 * 
 *-------------------------------------------------------------------------
 */

#ifndef __UTILS__
#define __UTILS__

#include "pilotscope_config.h"

extern void add_anchor_time(char* anchor_name,double anchor_time);
extern clock_t start_to_record_time();
extern double end_time(clock_t starttime);

#endif