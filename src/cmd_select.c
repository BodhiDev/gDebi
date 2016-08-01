/*
** 1998-05-29 -	This command is simply essential. It selects all entries in the current
**		pane.
** 1998-06-04 -	Greatly expanded; now also includes commands to deselect all entries,
**		toggle selected/unselected, and a quite powerful regular expression
**		selection tool. :) Also added freezing/thawing of the dirpane clist
**		during execution of these routines; helps speed, improves looks.
** 1998-06-05 -	Added a glob->RE translator, triggered by a check box in the selreg
**		dialog. Simplifies life for casual users (like myself). Also added
**		a check box which allows the user to invert the RE matching set, i.e.
**		to act upon all entries that do NOT match the expression.
**		BUG: The matching should check for regexec() internal errors!
** 1998-08-02 -	Added (mysteriously missing) support for SM_TYPE_FILES in selection,
**		and also closing the dialog by return/enter (accepts) and escape (cancels).
** 1998-09-19 -	Replaced regexp routines with the POSIX ones in the C library, which I
**		just discovered. Better.
** 1999-03-04 -	Changes in how selections are handled through-out gentoo made some
**		changes in this module necessary. We're on GTK+ 1.2.0 now.
** 1999-05-06 -	Adapted for built-in command argument support. Really cool.
** 1999-06-19 -	Adapted for the new dialog module. Fun, fun, fun. Not. ;^)
** 1999-11-14 -	Added support for user-selectable content matching in SelectRE. I like it.
*/

#include "gentoo.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <gdk/gdkkeysyms.h>

#include "cmdparse.h"
#include "dirpane.h"
#include "dialog.h"
#include "dpformat.h"
#include "events.h"
#include "fileutil.h"
#include "guiutil.h"
#include "strutil.h"
#include "types.h"

#include "cmd_select.h"

/* ----------------------------------------------------------------------------------------- */


/* Selection parameters.
 * FIXME: Converted over from #defines, could do with some more loving.
*/
enum {
	SM_SET = 0,
	SM_TYPE = 1,
	SM_ACTION = 2,

	SM_SET_ALL = 0,
	SM_SET_SELECTED = 1,
	SM_SET_UNSELECTED = 2,

	SM_TYPE_ALL = 0,
	SM_TYPE_DIRS = 1,
	SM_TYPE_FILES = 2,

	SM_ACTION_SELECT = 0,
	SM_ACTION_UNSELECT = 1,
	SM_ACTION_TOGGLE = 2
};

typedef void (*SelAction)(DirPane *, const DirRow2 *);

/* Base selection parameters. Filled-in either by GUI, or by direct command arguments. */
typedef struct {
	guint		set;		/* In which set of rows should we operate? */
	guint		type;		/* Do we operate only on files, dirs, or both? */
	guint		action;		/* What do we do with matching rows? */
	DPContent	content;	/* Which field of the pane should we match against? */
} SelParam;

typedef struct {
	SelParam	basic;		/* Basic parameters (selection set, action, content). */
	gboolean	glob;		/* Treat the RE as a glob pattern? */
	gboolean	invert;		/* Invert the matching sense? */
	gboolean	full;		/* Require RE to match entire row? */
	gboolean	nocase;		/* Ignore case differences? */
	GString		*re;		/* The actual regular expression string. */
} SelParamRE;

typedef struct {
	SelParam	basic;		/* Basic parameters (selection set, action, content). */
	GString		*cmd;		/* Shell command to execute. */
} SelParamShell;

static SelParamRE old_sp_re = {
	{ SM_SET_ALL, SM_TYPE_ALL, SM_ACTION_SELECT, DPC_NAME },
	TRUE, FALSE, TRUE, FALSE, NULL 
};

static SelParamShell old_sp_shell = {
	{ SM_SET_ALL, SM_TYPE_ALL, SM_ACTION_SELECT, DPC_NAME },
	NULL
};
				
/* ----------------------------------------------------------------------------------------- */

