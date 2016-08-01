/*
** 1998-09-12 -	A simple yet very useful command; pop up the list of built-ins, and
**		let the user pick one. Then run that command!
** 1998-09-27 -	Reimplemented using the new command sequence architecture (and dialog).
**		Renamed the module too, since there is no longer any need for a RunUser
**		command.
** 1999-03-29 -	Simplified thanks to new cmdseq_dialog semantics.
*/

#include "gentoo.h"

#include "cmdseq.h"
#include "cmdseq_dialog.h"

#include "cmd_run.h"

/* ----------------------------------------------------------------------------------------- */

static char	last_command[512] = "";

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-27 -	Pop up a dialog letting user select (or type) a command name,
**		and then run it.
*/
gint cmd_run(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	const gchar	*cmd;
	gint		ok = 0;

	if((cmd = csq_dialog_sync_new_wait(min, NULL)) != NULL)
	{
		/* Don't store Rerun itself, life becomes so circular and repetitive then. */
		if(strncmp(cmd, "Rerun", 5) != 0)
		{
			/* Attempt to remember, but if command overflows, drop it but still let it run. */
			if(g_snprintf(last_command, sizeof last_command, "%s", cmd) > sizeof last_command)
				last_command[0] = '\0';
		}

		ok = csq_execute(min, cmd);
	}
	return ok;
}

/* 2010-01-01 -	Rerun the last run command. Handy during testing, at least. */
gint cmd_rerun(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	if(last_command[0] != '\0')
		return csq_execute(min, last_command);
	return cmd_run(min, src, dst, ca);
}
