/*-------------------------------------------------------------------------
 *
 * pilotscope.c
 *	  Routines to hook some functions in which we will process anchors. We
 *    assume that it acts as a main program of our pilotscope.  
 * 
 * Hooks:
 *      planner_hook ---> pilotscope_hook_planner
 *      ExecutorStart_hook ---> pilotscope_hook_ExecutorStart
 *      ExecutorEnd_hook ---> pilotscope_hook_ExecutorEnd
 * 
 * Anchors:
 *      subquery_card_fetcher_anchor
 *      card_replace_anchor
 *      execution_time_fetch_anchor
 *      record_fetch_anchor
 * 
 * We expect that more and more hooks and anchors are added in the future to
 * support richer functions.   
 * 
 * The following is the workflow of the whole codes:
 * 
 * 1. When postgres starts, it will go through _PG_init and the global
 * hooks will be changed into ours.
 * 
 * 2. When a sql with the certain prefix arrives at our code, it will first
 * go through the pilotscope_hook_planner where we will parse the json to 
 * get infomation about anchors and other things which will be explained in detail 
 * in "parse_json.c". If there is certain prefix in front of sql, it will goto 
 * pilotscope_standard_planner to process two anchors:  SubqueryCardFetcherAnchor and 
 * CardReplaceAnchor which will be described in "/pgsysml/optimizer/path/costsize.c".
 * 
 * 3. After that, we will judge if all of the anchors have been done. If the answer is yes,
 * it will go through end_anchor to end anchors including sending and terminateing which 
 * will be explained in "anchor2struct.c".
 * 
 * 4. If it don't terminate, it will arrives at pilotscope_hook_ExecutorStart, we will 
 * start to process ExecutionTimeFetchAnchor with the help of postgres standard function.
 * 
 * 5. Finally, it will reach pilotscope_hook_ExecutorEnd where we will process ExecutionTimeFetchAnchor
 * and end anchor like the step 3. After all of these, it will goto other places in the 
 * souce code of postgres if it don't terminate.
 * 
 * In the above steps, we will proceess most of the anchors. As for the RecordFetchAnchor, 
 * we will get the record by continuing the whole program since "enableTerminate" is 0 in this case.
 * 
 * Also, we will record the time of parse、anchor processing、sending. Specially, we just 
 * record the moment to send since we have sent the time back just after we got the sending
 * moment.
 *
 * In order to extend more abilities, we leave some "prev_hook" to store some confict hooks 
 * used by other extensions inserting into our extensions. We will properly handle potential
 * conficts in the future.
 * 
 * Copyright (c) 2023, Damo Academy of Alibaba Group
 * -------------------------------------------------------------------------
 */

#include "postgres.h"
#include "optimizer/planner.h"
#include "commands/explain.h"

PG_MODULE_MAGIC;

#include "utils/http.h"
#include "utils/cJSON.h"
#include "anchor2struct.h"
#include "parse_json.h"
#include <stdlib.h>
#include <stdio.h>
#include "time.h"
#include "utils/pilotscope_config.h"
#include "utils/utils.h"

/*
 * When postgres starts, it will go through _PG_init and the global
 * hooks will be changed into ours. In addition, we will store the previous
 * hooks in case of future incremental codes. After the life cycle, it will
 * arrive at _PG_fini to  finish pilotscope.
 */
void _PG_init(void);
void _PG_fini(void);

extern PlannedStmt * pilotscope_standard_planner(Query *, const char *, int ,
				 ParamListInfo);
static PlannedStmt* pilotscope_hook_planner(Query* parse, const char* queryString, int cursorOptions, ParamListInfo boundParams);
static void pilotscope_hook_ExecutorStart(QueryDesc *queryDesc, int eflags);
static void pilotscope_hook_ExecutorEnd(QueryDesc *queryDesc);
static void set_timer_for_exeution_time_fetch_anchor(QueryDesc *queryDesc);
static double get_totaltime_for_exeution_time_fetch_anchor();
static planner_hook_type prev_planner_hook = NULL;
static ExecutorStart_hook_type prev_ExecutorStart_hook = NULL;
static ExecutorEnd_hook_type prev_ExecutorEnd_hook = NULL;

static void activate_hooks() 
{
    planner_hook       = pilotscope_hook_planner;
    ExecutorStart_hook = pilotscope_hook_ExecutorStart;
    ExecutorEnd_hook   = pilotscope_hook_ExecutorEnd;
}

static void deactivate_hooks() 
{
    planner_hook       = prev_planner_hook;
    ExecutorStart_hook = prev_ExecutorStart_hook;
    ExecutorEnd_hook   = prev_ExecutorEnd_hook;
}