/* Select a specific row, or, if the row=ROW keyword is not specified, the one that was last clicked. */
gint cmd_selectrow(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	gint		ri;
	gchar		buf[16];
	GtkTreeIter	iter;

	ri = car_keyword_get_integer(ca, "row", -1);
	if(ri < 0)
	{
		GdkEventButton	*evt;
		gint		ok = 0;

		if((evt = (GdkEventButton *) evt_event_get(GDK_BUTTON_PRESS)) != NULL)
		{
			GtkTreePath	*path;

			if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(src->view), evt->x, evt->y, &path, NULL, NULL, NULL))
			{
				GtkTreeIter	iter;

				if(gtk_tree_model_get_iter(dp_get_tree_model(src), &iter, path))
				{
					dp_select(src, &iter);
					ok = 1;
				}
				gtk_tree_path_free(path);
			}
		}
		return ok;
	}

	g_snprintf(buf, sizeof buf, "%d", ri);
	if(gtk_tree_model_get_iter_from_string(dp_get_tree_model(src), &iter, buf))
	{
		guint		action;
		void		(*afunc)(DirPane *dp, const DirRow2 *row);

		action = car_keyword_get_enum(ca, "action", 0, "select", "unselect", "toggle", NULL);
		afunc = (action == 0) ? dp_select : (action == 1) ? dp_unselect : dp_toggle;
		afunc(src, (DirRow2 *) &iter);
		gui_events_flush();
		return 1;
	}
	return 0;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-04 -	Select all rows in the currently active pane. Rewritten for the new cooler
