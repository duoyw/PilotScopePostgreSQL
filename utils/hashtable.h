/*-------------------------------------------------------------------------
 *
 * hashtable.h
 *	  prototypes for hashtable.c.
 *
 * Copyright (c) 2023, Damo Academy of Alibaba Group
 * 
 *-------------------------------------------------------------------------
 */

#ifndef HASHTABLE_H
#define HASHTABLE_H
// #include "optimizer/optimizer.h"
typedef struct 
{
    char* key;
    char* value;
    struct Entry* next;
} Entry;

typedef struct 
{
    Entry** entries;
} Hashtable;

extern int table_size;
extern Hashtable* table;
Hashtable* create_hashtable();
void put(Hashtable* table, const char* key, const char* value);
char* get(Hashtable* table, const char* key);
#endif