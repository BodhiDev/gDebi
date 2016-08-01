/*
** 1998-10-21 -	A little module to hold the quit commands. Purged them from the main
**		gentoo.c module, in an effort to shrink it a little.
** 1999-05-07 -	Slightly rewritten using the new command argument support. Removed
**		the QuitNow command, use "Quit dialog=false" for the same effect.
** 1999-06-19 -	Adapted for the new dialog module.
** 2000-03-04 -	Modified for the new window handling.
*/

#include "gentoo.h"
#include "dialog.h"
#include "cmdarg.h"
#include "cmdseq.h"
#include "cmd_configure.h"
#include "configure.h"

#include "cmd_quit.h"

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-15 -	Quit the program, but first check if there have been changes to the config. */
gint cmd_quit(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	guint	dd, dialog = FALSE;

	if(win_window_update(min->gui->window))
		cfg_modified_set(min);

	dd     = (min->cfg.flags & CFLG_CHANGED) ? 2 : 0;
	dialog = car_keyword_get_boolean(ca, "dialog", dd);

	if(dialog == 2)
	{
		gint	res;

		if(cmd_configure_autosave())
			res = 1;
		else
		{
			res = dlg_dialog_sync_new_simple_wait(_("You may have some unsaved configuration changes.\n"
								"Quitting without saving will lose them. Really quit?"),
								_("Confirm Quitting"), _("_Quit|_Save, then Quit|_Cancel"));
		}

		if(res == -1)
			return TRUE;
		if(res == 1)
			csq_execute(min, "ConfigureSave");
		else if(res == 2)
			return FALSE;
	}
	else if(dialog == 1)
	{
		if(dlg_dialog_sync_new_simple_wait(_("Are you sure you want to quit?"), _("Confirm Quitting"), NULL) != DLG_POSITIVE)
			return FALSE;
	}

	if(min->cfg.dp_history.save)
		dph_history_save(min, min->gui->pane, sizeof min->gui->pane / sizeof min->gui->pane[0]);
	gtk_main_quit();

	return TRUE;
}
