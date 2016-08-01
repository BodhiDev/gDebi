/*
** 1998-08-08 -	The Swap command is reborn, sort of. This swaps the contents of the panes,
**		not the active one. Very useful.
** 1998-08-27 -	Since this module was so small, I crammed in another command; the Other
**		command, which simply "copies" the directory from the other dirpane into
**		the one current. Very useful, believe it or not.
** 1999-03-06 -	Adapted for the new selection handling.
*/

#include "gentoo.h"
#include "cmdseq.h"
#include "dirhistory.h"
#include "dirpane.h"
#include "strutil.h"

#include "cmd_swap.h"

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-25 -	Updated to play by the new selection rules, and also understand that the
**		path is displayed in a combo, and not a plain GtkEntry. Far less efficient
**		now, though. Too bad.
** 2008-09-22 -	Changes for new path entry widget in GTK+ 2.0.
** 2010-03-27 -	Totally stopped trying to be efficient, since it's hard to swap GtkTreeModels.
*/
gint cmd_swap(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	gchar	cmd[2][32 + sizeof src->dir.path];

	g_snprintf(cmd[0], sizeof cmd[0], "DirEnter %s", src->dir.path);
	g_snprintf(cmd[1], sizeof cmd[1], "DirEnter %s", dst->dir.path);

	csq_execute(min, cmd[1]);
	csq_execute(min, "ActivateOther");
	csq_execute(min, cmd[0]);
	csq_execute(min, "ActivateOther");

	return 1;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-20 -	Assume that <dst> and <src> show the same path; copy the selection from <src>
**		into <dst>.
** 1999-03-06 -	Rewritten using the new selection abstraction and stuff. Probably takes 100
**		times longer to run now... Not a big deal.
*/
static void get_selection(DirPane *dst, DirPane *src)
{
	DHSel	*sel;

	if((sel = dph_dirsel_new(src)) != NULL)
	{
		dph_dirsel_apply(dst, sel);
		dph_dirsel_destroy(sel);
	}
}

/* 1998-08-27 -	Go to the same directory as the other (i.e. destination) pane. */
gint cmd_fromother(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	dp_unselect_all(src);
	csq_execute_format(min, "DirEnter 'dir=%s'", stu_escape(dst->dir.path));
	get_selection(src, dst);

	return 1;
}

/* 1998-09-20 -	Added this to complement the old DirOther command, which was also renamed. */
gint cmd_toother(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	dp_unselect_all(dst);
	csq_execute(min, "ActivateOther");
	csq_execute_format(min, "DirEnter 'dir=%s'", stu_escape(src->dir.path));
	csq_execute(min, "ActivateOther");
	get_selection(dst, src);

	return 1;
}