**		selection management made possible by GTK+ 1.2.0.
*/
gint cmd_selectall(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	dp_select_all(src);
	return 1;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-04 -	Unselect all rows of current pane. Rewritten. */
gint cmd_selectnone(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	dp_unselect_all(src);
	return 1;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-06-04 -	Invert the selection set, making all currently selected files become
**		unselected, and all unselected become selected.
** 2002-08-25 -	Complete rewrite, now uses bit vector for speed. 40-100X faster.
** 2009-10-01 -	Complete rewrite, ported to GTK+ 2.0, one-third the size.
*/
gint cmd_selecttoggle(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	GtkTreeIter	iter;

	if(gtk_tree_model_get_iter_first(dp_get_tree_model(src), &iter))
	{
		GtkTreeSelection	*ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(src->view));
		g_signal_handler_block(G_OBJECT(ts), src->sig_sel_changed);
		do
		{
			if(gtk_tree_selection_iter_is_selected(ts, &iter))
				gtk_tree_selection_unselect_iter(ts, &iter);
			else
				gtk_tree_selection_select_iter(ts, &iter);
		} while(gtk_tree_model_iter_next(dp_get_tree_model(src), &iter));
		g_signal_handler_unblock(G_OBJECT(ts), src->sig_sel_changed);
		g_signal_emit_by_name(G_OBJECT(ts), "changed", src);
	}
	return 1;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-06 -	Inspect which set (selected or unselected) the given row is, and determine
**		if it complies with the wanted one as specified in <sp>.
*/
static gboolean filter_set(DirPane *dp, const DirRow2 *row, const SelParam *sp)
{
	gboolean	sel;

	if(sp->set == SM_SET_ALL)
		return TRUE;
	sel = dp_is_selected(dp, row);
	return (sp->set == SM_SET_SELECTED) ? sel : !sel;
}

/* 1999-05-06 -	Inspect type (dir or non-dir) given row has, and match against <sp>. */
static gboolean filter_type(DirPane *dp, const DirRow2 *row, const SelParam *sp)
{
	gboolean	dir;

	if(sp->type == SM_TYPE_ALL)
		return TRUE;
	dir = dp_row_get_file_type(dp_get_tree_model(dp), row, TRUE) == G_FILE_TYPE_DIRECTORY;

	return (sp->type == SM_TYPE_DIRS) ? dir : !dir;
}

/* 2005-06-04 -	Select action fuction. The <action> should be one of the SM_ACTION_ values. */
static SelAction select_action_func(gint action)
{
	switch(action)
	{
	case SM_ACTION_SELECT:
		return dp_select;
	case SM_ACTION_UNSELECT:
		return dp_unselect;
	case SM_ACTION_TOGGLE:
		return dp_toggle;
	}
	return NULL;
}

/* 1999-05-06 -	Once we have all the selection's parameters neatly tucked into <sp>, perform the
**		actual operation.
*/
static void do_selectre(MainInfo *min, DirPane *dp, SelParamRE *sp)
{
	GString		*tmp;
	GRegex		*re;
	void		(*sel_action)(DirPane *dp, const DirRow2 *row) = NULL;

	if(sp->re == NULL)
		return;
	sel_action = select_action_func(sp->basic.action);
	if((tmp = g_string_new(sp->re->str)) != NULL)
	{
		GError	*error = NULL;

		if(sp->glob)
			stu_gstring_glob_to_re(tmp);
		if(sp->full)	/* Require RE to match entire name? Then surround it with "^$". */
		{
			g_string_prepend_c(tmp, '^');
			g_string_append_c(tmp,  '$');
		}
		if((re = g_regex_new(tmp->str, G_REGEX_EXTENDED | (sp->nocase ? G_REGEX_CASELESS : 0), G_REGEX_MATCH_NOTEMPTY, &error)) != NULL)
		{
			gboolean	matchok;
			GtkTreeIter	iter;
			gchar		text[1024];
			GtkTreeModel	*m = dp_get_tree_model(dp);

			matchok = !sp->invert;
			if(gtk_tree_model_get_iter_first(m, &iter))
			{
				do
				{
					if(filter_set(dp, &iter, &sp->basic) && filter_type(dp, &iter, &sp->basic))
					{
						if(dpf_get_content(min, dp, &iter, sp->basic.content, text, sizeof text))
						{
							if(g_regex_match(re, text, G_REGEX_MATCH_NOTEMPTY, NULL) == matchok)
								sel_action(dp, &iter);
						}
					}
				} while(gtk_tree_model_iter_next(m, &iter));
			}
			g_regex_unref(re);
		}
		else
		{
			if(error != NULL)
			{
				dlg_dialog_async_new_error(error->message);
				g_error_free(error);
			}
		}
		g_string_free(tmp, TRUE);
	}
}

/* 1999-05-06 -	Build a mode selection menu, using items from <def>. Items are separated by vertical
**		bars. Each item will have it's user data set to the index of the item in question.
** 2010-05-15 -	Now builds a combo box, rather than a menu.
*/
static GtkWidget * build_mode_combo_box(const gchar *def)
{
	GtkWidget	*cbox = NULL;
	gchar		*temp, *iname, *iend;
	guint		minor;

	if((temp = g_strdup(def)) != NULL)
	{
		cbox = gtk_combo_box_text_new();
		for(iname = temp, minor = 0; (*iname != '\0') && (iend = strchr(iname, '|')) != NULL; iname = iend + 1, minor++)
		{
			*iend = '\0';
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cbox), iname);
		}
		g_free(temp);
	}
	return cbox;
}

/* 2005-06-04 -	Compute which index in a content menu (as created by build_content_menu(), above)
 * 		a given content type has. Returns 0 if <content> is not used by <dp>.
*/
static guint content_menu_find(const DirPane *dp, DPContent content)
{
	const MainInfo	*min = dp->main;
	const DPFormat	*dpf;
	guint		i, index;

	dpf = &min->cfg.dp_format[dp->index];
	for(i = index = 0; i < dpf->num_columns; i++)
	{
		if(dpf->format[i].content == DPC_ICON)
			continue;
		if(dpf->format[i].content == content)
			return index;
		index++;
	}
	return 0;
}

