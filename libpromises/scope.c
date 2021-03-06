/*
   Copyright (C) CFEngine AS

   This file is part of CFEngine 3 - written and maintained by CFEngine AS.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; version 3.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA

  To the extent this program is licensed as part of the Enterprise
  versions of CFEngine, the applicable Commercial Open Source License
  (COSL) may apply to this file if you as a licensee so wish it. See
  included file COSL.txt.
*/

#include <scope.h>

#include <vars.h>
#include <expand.h>
#include <hashes.h>
#include <unix.h>
#include <fncall.h>
#include <mutex.h>
#include <misc_lib.h>
#include <rlist.h>
#include <conversion.h>
#include <syntax.h>
#include <policy.h>
#include <env_context.h>
#include <audit.h>

/*******************************************************************/

const char *SpecialScopeToString(SpecialScope scope)
{
    switch (scope)
    {
    case SPECIAL_SCOPE_CONST:
        return "const";
    case SPECIAL_SCOPE_EDIT:
        return "edit";
    case SPECIAL_SCOPE_MATCH:
        return "match";
    case SPECIAL_SCOPE_MON:
        return "mon";
    case SPECIAL_SCOPE_SYS:
        return "sys";
    case SPECIAL_SCOPE_THIS:
        return "this";
    case SPECIAL_SCOPE_BODY:
        return "body";
    default:
        ProgrammingError("Unhandled special scope");
    }
}

SpecialScope SpecialScopeFromString(const char *scope)
{
    if (strcmp("const", scope) == 0)
    {
        return SPECIAL_SCOPE_CONST;
    }
    else if (strcmp("edit", scope) == 0)
    {
        return SPECIAL_SCOPE_EDIT;
    }
    else if (strcmp("match", scope) == 0)
    {
        return SPECIAL_SCOPE_MATCH;
    }
    else if (strcmp("mon", scope) == 0)
    {
        return SPECIAL_SCOPE_MON;
    }
    else if (strcmp("sys", scope) == 0)
    {
        return SPECIAL_SCOPE_SYS;
    }
    else if (strcmp("this", scope) == 0)
    {
        return SPECIAL_SCOPE_THIS;
    }
    else if (strcmp("body", scope) == 0)
    {
        return SPECIAL_SCOPE_BODY;
    }
    else
    {
        return SPECIAL_SCOPE_NONE;
    }
}

void ScopeAugment(EvalContext *ctx, const Bundle *bp, const Promise *pp, const Rlist *arguments)
{
    if (RlistLen(bp->args) != RlistLen(arguments))
    {
        Log(LOG_LEVEL_ERR, "While constructing scope '%s'", bp->name);
        fprintf(stderr, "Formal = ");
        RlistShow(stderr, bp->args);
        fprintf(stderr, ", Actual = ");
        RlistShow(stderr, arguments);
        fprintf(stderr, "\n");
        FatalError(ctx, "Augment scope, formal and actual parameter mismatch is fatal");
    }

    const Bundle *pbp = NULL;
    if (pp != NULL)
    {
        pbp = PromiseGetBundle(pp);
    }

    for (const Rlist *rpl = bp->args, *rpr = arguments; rpl != NULL; rpl = rpl->next, rpr = rpr->next)
    {
        const char *lval = RlistScalarValue(rpl);

        Log(LOG_LEVEL_VERBOSE, "Augment scope '%s' with variable '%s' (type: %c)", bp->name, lval, rpr->val.type);

        // CheckBundleParameters() already checked that there is no namespace collision
        // By this stage all functions should have been expanded, so we only have scalars left

        if (rpr->val.type == RVAL_TYPE_SCALAR && IsNakedVar(RlistScalarValue(rpr), '@'))
        {
            DataType vtype;
            char naked[CF_BUFSIZE];
            
            GetNaked(naked, RlistScalarValue(rpr));

            Rval retval;
            if (pbp != NULL)
            {
                VarRef *ref = VarRefParseFromBundle(naked, pbp);
                EvalContextVariableGet(ctx, ref, &retval, &vtype);
                VarRefDestroy(ref);
            }
            else
            {
                VarRef *ref = VarRefParseFromBundle(naked, bp);
                EvalContextVariableGet(ctx, ref, &retval, &vtype);
                VarRefDestroy(ref);
            }

            switch (vtype)
            {
            case DATA_TYPE_STRING_LIST:
            case DATA_TYPE_INT_LIST:
            case DATA_TYPE_REAL_LIST:
                {
                    VarRef *ref = VarRefParseFromBundle(lval, bp);
                    EvalContextVariablePut(ctx, ref, retval.item, DATA_TYPE_STRING_LIST, "goal=data,source=promise");
                    VarRefDestroy(ref);
                }
                break;
            case DATA_TYPE_CONTAINER:
                {
                    VarRef *ref = VarRefParseFromBundle(lval, bp);
                    EvalContextVariablePut(ctx, ref, retval.item, DATA_TYPE_CONTAINER, "goal=data,source=promise");
                    VarRefDestroy(ref);
                }
                break;
            default:
                {
                    Log(LOG_LEVEL_ERR, "List or container parameter '%s' not found while constructing scope '%s' - use @(scope.variable) in calling reference", naked, bp->name);
                    VarRef *ref = VarRefParseFromBundle(lval, bp);
                    EvalContextVariablePut(ctx, ref, RlistScalarValue(rpr), DATA_TYPE_STRING, "goal=data,source=promise");
                    VarRefDestroy(ref);
                }
                break;
            }
        }
        else
        {
            switch(rpr->val.type)
            {
            case RVAL_TYPE_SCALAR:
                {
                    VarRef *ref = VarRefParseFromBundle(lval, bp);
                    EvalContextVariablePut(ctx, ref, RvalScalarValue(rpr->val), DATA_TYPE_STRING, "goal=data,source=promise");
                    VarRefDestroy(ref);
                }
                break;

            case RVAL_TYPE_FNCALL:
                {
                    FnCall *subfp = RlistFnCallValue(rpr);
                    Rval rval = FnCallEvaluate(ctx, subfp, pp).rval;
                    if (rval.type == RVAL_TYPE_SCALAR)
                    {
                        VarRef *ref = VarRefParseFromBundle(lval, bp);
                        EvalContextVariablePut(ctx, ref, RvalScalarValue(rval), DATA_TYPE_STRING, "goal=data,source=promise");
                        VarRefDestroy(ref);
                    }
                    else
                    {
                        Log(LOG_LEVEL_ERR, "Only functions returning scalars can be used as arguments");
                    }
                }
                break;
            default:
                ProgrammingError("An argument neither a scalar nor a list seemed to appear. Impossible");
            }
        }
    }

/* Check that there are no danglers left to evaluate in the hash table itself */

    return;
}


