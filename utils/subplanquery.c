/*---------------------------------------------------------
*  subplanquery.h
*  	     Routines to generate sub-plan query to server and 
*   receive its cardinality or selectivity by http service
*
*  Copyright (c) 2022-2023 Peng Jiazhen. 
*-----------------------------------------------------------
*/

#include "postgres.h"
#include "utils/lsyscache.h"
#include "optimizer/pathnode.h"
#include "http.h"
#include "cJSON.h"
#include "commands/dbcommands.h"
#include "catalog/pg_type.h"
#include "pilotscope_config.h"

#include <math.h>
#include "access/amapi.h"
#include "access/htup_details.h"
#include "access/tsmapi.h"
#include "executor/executor.h"
#include "executor/nodeAgg.h"
#include "executor/nodeHash.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "optimizer/clauses.h"
#include "optimizer/cost.h"
#include "optimizer/optimizer.h"
#include "optimizer/pathnode.h"
#include "optimizer/paths.h"
#include "optimizer/placeholder.h"
#include "optimizer/plancat.h"
#include "optimizer/planmain.h"
#include "optimizer/restrictinfo.h"
#include "parser/parsetree.h"
#include "utils/lsyscache.h"
#include "utils/selfuncs.h"
#include "utils/spccache.h"
#include "utils/tuplesort.h"

#define IsNumType(typid)  \
	((typid) == INT8OID || \
	 (typid) == INT2OID || \
	 (typid) == INT4OID || \
	 (typid) == FLOAT4OID || \
	 (typid) == FLOAT8OID || \
	 (typid) == NUMERICOID)

static void get_expr(const Node *expr, const List *rtable);
static void get_restrictclauses(PlannerInfo *root, List *clauses);
static void get_relids (PlannerInfo *root, Relids relids);
static void get_path(PlannerInfo *root, Path *path);
static void get_join_info (PlannerInfo *root, RelOptInfo *rel);
static void get_base_restrictclauses (PlannerInfo *root, Relids relids);

// if true, the prefix = " where ", else the prefix = " and "
static bool isWhereOrAnd;
char sub_query[SUBQUERY_MAXL];

/* 
 * transform expression into string
 */
static void
get_expr(const Node *expr, const List *rtable)
{
    if (expr == NULL)
    {
        printf("<>");
        return;
    }

    if (IsA(expr, Var))
    {
        const Var  *var = (const Var *) expr;
        char	*relname,
                *attname;

        switch (var->varno)
        {
            case INNER_VAR:
                relname = "INNER";
                attname = "?";
                break;
            case OUTER_VAR:
                relname = "OUTER";
                attname = "?";
                break;
            case INDEX_VAR:
                relname = "INDEX";
                attname = "?";
                break;
            default:
            {
                RangeTblEntry *rte;

                Assert(var->varno > 0 &&
                       (int) var->varno <= list_length(rtable));
                rte = rt_fetch(var->varno, rtable);
                relname = rte->eref->aliasname;
                attname = get_rte_attribute_name(rte, var->varattno);
            }
                break;
        }

		strcat(sub_query, attname);
    }
    else if (IsA(expr, Const))
    {
        const Const *c = (const Const *) expr;
        Oid			typoutput;
        bool		typIsVarlena;
        char	   *outputstr;

        if (c->constisnull)
        {
            printf("NULL");
            return;
        }

        getTypeOutputInfo(c->consttype,
                          &typoutput, &typIsVarlena);
		
        outputstr = OidOutputFunctionCall(typoutput, c->constvalue);
		if (!IsNumType(c->consttype))
			strcat(sub_query, "\'");
		strcat(sub_query, outputstr);
		if (!IsNumType(c->consttype))
			strcat(sub_query, "\'");
        pfree(outputstr);
    }
    else if (IsA(expr, OpExpr))
    {
        const OpExpr *e = (const OpExpr *) expr;
        char	   *opname;

        opname = get_opname(e->opno);
        if (list_length(e->args) > 1)
        {
            get_expr(get_leftop((const Expr *) e), rtable);
			strcat(sub_query, " ");
			strcat(sub_query, opname);
			strcat(sub_query, " ");
            get_expr(get_rightop((const Expr *) e), rtable);
			
		}
        else
        {
            /* we print prefix and postfix ops the same... */
			strcat(sub_query, opname);
			strcat(sub_query, " ");
            get_expr(get_leftop((const Expr *) e), rtable);
        }
    }
    else if (IsA(expr, FuncExpr))
    {
        const FuncExpr *e = (const FuncExpr *) expr;
        char	   *funcname;
        ListCell   *l;

        funcname = get_func_name(e->funcid);
    	printf("%s(", ((funcname != NULL) ? funcname : "(invalid function)"));
        foreach(l, e->args)
        {
            print_expr(lfirst(l), rtable);
            if (lnext(e->args, l))
                printf(",");
        }
        printf(")");
    }
    else
        printf("unknown expr");
}

