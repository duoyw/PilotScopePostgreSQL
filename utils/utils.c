#include "pilotscope_config.h"
#include <string.h>
#include <postgres.h>
#include "../anchor2struct.h"
#include "time.h"

// add anchor time
void add_anchor_time(char* anchor_name,double anchor_time) 
{
    anchor_time_num += 1;
    relloc_string_array_object(pilot_transdata->anchor_names,anchor_time_num+1);
    relloc_string_array_object(pilot_transdata->anchor_times,anchor_time_num+1);
    store_string(anchor_name,pilot_transdata->anchor_names[anchor_time_num-1]);
    store_string_for_num(anchor_time,pilot_transdata->anchor_times[anchor_time_num-1]) ;
}

// start to record time
clock_t start_to_record_time() 
{
    clock_t starttime = clock();
    return starttime;
}

// end time
double end_time(clock_t starttime) 
{
    clock_t endtime = clock();
    double cpu_time_used = ((double) (endtime - starttime)) / CLOCKS_PER_SEC;
    return cpu_time_used;
}





    
