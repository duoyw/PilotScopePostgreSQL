/*-------------------------------------------------------------------------
 *
 * parse_json.c
 *	  Routines to parse json and store relative varibles in corresponding structs.
 * 
 * Each anchor will be parsed here, we first extract anchor dict from the prefix
 * of the sql and parse anchors and other useful infomation.
 * 
 * The following is an example of json needed to parse: 
 * {
 *   "anchor":
 *   {
 *       "CARD_REPLACE_ANCHOR":
 *       {
 *           "enable": true,
 *           "name": "CARD_REPLACE_ANCHOR",
 *           "subquery":
 *           [
 *               "select count(*) from comments c;",
 *               "select count(*) from posts p where p.answercount <= 5 and p.favoritecount >= 0 and p.posttypeid = 2;",
 *               "select count(*) from comments c, posts p where c.postid = p.id and p.answercount <= 5 and p.favoritecount >= 0 and p.posttypeid = 2;"
 *           ],
 *           "card":
 *           [
 *               17430,
 *               318,
 *               59
 *           ]
 *       },
 *       "EXECUTION_TIME_FETCH_ANCHOR":
 *       {
 *           "enable": true,
 *           "name": "EXECUTION_TIME_FETCH_ANCHOR"
 *       },
 *       "RECORD_FETCH_ANCHOR":
 *       {
 *           "enable": true,
 *           "name": "RECORD_FETCH_ANCHOR"
 *       }
 *   },
 *   "port": 54523,
 *   "url": "localhost",
 *   "enableTerminate": false,
 *   "tid": "1234"
 * }
 * 
 * Attributes:
 *      anchor:including all of the anchor data
 *      port:the port used by python side
 *      url:the url used by python side
 *      enableTerminate:whether terminate or not after ending anchor
 *      tid:the process ID used by python side
 *      
 * In addition, we record the time of parsing the json
 * 
 * Copyright (c) 2023, Damo Academy of Alibaba Group
 * -------------------------------------------------------------------------
 */

#include<stdio.h>
#include "utils/cJSON.h"
#include "utils/cson.h"
#include "anchor2struct.h"
#include "send_and_receive.h"
#include "time.h"
#include "postgres.h"
#include "utils/pilotscope_config.h"
#include "utils/utils.h"

#define anchor_handler(anchor_json,anchor_struct,anchor_struct_definition,reflection_table) init_struct(anchor_struct,anchor_struct_definition); \
        csonJsonStr2Struct(anchor_json, anchor_struct, reflection_table);

static void parse_one_anchor(char* anchorname,char* anchor_json);
static cJSON* parse_relative_infomation();
static void for_each_anchor(cJSON *anchor);

/*
 * We parse all of the anchors here.
 *
 * First, we need to init some variables in respect to parsing, which will be described in 
 *  "anchor2struct.c". And then we extract the anchor dict of the prefix in front of a
 * sql. Finally, we parse each anchor one by one and get the total parse time.
 */
void parse_json(char* queryString)
{
    // The certain prefix is /*pilotscope     pilotscope*/
    /*
     * Try to Extract the certain prefix. If there is not such prefix,it will return and 
     * goto standard planner.
     */
    char* check_start = strstr(queryString, "/*pilotscope");
    char* check_end   = strstr(queryString, "pilotscope*/");
    if(check_start == NULL || check_end == NULL)
    {
        enablePilotscope = 0;
        elog(INFO,"There is no pilotscope comment.");
        elog(INFO,"Goto standard_planner!");
        return;
    }

    // start time

    clock_t starttime = start_to_record_time();

    // init
    init_some_vars();

    // parse relative information including tid and so on, and get anchor_item
    cJSON *anchor_item = parse_relative_infomation(queryString,check_start,check_end);

    /*
     * Process each anchor one by one using parse_one_anchor and count the num
     * of anchors to get anchor_num. We deal with the case when anchor_num == 0
     * by end_anchor.
     */
    cJSON *anchor = anchor_item->child;
    for_each_anchor(anchor);

    if(anchor_num == 0)
    {
        end_anchor();
        return;
    }

    // end time
    parser_time_ += end_time(starttime);

    // store parsing time
    store_string_for_num(parser_time_,pilot_transdata->parser_time);
}