void ScopeMapBodyArgs(EvalContext *ctx, const Body *body, const Rlist *args)
{
    const Rlist *arg = NULL;
    const Rlist *param = NULL;

    for (arg = args, param = body->args; arg != NULL && param != NULL; arg = arg->next, param = param->next)
    {
        DataType arg_type = StringDataType(ctx, RlistScalarValue(arg));
        DataType param_type = StringDataType(ctx, RlistScalarValue(param));

        if (arg_type != param_type)
        {
            Log(LOG_LEVEL_ERR, "Type mismatch between logical/formal parameters %s/%s", RlistScalarValue(arg), RlistScalarValue(param));
            Log(LOG_LEVEL_ERR, "%s is %s whereas %s is %s", RlistScalarValue(arg), DataTypeToString(arg_type), RlistScalarValue(param), DataTypeToString(param_type));
        }

        switch (arg->val.type)
        {
        case RVAL_TYPE_SCALAR:
            {
                const char *lval = RlistScalarValue(param);
                VarRef *ref = VarRefParseFromNamespaceAndScope(lval, NULL, "body", CF_NS, '.');
                EvalContextVariablePut(ctx, ref, RvalScalarValue(arg->val), arg_type, "goal=data,source=body");
            }
            break;

        case RVAL_TYPE_LIST:
            {
                const char *lval = RlistScalarValue(param);
                VarRef *ref = VarRefParseFromNamespaceAndScope(lval, NULL, "body", CF_NS, '.');
                EvalContextVariablePut(ctx, ref, RvalRlistValue(arg->val), arg_type, "goal=data,source=body");
                VarRefDestroy(ref);
            }
            break;

        case RVAL_TYPE_FNCALL:
            {
                FnCall *fp = RlistFnCallValue(arg);
                arg_type = DATA_TYPE_NONE;
                {
                    const FnCallType *fncall_type = FnCallTypeGet(fp->name);
                    if (fncall_type)
                    {
                        arg_type = fncall_type->dtype;
                    }
                }

                FnCallResult res = FnCallEvaluate(ctx, fp, NULL);

                if (res.status == FNCALL_FAILURE && THIS_AGENT_TYPE != AGENT_TYPE_COMMON)
                {
                    Log(LOG_LEVEL_VERBOSE, "Embedded function argument does not resolve to a name - probably too many evaluation levels for '%s'",
                        fp->name);
                }
                else
                {
                    FnCallDestroy(fp);

                    const char *lval = RlistScalarValue(param);
                    void *rval = res.rval.item;

                    VarRef *ref = VarRefParseFromNamespaceAndScope(lval, NULL, "body", CF_NS, '.');
                    EvalContextVariablePut(ctx, ref, rval, arg_type, "goal=data,source=body");
                    VarRefDestroy(ref);
                }
            }

            break;

        default:
            /* Nothing else should happen */
            ProgrammingError("Software error: something not a scalar/function in argument literal");
        }
    }
}

/*******************************************************************/
/* Utility functions                                               */
/*******************************************************************/

void SplitScopeName(const char *scope, char ns_out[CF_MAXVARSIZE], char bundle_out[CF_MAXVARSIZE])
{
    assert(scope);

    char *split_point = strchr(scope, CF_NS);
    if (split_point)
    {
        strncpy(ns_out, scope, split_point - scope);
        strncpy(bundle_out, split_point + 1, CF_MAXVARSIZE);
    }
    else
    {
        strncpy(bundle_out, scope, CF_MAXVARSIZE);
    }
}

/*******************************************************************/

void JoinScopeName(const char *ns, const char *bundle, char scope_out[CF_MAXVARSIZE])
{
    assert(bundle);

    if (ns)
    {
        snprintf(scope_out, CF_MAXVARSIZE, "%s%c%s", ns, CF_NS, bundle);
    }
    else
    {
        snprintf(scope_out, CF_MAXVARSIZE, "%s", bundle);
    }
}
