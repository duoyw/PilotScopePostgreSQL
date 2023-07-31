/*-------------------------------------------------------------------------
 *
 * anchor2struct.c
 *	  Routines to pares json to struct
 *
 * Pointers of anchor structs、pilotscope data struct and enumerate type:
 *      subquery_card_fetcher_anchor
 *      card_replace_anchor
 *      execution_time_fetch_anchor
 *      record_fetch_anchor
 *      pilot_transdata
 *      ANCHOR_NAME
 * 
 * Some global vars are used during parsing json and processing anchors:
 *      anchor_num
 *      subquery_count
 *      enableTerminate
 *      enableSend
 *      enablePilotscope
 *      subquerycardfetcher_time
 *      cardreplace_time
 *      executiontimefetch_time
 *      anchor_time_num
 *      port
 *      host
 * 
 * Some reflection tables are used in transforming json to strut wih the help
 * of "cson.h":
 *      Subquery_Card_Fetcher_Anchor_ref_tbl
 *      Card_Replace_Anchor_ref_tbl
 *      Execution_Time_Fetch_Anchor_ref_tbl
 *      Record_Fetch_Anchor_ref_tbl
 * 
 * Here, we init some varibales and structs、transform json to struct、transform struct
 * to json and store some data of anchors into hashtable in order to deal with some
 * special anchors.
 * 
 * Copyright (c) 2023, Damo Academy of Alibaba Group
 * -------------------------------------------------------------------------
 */

#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include <stdbool.h>
#include "postgres.h"
#include "time.h"
#include "utils/cJSON.h"
#include "utils/cson.h"
#include "anchor2struct.h"
#include "send_and_receive.h"
#include "utils/hashtable.h"
#include "utils/pilotscope_config.h"
#include "utils/utils.h"

/*
 * Change the value of ANCHOR_NAME according to anchorname.
 */
#define set_anchor_name_for_enu(name,enu_value) \
    if(strcmp(anchorname,name) == 0) \
    {\
        *ANCHOR_NAME = enu_value; \
        return;\
    }

/*
 * get json from cjson object
 */
#define get_json_from_cjson(root) cJSON_Print(root);

/*
 * free struct 
 */
#define free_struct(struct_name) if(struct_name!=NULL)\
    {\
        pfree(struct_name);\
    }

static char* pilottransdata_to_json();
static void put_aimodel_subquery2card();
static void cJSON_AddStringArrayToObject(cJSON*root,char* array_name,char** array,int array_size);
static void store_array_num_for_pilottransdata();
static double get_curr_timestamp();
static void free_all_struct();
/*
 * Some global vars relative to anchor operation.
 */
SubqueryCardFetcherAnchor *subquery_card_fetcher_anchor;
CardReplaceAnchor *card_replace_anchor;
ExecutionTimeFetchAnchor *execution_time_fetch_anchor ;
RecordFetchAnchor *record_fetch_anchor;
PilotTransData *pilot_transdata;
AnchorName* ANCHOR_NAME;

int anchor_num;
int subquery_count;
int enableTerminate;
int enableSend;
int enablePilotscope;
double subquerycardfetcher_time;
double cardreplace_time;
double executiontimefetch_time;
double parser_time_;
int anchor_time_num;
int port;
char* host;

/*
 * Define some reflection tables(refer to 'cson'). We need to state every varibles in the struct.
 * Note that the type '_property_int_ex' are not included in struct but need to be stated 
 * according to 'cson'. More information about reflection tables is in public document about 'cson'
 */
reflect_item_t Subquery_Card_Fetcher_Anchor_ref_tbl[] = {
    _property_bool(SubqueryCardFetcherAnchor, enable),
    _property_string(SubqueryCardFetcherAnchor, name),
    _property_end()
};

reflect_item_t Card_Replace_Anchor_ref_tbl[] = {
    _property_bool(CardReplaceAnchor, enable),
    _property_string(CardReplaceAnchor, name),

    _property_int_ex(CardReplaceAnchor, subquery_num, _ex_args_all),
    _property_array_string(CardReplaceAnchor, subquery, char*, subquery_num),

    _property_int_ex(CardReplaceAnchor, card_num, _ex_args_all),
    _property_array_real(CardReplaceAnchor, card, double, card_num),
    _property_end()
};