// enumerate each anchor
static void for_each_anchor(cJSON *anchor)
{
    while (anchor != NULL) 
    {   
        // parse
        char* anchor_json = cJSON_Print(anchor);
        parse_one_anchor(anchor->string,anchor_json);

        // next anchor
        anchor = anchor->next;
        anchor_num++;
    }
}

/*
 * Pares one anchor here according to the anchorname. We change the ANCHOR_NAME
 * ,a enumerate type, by anchorname and it will goto corresponding function acc
 * -ording to ANCHOR_NAME in order to specifically parse the anchor. Noting that there are
 * some anchors left for the future work.
 */
static void parse_one_anchor(char* anchorname,char* anchor_json)
{
    // string2enu
    anchorname_to_enu(anchorname);
    
    // json2struct according to ANCHOR_NAME
    switch (*ANCHOR_NAME)
    {
        case SUBQUERY_CARD_FETCH_ANCHOR:
            anchor_handler(anchor_json,subquery_card_fetcher_anchor,SubqueryCardFetcherAnchor,Subquery_Card_Fetcher_Anchor_ref_tbl);
            break;
        case CARD_REPLACE_ANCHOR:
            anchor_handler(anchor_json,card_replace_anchor,CardReplaceAnchor,Card_Replace_Anchor_ref_tbl);
            store_aimodel_subquery2card();
            break;
        case EXECUTION_TIME_FETCH_ANCHOR:
            anchor_handler(anchor_json,execution_time_fetch_anchor,ExecutionTimeFetchAnchor,Execution_Time_Fetch_Anchor_ref_tbl);
            break;
        case RECORD_FETCH_ANCHOR:
            anchor_handler(anchor_json,record_fetch_anchor,RecordFetchAnchor,Record_Fetch_Anchor_ref_tbl);
            break;
        case CostAnchorHandler:
            break;
        case HintAnchorHandler:
            break;
        case UNKNOWN_ANCHOR:
            back_to_psql("There is an UNKNOWN_ANCHOR in json!");
            break;
        default:
            break;
    }
}

static cJSON* parse_relative_infomation(char* queryString,char* check_start,char* check_end)
{
    /*
     * Extract each item of the anchor dict including  anchor_item、port_item、url_item、
     * enableTerminate_item and tid_item. If the port_item and url_item are null, enableSend is
     * set to 0 and no need to send data back because there is no fetch anchor.
     */
    // locate the index
    int   prefix_len = strlen("/*pilotscope");
    char* start      = check_start + prefix_len+1;
    char* end        = check_end - 2;

    // get json
    int  len_of_anchor_dict     = end-start+1;
    char *string_of_anchor_dict = (char *)palloc((len_of_anchor_dict+1) * sizeof(char));
    strncpy(string_of_anchor_dict,start,len_of_anchor_dict);
    cJSON* anchor_dict = cJSON_Parse(string_of_anchor_dict);

    // get each item
    cJSON *anchor_item          = cJSON_GetObjectItem(anchor_dict, "anchor");
    cJSON *port_item            = cJSON_GetObjectItem(anchor_dict, "port");
    cJSON *url_item             = cJSON_GetObjectItem(anchor_dict, "url");
    cJSON *enableTerminate_item = cJSON_GetObjectItem(anchor_dict, "enableTerminate");
    cJSON *tid_item             = cJSON_GetObjectItem(anchor_dict, "tid");

    // enableTerminate
    enableTerminate = enableTerminate_item->valueint;

    // port、url
    if(port_item == NULL || url_item == NULL)
    {
        enableSend = 0;
        elog(INFO,"There is no fetch anchor!");
    }
    else
    {
        port = port_item->valueint;
        host = (char*)palloc(CHAR_LEN_FOR_NUM*sizeof(char));
        strcpy(host,url_item->valuestring);
    }

    // tid
    if(tid_item != NULL)
    {
        pilot_transdata->tid = (char*)palloc(CHAR_LEN_FOR_NUM*sizeof(char));
        strcpy(pilot_transdata->tid,tid_item->valuestring);
    }

    return anchor_item;
}