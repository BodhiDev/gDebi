/*
** 1998-05-29 -	I wisen up, and design a generic command dialog interface. Might come in handy. This will
**		support the Opus-like OK, All, Skip and Cancel buttons for all commands using it (i.e. delete,
**		rename, protect etc). Great.
** 1998-06-04 -	Added flags to the cmd_generic() call, allowing different commands to tailor the generic part
**		somewhat. Specifically, that means that some commands (rename) now can get rid of the "All" button.
** 1998-07-09 -	Added flag CGF_NODIRS, which avoids calling body() or action() on dirs. Required by the split
**		command.
** 1998-07-10 -	Added CGF_NOENTER/NOESC, which disable the new keyboard shortcuts.
** 1998-07-27 -	Renamed the CGF_NOENTER flag to CGF_NORETURN. Better.
** 1998-09-12 -	Implemented the CGF_SRC flag for source pane rescanning.
** 2002-08-19 -	Congrats, sis! :) Rewrote to use dialog module, shrinking it down nicely.
*/

#include "gentoo.h"

#include <gdk/gdkkeysyms.h>

#include "dialog.h"
#include "errors.h"
#include "fileutil.h"
#include "dirpane.h"

#include "cmd_generic.h"

struct gen_info {
	guint32		flags;

	MainInfo	*min;
	DirPane		*src, *dst;
	GSList		*s_slist;		/* Source pane selection. */
	const GSList	*s_iter;		/* Source selection iterator. */

	GenBodyFunc	bf;
	GenActionFunc	af;
	GenFreeFunc	ff;
	GError		*error;			/* For action function to report any error in. */
	gpointer	user;			/* User's data, passed on in callbacks. */
	gboolean	open;
	gboolean	need_update;		/* Set if user's action function has been called. */
	gboolean	ok;			/* Tracks success/fail of last action. */

	Dialog		*dlg;
};

/* ----------------------------------------------------------------------------------------- */

/* 1998-05-29 -	This is run when user clicks "Cancel", or when we run out of entries to
**		work on. It simply closes everything down.
*/
static void end_command(struct gen_info *gen)
{
	if(gen->s_slist != NULL)
	{
		dp_free_selection(gen->s_slist);
		gen->s_slist = NULL;
	}
	if(gen->dlg != NULL)				/* Protect against the evils of uncontrolled recursion. */
	{
		if(gen->ff != NULL)
			gen->ff(gen->user);		/* Let caller free his stuff. */
		dlg_dialog_sync_destroy(gen->dlg);
		gen->dlg  = NULL;
		gen->open = FALSE;
		if(gen->need_update)
		{
			if(gen->flags & CGF_SRC)
				dp_rescan(gen->src);
			if(!(gen->flags & CGF_NODST))
				dp_rescan(gen->dst);
			gen->need_update = FALSE;
		}
	}
}

/* 2009-09-19 -	Filter out current row, based on its file type. Returns TRUE if row is to be included. */
static gboolean type_filter(const struct gen_info *gen)
{
	/* We need both target file type and actual non-following type. */
	const GFileType	tft = dp_row_get_file_type(dp_get_tree_model(gen->src), gen->s_iter->data, TRUE);
	const GFileType	ft = dp_row_get_file_type(dp_get_tree_model(gen->src), gen->s_iter->data, FALSE);
	if((gen->flags & CGF_NODIRS) && (tft == G_FILE_TYPE_DIRECTORY))
		return FALSE;
	else if((gen->flags & CGF_LINKSONLY) && (ft != G_FILE_TYPE_SYMBOLIC_LINK))
		return FALSE;
	return TRUE;
}

/* 1998-05-29 -	Find the first selected entry, initialize body using it, and return 1.
**		If no selected entry was found, 0 is returned.
*/
static gint first_body(struct gen_info *gen)
{
	for(gen->s_iter = gen->s_slist; gen->s_iter != NULL; gen->s_iter = g_slist_next(gen->s_iter))
	{
		if(type_filter(gen))
		{
			gen->bf(gen->min, gen->src, gen->s_iter->data, gen, gen->user);
			return 1;
		}
	}
	return 0;
}

