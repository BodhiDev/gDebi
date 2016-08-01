/*
** 1998-09-02 -	The VIEW command. Tries to view the double-clicked file, or all selected
**		ones. Is pretty cool, since it can envoke other commands if file styles
**		call for that (through a setting of the View action).
** 1998-09-05 -	Introducing two more closely related commands (EDIT and PRINT) made me
**		notice the lousy design of the old VIEW command. Redesigned, and renamed
**		the entire module - this is now the cmd_file module.
** 1998-09-14 -	Renamed the DOUBLECLICK command; it's now called FileDefault.
** 1999-03-06 -	Changes for the recently redesigned selection management.
** 1999-05-20 -	Big, sweping changes due to new style system. Removed old hard-coded
**		action commands (FileEdit, FileView, etc), replacing them with the new
**		general FileAction command.
** 2000-09-05 -	Added magic for single-selection case.
*/

#include "gentoo.h"
#include "cmdseq.h"
#include "dirpane.h"
#include "styles.h"
#include "cmdarg.h"

#include "cmd_file.h"

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-20 -	Run a command on the currently selected files. The <action> is the action property
**		name (e.g. stuff like "Default", "View", etc), not an actual command sequence.
*/
static gint file_command(MainInfo *min, DirPane *src, const gchar *action)
{
	GSList	*slist;

	if((slist = dp_get_selection(src)) != NULL)
	{
		const gchar	*cmd;

		/* If only one row is selected, that might come from the focus, and then
		** requires a somewhat special treatment.
		*/
		if(g_slist_next(slist) == NULL)
		{
			if((cmd = stl_style_property_get_action(dp_row_get_ftype(dp_get_tree_model(src), slist->data)->style, action)) != NULL)
			{
				csq_execute(min, cmd);
			}
		}
		else
		{
			const GSList	*iter;

			for(iter = slist; iter != NULL; iter = g_slist_next(iter))
			{
				if((cmd = stl_style_property_get_action(dp_row_get_ftype(dp_get_tree_model(src), iter->data)->style, action)) != NULL)
					csq_execute(min, cmd);
			}
		}
		dp_free_selection(slist);
	}
	return 1;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-20 -	New entrypoint for general file action access. Very cool. */
gint cmd_fileaction(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	const gchar	*action = car_keyword_get_value(ca, "action", "Default");

	return file_command(min, src, action);
}