/* 1999-05-06 -	Build option menus for mode selections. */
static void selparam_init_menus(DirPane *dp, GtkWidget *vbox, GtkWidget *opt_menu[4], guint *history[4])
{
	const gchar	*mdef[] = { N_("All rows|Selected|Unselected|"),
				  N_("All types|Directories only|Non-directories only|"),
				  N_("Select|Unselect|Toggle|") },
			*mlabel[] = { N_("Set"), N_("Type"), N_("Action") };
	GtkWidget	*grid, *label;
	guint		i;

	grid = gtk_grid_new();
	for(i = 0; i < sizeof mdef / sizeof mdef[0]; i++)
	{
		label = gtk_label_new(_(mlabel[i]));
		gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
		opt_menu[i] = build_mode_combo_box(_(mdef[i]));
		gtk_combo_box_set_active(GTK_COMBO_BOX(opt_menu[i]), *history[i]);
		gtk_widget_set_hexpand(opt_menu[i], TRUE);
		gtk_widget_set_halign(opt_menu[i], GTK_ALIGN_FILL);
		gtk_grid_attach(GTK_GRID(grid), opt_menu[i], 1, i, 1, 1);
	}
	label = gtk_label_new(_("Content"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
	opt_menu[3] = dpf_get_content_combo_box(NULL, NULL);	/* We read out choice later, no callback needed. */
	gtk_combo_box_set_active(GTK_COMBO_BOX(opt_menu[3]), content_menu_find(dp, *history[3]));
	gtk_grid_attach(GTK_GRID(grid), opt_menu[3], 1, i, 1, 1);

	gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);
}

/* 2005-06-04 -	Parse out basic select parameters from command arguments. */
static void selparam_set_from_cmdarg(SelParam *sp, const CmdArg *ca)
{
	const gchar	*content;

	/* Always allow non-bareword arguments to alter the settings. */
	sp->set    = car_keyword_get_enum(ca, "set", sp->set, "all rows", "selected", "unselected", NULL);
	sp->type   = car_keyword_get_enum(ca, "type", sp->type, "all types", "directories only", "non-directories only", NULL);
	sp->action = car_keyword_get_enum(ca, "action", sp->action, "select", "unselect", "toggle", NULL);
	if((content = car_keyword_get_value(ca, "content", NULL)) != NULL)
	{
		DPContent	ac;

		if((ac = dpf_get_content_from_mnemonic(content)) < DPC_NUM_TYPES)
			sp->content = ac;
	}
}

/* 2005-06-04 -	Get pointers to each value in a SelParam, useful with option menus and selparam_init_menus(). */
static void selparam_get_value_pointers(guint **ptr, SelParam *sp)
{
	ptr[0] = &sp->set;
	ptr[1] = &sp->type;
	ptr[2] = &sp->action;
	ptr[3] = (guint *) &sp->content;
}

static void selparam_read_menus(guint **ptr, GtkWidget *opt_menu[])
{
	guint	i;

	for(i = 0; i < 4; i++)
		*ptr[i] = gtk_combo_box_get_active(GTK_COMBO_BOX(opt_menu[i]));
}

/* 1998-06-04 -	Pop up a dialog window asking the user for a regular expression, and
**		then match only those entries whose names match. Very Opus.
** 1999-05-06 -	Basically rewritten, to accomodate command arguments in a nice way. Now
**		the GUI uses no external state; all values are extracted from the
**		widgets after the dialog is closed.
*/
gint cmd_selectre(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	SelParamRE	sp;
	const gchar	*str;
	guint		res = 0;

	/* Always start from the previous set of parameters. */
	sp = old_sp_re;

	selparam_set_from_cmdarg(&sp.basic, ca);
	sp.glob   = car_keyword_get_boolean(ca, "glob", sp.glob);
	sp.invert = car_keyword_get_boolean(ca, "invert", sp.invert);
	sp.full   = car_keyword_get_boolean(ca, "full", sp.full);
	sp.nocase = car_keyword_get_boolean(ca, "nocase", sp.nocase);

	/* If actual bareword expression found, it's all we need to do a search. */
	if((str = car_bareword_get(ca, 0)) != NULL)
	{
		if(sp.re == NULL)
			sp.re = g_string_new(str);
		else
			g_string_assign(sp.re, str);
	}	
	else			/* No bareword, so pop up dialog with settings, asking for expression. */
	{
		GtkWidget	*vbox, *opt_menu[4], *glob, *invert, *full, *nocase, *re;
		guint		*mptr[sizeof opt_menu / sizeof *opt_menu];
		Dialog		*dlg;

		mptr[0] = &sp.basic.set;
		mptr[1] = &sp.basic.type;
		mptr[2] = &sp.basic.action;
		mptr[3] = (guint *) &sp.basic.content;

		vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_container_set_border_width(GTK_CONTAINER(vbox), 2);
		selparam_init_menus(src, vbox, opt_menu, mptr);

		glob = gtk_check_button_new_with_label(_("Treat RE as Glob Pattern?"));
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(glob), sp.glob);
		gtk_box_pack_start(GTK_BOX(vbox), glob, FALSE, FALSE, 0);
		invert = gtk_check_button_new_with_label(_("Invert RE Matching?"));
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(invert), sp.invert);
		gtk_box_pack_start(GTK_BOX(vbox), invert, FALSE, FALSE, 0);
		full = gtk_check_button_new_with_label(_("Require Match on Full Name?"));
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(full), sp.full);
		gtk_box_pack_start(GTK_BOX(vbox), full, FALSE, FALSE, 0);
		nocase = gtk_check_button_new_with_label(_("Ignore Case?"));
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(nocase), sp.nocase);
		gtk_box_pack_start(GTK_BOX(vbox), nocase, FALSE, FALSE, 0);

		re = gui_dialog_entry_new();
		if(sp.re != NULL)
		{
			gtk_entry_set_text(GTK_ENTRY(re), sp.re->str);
			gtk_editable_select_region(GTK_EDITABLE(re), 0, -1);
		}
		gtk_box_pack_start(GTK_BOX(vbox), re, FALSE, FALSE, 0);
		dlg = dlg_dialog_sync_new(vbox, _("Select Using RE"), NULL);
		gtk_widget_grab_focus(re);
		res = dlg_dialog_sync_wait(dlg);
		if(res == DLG_POSITIVE)		/* If user accepted, collect widget state into 'sp'. */
		{
			guint	i;

			for(i = 0; i < 3; i++)
				*mptr[i] = gtk_combo_box_get_active(GTK_COMBO_BOX(opt_menu[i]));
			sp.basic.content = gtk_combo_box_get_active(GTK_COMBO_BOX(opt_menu[3]));
			sp.glob	  = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(glob));
			sp.invert = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(invert));
			sp.full   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(full));
			sp.nocase = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(nocase));
			if(sp.re != NULL)
				g_string_assign(sp.re, gtk_entry_get_text(GTK_ENTRY(re)));
			else
				sp.re = g_string_new(gtk_entry_get_text(GTK_ENTRY(re)));
		}
		dlg_dialog_sync_destroy(dlg);
	}
	if(res == 0)
	{
		do_selectre(min, src, &sp);
		old_sp_re = sp;		/* Make this search's parameter the defaults for the next. */
	}
	return 0;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-06-12 -	Extend the selection to include all rows having styles that are siblings to
