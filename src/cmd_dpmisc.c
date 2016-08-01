/*
** 1998-12-19 -	Toggle the hide allowed flag of the source pane, update the toggle button
**		accordingly, and do a rescan.
** 1999-03-15 -	Stuck the DpRecenter command in here, and renamed the entire module (was dphide).
*/

#include "gentoo.h"

#include <stdlib.h>

#include "cmdseq.h"
#include "configure.h"
#include "dirpane.h"
#include "cmd_dpmisc.h"

/* ----------------------------------------------------------------------------------------- */

gint cmd_dphide(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	DPFormat	*fmt;

	fmt = &min->cfg.dp_format[src->index];
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(src->hide), !fmt->hide_allowed);
	csq_execute(min, "DirRescan");

	return 1;
}

/* 1999-03-15 -	Recenter the horizontal pane betweeen the, er, panes. Very useful.
** 1999-08-28 -	Now handles centering the pane even if the main window has not yet been made visible.
** 1999-12-23 -	Updated to deal with the new window utility module.
** 2009-07-04 -	Refactored to nearly nothing, but a very clear nothing.
*/
gint cmd_dprecenter(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	min->cfg.dp_paning.value = 0.5;
	dp_split_refresh(min);
	return 1;
}

/* 2002-05-01 -	A command to toggle the pane orientation. */
gint cmd_dpreorient(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	gint		tmp;
	DpOrient	orient = min->cfg.dp_paning.orientation;

	tmp = car_keyword_get_enum(ca, "orient", -1, "h", "horiz", "horizontal", "v", "vert", "vertical", NULL);
	if(tmp == -1)
		orient = min->cfg.dp_paning.orientation == DPORIENT_HORIZ ? DPORIENT_VERT : DPORIENT_HORIZ;
	else if(tmp < 3)
		orient = DPORIENT_HORIZ;
	else if(tmp >= 3 && tmp < 6)
		orient = DPORIENT_VERT;
	if(orient != min->cfg.dp_paning.orientation)
	{
		if(!car_bareword_present(ca, "quiet"))
			cfg_modified_set(min);

		min->cfg.dp_paning.orientation = orient;
		rebuild_middle(min);
		rebuild_bottom(min);
		/* Make sure the split is maintained at the configured size. */
		dp_split_refresh(min);

		csq_execute(min, "ActivateOther");
		csq_execute(min, "DirRescan");
		csq_execute(min, "ActivateOther");
		csq_execute(min, "DirRescan");
	}
	return 1;
}

/* 2010-10-05 -	Maximize the source pane, or the one indexed if an argument is given. */
gint cmd_dpmaximize(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	gint	index = car_keyword_get_integer(ca, "index", -1);

	if(index < 0)
		index = src->index;
	if(index < 0 || index >= sizeof min->gui->pane / sizeof *min->gui->pane)
		return 0;

	min->cfg.dp_paning.value = (index == 0) ? 1.0 : 0.0f;
	dp_split_refresh(min);

	return 1;
}

/* 1999-09-14 -	This command (or something similar) implemented after suggestion by
**		Jarle Thorsen <jthorsen@iname.com>. It allows you to scroll a pane's
**		contents so that the first row beginning with a specified letter becomes
**		visible.
*/
gint cmd_dpgotorow(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	const gchar	*value;
	gint		rowno;
	GtkTreePath	*path = NULL;
	const gboolean	focus = car_keyword_get_boolean(ca, "focus", FALSE);

	if((value = car_keyword_get_value(ca, "re", NULL)) != NULL)
	{
		const guint	nocase = car_keyword_get_boolean(ca, "nocase", FALSE);
		GRegex		*re;

		if((re = g_regex_new(value, G_REGEX_EXTENDED | (nocase ? G_REGEX_CASELESS : 0), G_REGEX_MATCH_NOTEMPTY, NULL)) != NULL)
		{
			GtkTreeModel	*m = dp_get_tree_model(src);
			GtkTreeIter	iter;

			if(gtk_tree_model_get_iter_first(m, &iter))
			{
				do
				{
					if(g_regex_match(re, dp_row_get_name_display(m, &iter), G_REGEX_MATCH_NOTEMPTY, NULL))
					{
						path = gtk_tree_model_get_path(m, &iter);
						break;
					}
				} while(gtk_tree_model_iter_next(m, &iter));
			}
			g_regex_unref(re);
		}
	}
	else if((rowno = car_keyword_get_integer(ca, "row", -1)) > 0)
		path = gtk_tree_path_new_from_indices(rowno, -1);

	if(path != NULL)
	{
		gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(src->view), path, NULL, FALSE, 0.f, 0.f);
		if(focus)
			gtk_tree_view_set_cursor(GTK_TREE_VIEW(src->view), path, NULL, FALSE);
		gtk_tree_path_free(path);
	}
	return path != NULL;
}

/* 1999-11-21 -	Move GTK+ input focus to the path text entry box. Very handy. */
gint cmd_dpfocuspath(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	guint	select, clear;

	select = car_keyword_get_boolean(ca, "select", FALSE);
	clear  = car_keyword_get_boolean(ca, "clear", FALSE);

	gtk_editable_set_position(GTK_EDITABLE(DP_ENTRY(src)), -1);

	if(select)
		gtk_editable_select_region(GTK_EDITABLE(DP_ENTRY(src)), 0, 3);
	else if(clear)
		dp_path_clear(src);
	dp_path_focus(src);

	return 1;
}