void _PG_init(void) 
{
    prev_planner_hook       = planner_hook;
    prev_ExecutorEnd_hook   = ExecutorEnd_hook;
    prev_ExecutorStart_hook = ExecutorStart_hook;
    activate_hooks();
    elog(INFO, "pilotscope extension loaded.");
}

void _PG_fini(void) 
{
    deactivate_hooks();
    elog(INFO, "pilotscope extension fini().");
}

/*
 * Here is our planner hook, we first parse json and then process two anchors in it 
 * including card_replace_anchor and subquery_card_fetcher_anchor and finally end anchors if meeting the condition.
 * 
 * There are some important varibles:
 * 
 * 'enablePilotscope' describes whether there is certain prefix in front of sql. 
 * 
 * 'anchor_num' is a global varible to record the anchor num unprocessed which are used to judge when to end anchors
 * 
 * 'card_replace_anchor->enable' and 'subquery_card_fetcher_anchor->enable' are set to 1 if there are corresponding
 * anchors in json.
 */
static PlannedStmt* pilotscope_hook_planner(Query* parse, const char* queryString, int cursorOptions, ParamListInfo boundParams) 
{
    /*
     * Parse json here 
     */
    parse_json(queryString);
    
    /*
     * If there is a previous hook, we will  give up our hook and goto the previous. The aim of such design is to leave room for 
     the future extensions such as pg_hint_plan and so on.
     */
    PlannedStmt* result = NULL;
    if (prev_planner_hook) 
    {   
        result = prev_planner_hook(parse, queryString, cursorOptions, boundParams);
    } 
    else 
    {
        // whether need pilotscope or not（according to /*pilotscope  pilotscope*/
        if(enablePilotscope == 0)
        {
            return standard_planner(parse, queryString, cursorOptions, boundParams);
        }

        /*
         * Two anchors are set in pilotscope_standard_planner. They are subquery_card_fetcher_anchor 
         * and card_replace_anchor. It is worth noting that pilotscope_standard_planner is a little different
         * from the standard_planner. Since we dig out the 'optimizer' part from pg source codes and put it in
         * our work path. Then we modify some codes in 'costsize.c' in order to process the above two anchors.
         * Some reseachers and enigineers may be confused why we do not directly modify source codes of pg.
         * The reason is that the naive method seems like convinent but will break the aim
         * of 'plug and play' because users may be forced to modify the pg source themselves.
         */
        result = pilotscope_standard_planner(parse, queryString, cursorOptions, boundParams);
        if(anchor_num != 0)
        {
            /*
             * After the above anchors are processed, it will arrive at the post-processing stage,
             * where we will make some 'enable' to 0 、reduce anchor_num by 1 and store the time of processing
             * each anchor.
             * 
             * The anchor_time_num is the num of anchors needing to record time, it is a little different from
             * anchor_num, since the RecordFetchAnchor is hard to get anchor time(the time is sended back and the 
             * program will go on to get record.) In addition, the anchor_time_num acts as the serial number
             *  of 'pilot_transdata->anchor_times'.
             */
            if(card_replace_anchor != NULL && card_replace_anchor->enable == 1)
            { 
                elog(INFO,"card_replace_anchor done!");
                change_flag_for_anchor(card_replace_anchor->enable);

                // add anchor time
                add_anchor_time(card_replace_anchor->name,cardreplace_time);
            }

            if(subquery_card_fetcher_anchor != NULL && subquery_card_fetcher_anchor->enable == 1)
            { 
                elog(INFO,"The number of subqueries is %d",pilot_transdata->subquery_num);
                elog(INFO,"subquery_card_fetcher_anchor done!");
                change_flag_for_anchor(subquery_card_fetcher_anchor->enable);
                
                // add anchor time
                add_anchor_time(subquery_card_fetcher_anchor->name,subquerycardfetcher_time);
            }

            /*
             * We will end the anchors if "anchor_num == 0" or there is just record_fetch_anchor unprocessed.
             * The record_fetch_anchor is specially dealt with because we can only process it just by the end of
             * life cycle of input sql. There is no proper room to judge in our regular process. In end_anchor, we will
             * decide whether to send and whether to terminate.
             */
            if(anchor_num == 0 || (anchor_num == 1 && record_fetch_anchor != NULL && record_fetch_anchor->enable == 1))
            {   
                end_anchor();
            }
        }

    }

    return result;
}

/*
 * Here is our ExecutorStart hook, we are ready to process execution_time_fetch_anchor in it.
 * Specifically, we will add timers to the query execution plan, in order to measure the execution 
 * time of the query.
 */