/*
 * get table from "where clause" 
 */
static void
get_restrictclauses(PlannerInfo *root, List *clauses)
{
    ListCell   *l;
	bool		first = true;
	char	   *prefix;
    foreach(l, clauses)
    {
		if (first)
		{
			prefix = isWhereOrAnd ? " where " : " and ";
			strcat(sub_query, prefix);
			isWhereOrAnd = false;
		}
			
        RestrictInfo *c = lfirst(l);
        get_expr((Node *) c->clause, root->parse->rtable);
        if (lnext(clauses, l))
            strcat(sub_query, " and ");
		first = false;
    }
}

static void 
get_base_restrictclauses (PlannerInfo *root, Relids relids)
{
	int			x;
    x = -1;
    while ((x = bms_next_member(relids, x)) >= 0)
    {
        if (x < root->simple_rel_array_size &&
            root->simple_rel_array[x])
        {
			get_restrictclauses(root, root->simple_rel_array[x]->baserestrictinfo);
		}
        else
			strcat(sub_query, "error");
    }
}

/*
 * get table from "from clause" 
 */
static void 
get_relids (PlannerInfo *root, Relids relids)
{
	int			x;
    bool		first = true;
    x = -1;
    while ((x = bms_next_member(relids, x)) >= 0)
    {
        if (!first)
			strcat(sub_query, ", ");
        if (x < root->simple_rel_array_size &&
            root->simple_rte_array[x])
		{
			char *rname = get_rel_name(root->simple_rte_array[x]->relid);
			char *alias = root->simple_rte_array[x]->eref->aliasname;
			if (strcmp(rname, alias)==0)
            {
                strcat(sub_query, rname);
            }
				
			else
            {
				strcat(sub_query, rname);
				strcat(sub_query, " ");
				strcat(sub_query, alias);
			}			
		}
        else
			strcat(sub_query, "error");
        first = false;
    }
}

/*
 * transform "join" to string
 */
static void
get_path(PlannerInfo *root, Path *path)
{
	bool		join = false;
	Path	   *subpath = NULL;
	switch (nodeTag(path))
	{
		case T_NestPath:
			join = true;
			break;
		case T_MergePath:
			join = true;
			break;
		case T_HashPath:
			join = true;
			break;
		case T_GatherPath:
			subpath = ((GatherPath *) path)->subpath;
			break;
		case T_GatherMergePath:
			subpath = ((GatherMergePath *) path)->subpath;
			break;
	}

	if (join)
	{
		JoinPath   *jp = (JoinPath *) path;

		if (jp->joinrestrictinfo){
			get_restrictclauses(root, jp->joinrestrictinfo);
		}
		else if (jp->innerjoinpath && jp->innerjoinpath->param_info && jp->innerjoinpath->param_info->ppi_clauses){
			get_restrictclauses(root, jp->innerjoinpath->param_info->ppi_clauses);
		}
	
		get_path(root, jp->outerjoinpath);
		get_path(root, jp->innerjoinpath);
	}
	if (subpath)
		get_path(root, subpath);
}

static void
get_join_info (PlannerInfo *root, RelOptInfo *rel)
{
	if (rel->cheapest_total_path)
		get_path(root, rel->cheapest_total_path);
}

/*
 * Get sing table subquery. Extract infomation from " select 、from、 where" 
 */
void
get_single_rel (PlannerInfo *root, RelOptInfo *rel) 
{
	//select
	strcpy(sub_query, "select count(*) from ");
	
	//from 
	get_relids(root, rel->relids);
	
	//where 
	isWhereOrAnd = true;
	get_restrictclauses(root, rel->baserestrictinfo);

	strcat(sub_query, "\0");
}

/*
 * Get muti table subquery. Extract infomation from " select 、from、 where" 
 */
void
get_join_rel (PlannerInfo *root, 
					RelOptInfo *join_rel,
					RelOptInfo *outer_rel,
					RelOptInfo *inner_rel,
					List *restrictlist_in) 
{
	// select
    strcpy(sub_query, "select count(*) from ");
    
    // from
	get_relids(root, join_rel->relids);
    
    //where 
	isWhereOrAnd = true;
	get_restrictclauses(root, restrictlist_in);
	get_join_info(root, inner_rel);
	get_join_info(root, outer_rel);
	get_base_restrictclauses(root, join_rel->relids);

	strcat(sub_query, ";");
	strcat(sub_query, "\0");
}