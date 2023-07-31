/*-------------------------------------------------------------------------
 *
 * anchor2struct.h
 *	  prototypes for anchor2struct.c.
 *
 * Copyright (c) 2023, Damo Academy of Alibaba Group
 * 
 *-------------------------------------------------------------------------
 */
#ifndef __ANCHOR_STRUCT__
#define __ANCHOR_STRUCT__
#include "utils/hashtable.h"
#include "utils/cson.h"

// init structs including anchor struct and pilottransdata struct
#define init_struct(anchor,type) anchor = (type*)palloc(sizeof(type)); \
        memset(anchor,0,sizeof(*anchor));

// store string for num
// avoid redefinition caused by "#define"
#define store_string_for_num(num,store_var) if(1){\
        char num_string[CHAR_LEN_FOR_NUM]; \
        sprintf(num_string, "%.6f", num);\
        int num_string_length = strlen(num_string);\
        store_var = (char*)palloc((num_string_length+1)*sizeof(char));  \
        strcpy(store_var,num_string);}

// store string
// avoid redefinition caused by "#define"
#define store_string(string_object,store_var) if(1)\
    {\
        int string_length = strlen(string_object);\
        store_var = (char*)palloc((string_length+1)*sizeof(char));  \
        strcpy(store_var,string_object);}

// realloc char**
#define relloc_string_array_object(string_array_object,new_size) string_array_object = (char**)realloc(string_array_object,new_size*sizeof(char*));

// back_to_psql
#define back_to_psql(message) ereport(ERROR,(errmsg(message)));


// change flag for anchor
#define change_flag_for_anchor(anchor_flag) anchor_flag = 0;\
         anchor_num--; 

// struct
typedef struct 
{
    char* sql;
    char* physical_plan;
    char*  logical_plan;
    char* execution_time;
    char* tid;
    char** subquery;
    char** card;
    size_t subquery_num;
    size_t card_num;

    char* parser_time;
    char* http_time;
    char** anchor_names;
    char** anchor_times;
    size_t anchor_names_num;
    size_t anchor_times_num;
    
}PilotTransData;

typedef struct 
{
    int enable;
    char* name;
}SubqueryCardFetcherAnchor;

typedef struct 
{
    int enable;
    char* name;
    char** subquery;
    double* card;
    size_t  subquery_num;
    size_t  card_num;
}CardReplaceAnchor;

typedef struct 
{
    int enable;
    char* name;
}ExecutionTimeFetchAnchor;

typedef struct 
{
    int enable;
    char* name;
}RecordFetchAnchor;

// enumerate
typedef enum 
{
    SUBQUERY_CARD_FETCH_ANCHOR,
    CARD_REPLACE_ANCHOR,
    EXECUTION_TIME_FETCH_ANCHOR,
    RECORD_FETCH_ANCHOR,
    PHYSICAL_PLAN_FETCH_ANCHOR,
    CostAnchorHandler,
    HintAnchorHandler,
    UNKNOWN_ANCHOR
}AnchorName;

// struct
extern PilotTransData* pilot_transdata;
extern SubqueryCardFetcherAnchor* subquery_card_fetcher_anchor;
extern CardReplaceAnchor* card_replace_anchor;
extern ExecutionTimeFetchAnchor* execution_time_fetch_anchor;
extern RecordFetchAnchor *record_fetch_anchor;
extern AnchorName* ANCHOR_NAME;
extern reflect_item_t Subquery_Card_Fetcher_Anchor_ref_tbl[];
extern reflect_item_t Card_Replace_Anchor_ref_tbl[];
extern reflect_item_t Execution_Time_Fetch_Anchor_ref_tbl[] ;
extern reflect_item_t Record_Fetch_Anchor_ref_tbl[];

// vars
extern int anchor_num;
extern int enableTerminate;
extern double subquerycardfetcher_time;
extern double cardreplace_time;
extern double executiontimefetch_time;
extern double parser_time_;
extern int enablePilotscope;
extern int anchor_time_num;
extern int subquery_count;
extern int port;
extern char* host;
extern int enableSend;

// function
extern void init_some_vars();
extern void end_anchor();
extern char* get_aimodel_subquery2card(Hashtable* table, const char* key);
extern void store_aimodel_subquery2card();

#endif 