/* 1998-05-29 -	Find the next selected entry, and generate body using it. If none is
**		found, we terminate by calling end_command().
** 1999-03-05 -	Rewritten for new selection and general call format.
*/
static void next_or_end(struct gen_info *gen)
{
	for(gen->s_iter = g_slist_next(gen->s_iter); gen->s_iter != NULL; gen->s_iter = g_slist_next(gen->s_iter))
	{
		if(!type_filter(gen))
			continue;
		gen->bf(gen->min, gen->src, gen->s_iter->data, gen, gen->user);
		return;
	}
	end_command(gen);
}

/* 1998-05-29 -	Execute the command on all selected entries, from the current and
**		onwards. Then close down the dialog and exit.
*/
static void all_then_end(struct gen_info *gen)
{
	for(; gen->s_iter != NULL; gen->s_iter = g_slist_next(gen->s_iter))
	{
		if(!type_filter(gen))
			continue;
		gen->need_update = TRUE;
		if(!gen->af(gen->min, gen->src, gen->dst, gen->s_iter->data, &gen->error, gen->user))
			break;
	}
	end_command(gen);
}

/* 1998-05-29 -	General purpose command execution framework entry point.
** 1998-05-31 -	Set the CAN_DEFAULT flag on all four buttons, making them a lot
**		lower (of course). This made it look somewhat better.
*/
gint cmd_generic(MainInfo *min, const gchar *title, guint32 flags, GenBodyFunc bf, GenActionFunc af, GenFreeFunc ff, gpointer user)
{
	const gchar		*btn1 = N_("_OK|A_ll|_Skip|_Cancel"),
				*btn2 = N_("_OK|_Skip|_Cancel"), *btn;
	static struct gen_info	gen;

	gen.flags = flags;
	gen.min	  = min;
	gen.src	  = min->gui->cur_pane;
	gen.dst	  = dp_mirror(min, gen.src);

	if((gen.s_iter = gen.s_slist = dp_get_selection(gen.src)) == NULL)	/* No selection? */
		return 0;

	gen.bf = bf;
	gen.af = af;
	gen.ff = ff;

	gen.open = TRUE;
	gen.error = NULL;
	gen.user = user;
	gen.need_update = FALSE;
	gen.ok = FALSE;

	btn = _((flags & CGF_NOALL) ? btn2 : btn1);
	gen.dlg = dlg_dialog_sync_new(*(GtkWidget **) user, title, btn);
	/* Make dialog stay open, and in place, after wait() returns. Less annoying. */
	dlg_dialog_sync_stay_open(gen.dlg);

	if(first_body(&gen))
	{
		gint	bid_ok = DLG_POSITIVE, bid_all = bid_ok + 1, bid_skip = bid_all + 1, bid_cancel = bid_skip + 1;
		gint	reply;

		if(flags & CGF_NOALL)		/* Adjust button IDs if no "All" present. */
			bid_all = -1, bid_skip--, bid_cancel--;

		gen.ok = TRUE;
		while(gen.open && gen.ok)
		{
			reply = dlg_dialog_sync_wait(gen.dlg);
			if(reply == bid_ok)
			{
				gen.need_update = TRUE;
				if(!(gen.ok = gen.af(gen.min, gen.src, gen.dst, gen.s_iter->data, &gen.error, gen.user)))
				{
					err_set_gerror(min, &gen.error, title, dp_get_file_from_row(gen.src, gen.s_iter->data));
					end_command(&gen);
				}
				else
					next_or_end(&gen);
			}
			else if(reply == bid_all)
				all_then_end(&gen);
			else if(reply == bid_skip)
				next_or_end(&gen);
			else
				end_command(&gen);
		}
	}
	if(gen.dlg != NULL)
		dlg_dialog_sync_destroy(gen.dlg);

	return gen.ok;
}

/* 2010-06-13 -	Track the state of an entry widget, allowing the command to execute only for non-empty strings. */
void cmd_generic_track_entry(gpointer gen, GtkWidget *entry)
{
	struct gen_info *real_gen = gen;

	dlg_dialog_track_entry(real_gen->dlg, entry);
}