static void pilotscope_hook_ExecutorStart(QueryDesc *queryDesc, int eflags)
{
    /*
     * If there is a previous hook, we will  give up our hook and goto the previous. The aim of such design is to the future
     * extensions sun ch pg_hint_plan and so on.
     */
    if (prev_ExecutorStart_hook)
		prev_ExecutorStart_hook(queryDesc, eflags);
	else
		standard_ExecutorStart(queryDesc, eflags);

    /*
     * execution_time_fetch_anchor is first processd here and will be processed secondly in pilotscope_hook_ExecutorEnd.
     *
     * First, a variable named oldcxt of type MemoryContext is defined to store the current memory context. Then, the 
     * code switches the memory context to queryDesc->estate->es_query_cxt, which is the memory context used by the query 
     * execution plan. After switching the memory context, the code uses InstrAlloc() function to allocate a timer for 
     * each node in the query execution plan, which can be used to measure the execution time. Finally, the code switches 
     * the memory context back to the original context stored in oldcxt. 
     * 
     * The timer need some memory, in order to avoid frequently allocations and free, pg uses memorytext to do it.
     * 
     * Also, we will record the time we deal with ExecutionTimeFetchAnchor.
     */
	if (execution_time_fetch_anchor != NULL && execution_time_fetch_anchor->enable == 1)
	{
        // start time
        clock_t starttime = start_to_record_time();

        // set timer
		if (queryDesc->totaltime == NULL)
		{
            set_timer_for_exeution_time_fetch_anchor(queryDesc);
		}

        // end time
        executiontimefetch_time += end_time(starttime);
	}
}

// set timer for exeution_time_fetch_anchor
static void set_timer_for_exeution_time_fetch_anchor(QueryDesc *queryDesc)
{
    MemoryContext oldcxt;
    oldcxt = MemoryContextSwitchTo(queryDesc->estate->es_query_cxt);
    queryDesc->totaltime = InstrAlloc(1, INSTRUMENT_ALL);
    MemoryContextSwitchTo(oldcxt);
}

// get totaltime fot execution_time_fetch_anchor
static double get_totaltime_for_exeution_time_fetch_anchor(QueryDesc *queryDesc)
{
    InstrEndLoop(queryDesc->totaltime);
    double totaltime = queryDesc->totaltime->total;
    return totaltime;
}

/*
 * Here is our ExecutorEnd hook, we process execution_time_fetch_anchor in it and end anchors.
 */
static void pilotscope_hook_ExecutorEnd(QueryDesc *queryDesc) 
{
    /*
     * execution_time_fetch_anchor is secondly processed here in order to get the execution time recorded 
     * by timer set before. Also, we will record the time we deal with execution_time_fetch_anchor.
     */
    if(execution_time_fetch_anchor != NULL && execution_time_fetch_anchor->enable == 1)
    {
        // start time
        clock_t starttime = start_to_record_time();

        // get execution time
        double	totaltime = get_totaltime_for_exeution_time_fetch_anchor(queryDesc);

        // store execution time
        store_string_for_num(totaltime,pilot_transdata->execution_time);

        // update anchor num
        elog(INFO,"The execution time of query is %s s!",pilot_transdata->execution_time);
        elog(INFO,"execution_time_fetch_anchor done!");
    
        change_flag_for_anchor(execution_time_fetch_anchor->enable);

        // end time
        executiontimefetch_time += end_time(starttime);

        // add anchor time
        // avoid redefinition cause by "#define"

        add_anchor_time(execution_time_fetch_anchor->name,executiontimefetch_time);
        
       /*
        * This is the second time we try to end anchors. Because just execution_time_fetch_anchor and record_fetch_anchor 
        * need to execute the query plan, we will end anchors in pilotscope_hook_planner before executing if there is no 
        * execution_time_fetch_anchor or record_fetch_anchor in json.
        * 
        * We will end the anchors if "anchor_num == 0" or there is just record_fetch_anchor unprocessed.
        * The record_fetch_anchor is specially dealt with because we can only process it just by the end of
        * life cycle of input sql. There is no proper room to judge it in our regular process. In end_anchor, we will
        * decide whether to send and whether to terminate.
        */
        if(anchor_num == 0 || (anchor_num == 1 && record_fetch_anchor != NULL && record_fetch_anchor->enable == 1))
        {   
            end_anchor();
        }
    }
     
    /*
     * If there is a previous hook, we will  give up our hook and goto the previous. The aim of such design is to the future
     * extensions sun ch pg_hint_plan and so on.
     */
    if (prev_ExecutorEnd_hook)
        prev_ExecutorEnd_hook(queryDesc);
    else
        standard_ExecutorEnd(queryDesc);
}