**		the currently selected row(s). Typically useful to select e.g. all images
**		after selecting one, and things like that. Assumes the style tree layout is
**		sensible (i.e. rather deep and with "abstract" parents).
*/
gint cmd_selectext(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	GSList		*slist, *iter;
	GtkTreeIter	riter;
	GtkTreeModel	*model;

	slist = dp_get_selection(src);
	model = dp_get_tree_model(src);
	for(iter = slist; iter != NULL; iter = g_slist_next(iter))
	{
		const DirRow2	*sel = iter->data;

		if(gtk_tree_model_get_iter_first(model, &riter))
		{
			do
			{
				if(memcmp(sel, &riter, sizeof *sel) == 0)
					continue;
				if(stl_styleinfo_style_siblings(min->cfg.style, dp_row_get_ftype(model, sel)->style, dp_row_get_ftype(model, &riter)->style))
					dp_select(src, &riter);
			} while(gtk_tree_model_iter_next(model, &riter));
		}
	}
	dp_free_selection(slist);

	return 1;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-06-12 -	Perform a suffix selection. If there is a mouse button event available, use
**		it to determine which row has the desired suffix. If not, use the focus if
**		one exists. Else, do nothing.
*/
gint cmd_selectsuffix(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	const gchar	*suff = NULL;
	GtkTreeModel	*m = dp_get_tree_model(src);

	if((suff = car_bareword_get(ca, 0)) != NULL)
		;
	if(suff == NULL)
	{
		GdkEventButton	*evt;
		GtkTreeIter	si;
		gboolean	siok = FALSE;

		if((evt = (GdkEventButton *) evt_event_get(GDK_BUTTON_PRESS)) != NULL)
		{
			GtkTreePath	*path = NULL;

			if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(src->view), evt->x, evt->y, &path, NULL, NULL, NULL) && path != NULL)
			{
				siok = gtk_tree_model_get_iter(m, &si, path);
				gtk_tree_path_free(path);
			}
		}
		else
			g_warning("Suffix selection from focused row is not implemented");
		if(siok)
		{
			const gchar	*dn = dp_row_get_name_display(dp_get_tree_model(src), (DirRow2 *) &si);
			suff = g_utf8_strrchr(dn, -1, '.');
		}
	}

	/* Did we find a suffix? If we did, extract its suffix, and select all matching rows. */
	if(suff != NULL)
	{
		GtkTreeIter	iter;

		if(gtk_tree_model_get_iter_first(m, &iter))
		{
			guint	action;
			void	(*afunc)(DirPane *dp, const DirRow2 *row);

			action = car_keyword_get_enum(ca, "action", 0, "select", "unselect", "toggle", NULL);
			afunc = (action == 0) ? dp_select : (action == 1) ? dp_unselect : dp_toggle;
			do
			{
				if(g_str_has_suffix(dp_row_get_name_display(m, &iter), suff))
					afunc(src, &iter);

			} while(gtk_tree_model_iter_next(m, &iter));
		}
	}
	return 1;
}