reflect_item_t Execution_Time_Fetch_Anchor_ref_tbl[] = {
    _property_bool(ExecutionTimeFetchAnchor, enable),
    _property_string(ExecutionTimeFetchAnchor, name),
    _property_end()
};

reflect_item_t Record_Fetch_Anchor_ref_tbl[] = {
    _property_bool(RecordFetchAnchor, enable),
    _property_string(RecordFetchAnchor, name),
    _property_end()
};

/*
 * Init some vars here including pilot_transdata、ANCHOR_NAME and other vars in
 * respect to the parsing json and processing anchors. 
 */
 void init_some_vars()
 {  
    // init pilottransdata
    init_struct(pilot_transdata,PilotTransData)

    // init other vars
    AnchorName an                   = UNKNOWN_ANCHOR;
    ANCHOR_NAME                     = &an;
    anchor_num                      = 0;
    enableSend                      = 1;
    enableTerminate                 = false;
    enablePilotscope                = 1;
    subquerycardfetcher_time        = 0.0;
    cardreplace_time                = 0.0;
    executiontimefetch_time         = 0.0;
    parser_time_                    = 0.0;
    anchor_time_num                 = 0;
    subquery_count                  = 0;
    host                            = NULL;
    port                            = 8888;
 }

/*
 * Change the value of ANCHOR_NAME according to anchorname.
 */
void anchorname_to_enu(char* anchorname)
{
    set_anchor_name_for_enu("SUBQUERY_CARD_FETCH_ANCHOR", SUBQUERY_CARD_FETCH_ANCHOR);
    set_anchor_name_for_enu("CARD_REPLACE_ANCHOR", CARD_REPLACE_ANCHOR);
    set_anchor_name_for_enu("EXECUTION_TIME_FETCH_ANCHOR", EXECUTION_TIME_FETCH_ANCHOR);
    set_anchor_name_for_enu("RECORD_FETCH_ANCHOR", RECORD_FETCH_ANCHOR);
    set_anchor_name_for_enu("PHYSICAL_PLAN_FETCH_ANCHOR", PHYSICAL_PLAN_FETCH_ANCHOR);
    set_anchor_name_for_enu("CostAnchorHandler", CostAnchorHandler);
    set_anchor_name_for_enu("HintAnchorHandler", HintAnchorHandler);
    *ANCHOR_NAME = UNKNOWN_ANCHOR;
}

/*
 * Transform pilotscopedata struct to json one by one with the help of 'cjson'.
 * Some people may be confused that why we don't use the reflection tables in 
 * 'cson'. What we can tell you is that there is some bugs when transform struct
 * to json with reflection tables by cson when some of the vars of struct is null. 
 * So, we just transform each item one by one wit the help of 'cjson'. Noting that,
 * we can modify the codes if we extend the vars in piotscopedata struct.
 */
static char* pilottransdata_to_json() 
{     
    cJSON* root = cJSON_CreateObject();    
    if (root == NULL) 
    {   
        return NULL;    
    }

    // add each single element
    cJSON_AddStringToObject(root, "sql", pilot_transdata->sql);
    cJSON_AddStringToObject(root, "physical_plan", pilot_transdata->physical_plan);
    cJSON_AddStringToObject(root, "logical_plan", pilot_transdata->logical_plan);
    cJSON_AddStringToObject(root, "execution_time", pilot_transdata->execution_time);
    cJSON_AddStringToObject(root, "tid", pilot_transdata->tid);
    cJSON_AddStringToObject(root, "parser_time", pilot_transdata->parser_time);
    cJSON_AddStringToObject(root, "http_time", pilot_transdata->http_time);

    //  add array element to json from struct
    cJSON_AddStringArrayToObject(root,"subquery",pilot_transdata->subquery,pilot_transdata->subquery_num);
    cJSON_AddStringArrayToObject(root,"card",pilot_transdata->card,pilot_transdata->card_num);
    cJSON_AddStringArrayToObject(root,"anchor_names",pilot_transdata->anchor_names,pilot_transdata->anchor_names_num);
    cJSON_AddStringArrayToObject(root,"anchor_times",pilot_transdata->anchor_times,pilot_transdata->anchor_times_num);

    // get json
    char* json = get_json_from_cjson(root);    
    cJSON_Delete(root);    

    return json;
}

