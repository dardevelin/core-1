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

#include <cf3.defs.h>

#include <dbm_api.h>
#include <files_names.h>
#include <files_interfaces.h>
#include <item_lib.h>
#include <vars.h>
#include <sort.h>
#include <attributes.h>
#include <communication.h>
#include <locks.h>
#include <string_lib.h>
#include <misc_lib.h>
#include <file_lib.h>
#include <policy.h>
#include <scope.h>
#include <ornaments.h>
#include <env_context.h>

static void PrintFile(EvalContext *ctx, Attributes a, Promise *pp);
static void ReportToFile(const char *logfile, const char *message);

PromiseResult VerifyReportPromise(EvalContext *ctx, Promise *pp)
{
    Attributes a = { {0} };
    CfLock thislock;
    char unique_name[CF_EXPANDSIZE];

    a = GetReportsAttributes(ctx, pp);

    // We let AcquireLock worry about making a unique name
    snprintf(unique_name, CF_EXPANDSIZE - 1, "%s", pp->promiser);
    thislock = AcquireLock(ctx, unique_name, VUQNAME, CFSTARTTIME, a.transaction, pp, false);

    // Handle return values before locks, as we always do this

    if (a.report.result)
    {
        // User-unwritable value last-result contains the useresult
        if (strlen(a.report.result) > 0)
        {
            snprintf(unique_name, CF_BUFSIZE, "last-result[%s]", a.report.result);
        }
        else
        {
            snprintf(unique_name, CF_BUFSIZE, "last-result");
        }

        VarRef *ref = VarRefParseFromBundle(unique_name, PromiseGetBundle(pp));
        EvalContextVariablePut(ctx, ref, pp->promiser, DATA_TYPE_STRING, "goal=data,source=bundle");
        VarRefDestroy(ref);
        return PROMISE_RESULT_NOOP;
    }
    
    if (thislock.lock == NULL)
    {
        return PROMISE_RESULT_SKIPPED;
    }

    PromiseBanner(pp);

    if (a.transaction.action == cfa_warn)
    {
        cfPS(ctx, LOG_LEVEL_VERBOSE, PROMISE_RESULT_WARN, pp, a, "Need to repair reports promise: %s", pp->promiser);
        YieldCurrentLock(thislock);
        return PROMISE_RESULT_WARN;
    }

    cfPS(ctx, LOG_LEVEL_VERBOSE, PROMISE_RESULT_CHANGE, pp, a, "Report: %s", pp->promiser);

    if (a.report.to_file)
    {
        ReportToFile(a.report.to_file, pp->promiser);
    }
    else
    {
        Log(LOG_LEVEL_NOTICE, "R: %s", pp->promiser);
    }

    if (a.report.haveprintfile)
    {
        PrintFile(ctx, a, pp);
    }

    if (a.report.showstate)
    {
        /* Do nothing. Deprecated. */
    }

    if (a.report.havelastseen)
    {
        /* Do nothing. Deprecated. */
    }

    YieldCurrentLock(thislock);

    return PROMISE_RESULT_CHANGE;
}

static void ReportToFile(const char *logfile, const char *message)
{
    FILE *fp = safe_fopen(logfile, "a");
    if (fp == NULL)
    {
        Log(LOG_LEVEL_ERR, "Could not open log file '%s', message '%s'. (fopen: %s)", logfile, message, GetErrorStr());
    }
    else
    {
        fprintf(fp, "%s\n", message);
        fclose(fp);
    }
}

static void PrintFile(EvalContext *ctx, Attributes a, Promise *pp)
{
    FILE *fp;
    char buffer[CF_BUFSIZE];
    int lines = 0;

    if (a.report.filename == NULL)
    {
        Log(LOG_LEVEL_VERBOSE, "Printfile promise was incomplete, with no filename.");
        return;
    }

    if ((fp = safe_fopen(a.report.filename, "r")) == NULL)
    {
        cfPS(ctx, LOG_LEVEL_ERR, PROMISE_RESULT_INTERRUPTED, pp, a, "Printing of file '%s' was not possible. (fopen: %s)", a.report.filename, GetErrorStr());
        return;
    }

    while ((lines < a.report.numlines))
    {
        if (fgets(buffer, sizeof(buffer), fp) == NULL)
        {
            if (ferror(fp))
            {
                UnexpectedError("Failed to read line from stream");
                break;
            }
            else /* feof */
            {
                break;
            }
        }
        Log(LOG_LEVEL_ERR, "R: %s", buffer);
        lines++;
    }

    fclose(fp);
}