/* 1999-06-12 -	Alter the selected state of all rows having the same type as a controlling
**		row. The latter is either the one most recently clicked, or the focused row.
** NOTE NOTE	This command replaces a command that had the same name, but that I think
**		wasn't very useful. If you disagree, get in touch. Thanks.
*/
gint cmd_selecttype(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	const gchar	*tname;
	GList		*types = NULL;
	GtkTreeModel	*m = dp_get_tree_model(src);
	GtkTreeIter	iter;

	if((tname = car_bareword_get(ca, 0U)) != NULL)
		types = typ_type_lookup_glob(min->cfg.type, tname);
	else
	{
		GdkEventButton	*evt;
		GtkTreePath	*path = NULL;

		if((evt = (GdkEventButton *) evt_event_get(GDK_BUTTON_PRESS)) != NULL)
			gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(src->view), evt->x, evt->y, &path, NULL, NULL, NULL);
		else
			gtk_tree_view_get_cursor(GTK_TREE_VIEW(src->view), &path, NULL);

		if(path != NULL)
		{
			GtkTreeIter	ei;

			if(gtk_tree_model_get_iter(m, &ei, path))
				types = g_list_append(NULL, dp_row_get_ftype(dp_get_tree_model(src), (DirRow2* ) &ei));
			gtk_tree_path_free(path);
		}
	}

	if(types != NULL)
	{
		if(gtk_tree_model_get_iter_first(m, &iter))
		{
			guint	action;
			void	(*afunc)(DirPane *dp, const DirRow2 *row);

			action = car_keyword_get_enum(ca, "action", 0, "select", "unselect", "toggle", NULL);
			afunc  = (action == 0) ? dp_select : (action == 1) ? dp_unselect : dp_toggle;

			do
			{
				if(g_list_find(types, dp_row_get_ftype(m, &iter)))
					afunc(src, &iter);
			} while(gtk_tree_model_iter_next(m, &iter));
		}
		g_list_free(types);
	}
	return 1;
}

/* ----------------------------------------------------------------------------------------- */

typedef enum {
	SHELL_BOOLEAN = 0	/* Interpret shell exit status as boolean, 0 is success. */
} ShellMode;

typedef struct {
	ShellMode	mode;
} ShellRule;