/* 
 * Here,we will send data back if "enableSend == 1" and terminate the program if "enableTerminate == 1"
 */
void end_anchor()
{ 
    /*
     * Store anchor_num 、anchortime_num、card_num and subquery_num which are aligned to the anchor structs. Note that we will not
     * send this vars back. 
     */
    store_array_num_for_pilottransdata();  
    elog(INFO,"Ready to end anchors!");

    /*
     * If "enableSend == 1", we will ßfirst record the current moment as the send time and then transform
     * struct to json in struct_to_json. Finally, the data will be sent to python side.
     */
    if(enableSend == 1)
    {
        // http start time
        double http_time = get_curr_timestamp();
        store_string_for_num(http_time,pilot_transdata->http_time);

        // send
        char* string_of_pilottransdata = pilottransdata_to_json();
        send_and_receive(string_of_pilottransdata);

        // free
        free(string_of_pilottransdata); 
        free_all_struct();  
    }
    else
    {
        elog(INFO,"No fetch anchor and no need to send!");
    }

    /*
     * If enableTerminate == 1, we will terminate the program and back to psql. Note that we don't close
     * the session but just back to psql with the help of ereport.
     */
    if(enableTerminate == 1)
    {
        back_to_psql("PilotScopeFetchEnd:Back to psql!");
    }
    else
    {
        elog(INFO,"No need to terminate and the program will go on!");
    }
}

/*
 * Some hash operation for card_replace_anchor. Including store the whoe、get one and put one.
 * Note that the size of table are advised to set as  the square of the card_num in order to
 * avoid the hash confict.
 */

// store_aimodel_subquery2card
 void store_aimodel_subquery2card()
{
    // avoid hash confict
    table_size = card_replace_anchor->card_num * card_replace_anchor->card_num;
    table      = create_hashtable();

    for(int i = 0;i<card_replace_anchor->card_num;i++)
    {
        char card[CHAR_LEN_FOR_NUM];
        sprintf(card,"%.3f",card_replace_anchor->card[i]);
        put_aimodel_subquery2card(table, card_replace_anchor->subquery[i], card);
    }
}

// get_aimodel_subquery2card
char* get_aimodel_subquery2card(Hashtable* table, const char* key)
{
    char* card = get(table, key);
    if(card == NULL)
    {
        return NULL;
    }
    else
    {
        return card;
    }
}

// put_aimodel_subquery2card
static void put_aimodel_subquery2card(Hashtable* table, const char* key, const char* value)
{
    put(table, key, value);
    
}

// add string array to cjson object
static void cJSON_AddStringArrayToObject(cJSON*root,char* array_name,char** array,int array_size)
{
    if (array_size > 0 && array != NULL) 
    { 
        cJSON* cjson_array = cJSON_CreateArray(); 
        for (size_t i = 0; i < array_size; i++) { 
            if (array[i] != NULL) 
            { 
                cJSON_AddItemToArray(cjson_array, cJSON_CreateString(array[i])); 
            } 
        } 
        cJSON_AddItemToObject(root, array_name, cjson_array); 
    }
    return;
}

// store array num for pilottransdata
static void store_array_num_for_pilottransdata()
{
    pilot_transdata->anchor_names_num = anchor_time_num;
    pilot_transdata->anchor_times_num = anchor_time_num;
    pilot_transdata->card_num = subquery_count;
    pilot_transdata->subquery_num = subquery_count;
    return ;
}

// get timestamp such as 1617588512.123456
static double get_curr_timestamp()
{
    double http_time;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    http_time = tv.tv_sec + tv.tv_usec/1000000.0; 
    return http_time;
}

// free all struct
static void free_all_struct()
{
    free_struct(pilot_transdata);
    free_struct(subquery_card_fetcher_anchor);
    free_struct(card_replace_anchor);
    free_struct(execution_time_fetch_anchor);
    free_struct(record_fetch_anchor);

    return;
}