/* 2009-10-04 -	Rewritten, now uses glib primitive to run a full commandline, doesn't require splitting. */
static gboolean shell_select(const gchar *commandline, const ShellRule *rule)
{
	gint	status;

	if(g_spawn_command_line_sync(commandline, NULL, NULL, &status, NULL))
	{
		switch(rule->mode)
		{
		case SHELL_BOOLEAN:
			return WEXITSTATUS(status) == 0;
		}
	}
	return FALSE;
}

/* 2006-04-24 -	Select based on return value of shell command. */
gint cmd_selectshell(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	SelParamShell	sp;
	const gchar	*str;
	GString		*cmd;
	gint		reply = DLG_POSITIVE;

	/* Start from previous set of parameters. */
	sp = old_sp_shell;
	selparam_set_from_cmdarg(&sp.basic, ca);
	/* If bareword found, use it as command. Else, pop up the big old dialog. */
	if((str = car_bareword_get(ca, 0)) != NULL)
	{
		if(sp.cmd == NULL)
			sp.cmd = g_string_new(str);
		else
			g_string_assign(sp.cmd, str);
	}
	else
	{
		GtkWidget	*vbox, *opt_menu[4], *label, *entry;
		guint		*mptr[sizeof opt_menu / sizeof *opt_menu];
		Dialog		*dlg;
	
		selparam_get_value_pointers(mptr, &sp.basic);
		vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		selparam_init_menus(src, vbox, opt_menu, mptr);
		label = gtk_label_new(_("Enter shell command to run. The command\nwill have the selected content appended.\nAction is performed on successful exit."));
		gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
		entry = gui_dialog_entry_new();
		gtk_entry_set_max_length(GTK_ENTRY(entry), 1024);
		if(sp.cmd != NULL)
			gtk_entry_set_text(GTK_ENTRY(entry), sp.cmd->str);
		gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);
		dlg = dlg_dialog_sync_new(vbox, _("Select using shell command"), _("OK|Cancel"));
		gtk_editable_select_region(GTK_EDITABLE(entry), 0, 1 << 30);
		gtk_widget_grab_focus(entry);
		reply = dlg_dialog_sync_wait(dlg);
		if(reply == DLG_POSITIVE)
		{
			selparam_read_menus(mptr, opt_menu);
			if(sp.cmd == NULL)
				sp.cmd = g_string_new(gtk_entry_get_text(GTK_ENTRY(entry)));
			else
				g_string_assign(sp.cmd, gtk_entry_get_text(GTK_ENTRY(entry)));
		}
		dlg_dialog_sync_destroy(dlg);
	}

	if(reply == DLG_POSITIVE)
	{
		SelAction	sel_action;
		GtkTreeModel	*m = dp_get_tree_model(src);
		GtkTreeIter	iter;
		gchar		text[1024];
		const ShellRule	rule = { SHELL_BOOLEAN };

		if(gtk_tree_model_get_iter_first(m, &iter))
		{
			cmd = g_string_sized_new(4096);		/* Static for simplicity. */
			sel_action = select_action_func(sp.basic.action);
			do
			{
				if(!filter_set(src, &iter, &sp.basic) || !filter_type(src, &iter, &sp.basic))
					continue;
				g_string_assign(cmd, sp.cmd->str);
				g_string_append_c(cmd, ' ');
				if(!dpf_get_content(min, src, &iter, sp.basic.content, text, sizeof text))
					continue;
				g_string_append(cmd, text);
				if(cmd->str[0] == '\0' || cmd->str[0] == ' ')
					continue;
				if(shell_select(cmd->str, &rule))
					sel_action(src, &iter);
			} while(gtk_tree_model_iter_next(m, &iter));
			g_string_free(cmd, TRUE);
		}
		old_sp_shell = sp;		/* Remember parameters for next time. */
	}
	return 0;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-20 -	The UnselectFirst command. In wrong module?
** 1998-12-16 -	This was freeze/thaw bracketed, for no good reason. Didn't work.
*/
gint cmd_unselectfirst(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	GSList	*slist;

	if((slist = dp_get_selection(src)) != NULL)
	{
		dp_unselect(src, slist->data);
		dp_free_selection(slist);
	}
	return 1;
}
