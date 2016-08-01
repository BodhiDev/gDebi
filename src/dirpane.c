/*
** 1998-05-25 -	The amount of code in the main module dealing with dirpanes simply got too big. Dike, dike.
** 1998-06-08 -	Redesigned sorting somewhat. Now, the information used to determine how to sort resides
**		in the configuration structure, not the individual dir pane.
** 1998-08-02 -	Fixed big performance mishap; when changing the sort mode, a full dir reread was done,
**		rather than just a (relatively quick) redisplay.
** 1998-08-08 -	Now finally supports high-speed dragging without losing lines. Great.
** 1999-03-05 -	Massive changes since we now rely on GTK+'s CList widget to handle all selection details.
**		Gives dragging, scrolling, and stuff.
** 1999-03-14 -	Opaque-ified the access to DirRow fields.
** 1999-05-29 -	Added support for symlinks. More controlled and more memory efficient than the old code.
** 2000-04-16 -	Simplified sorting code somewhat, implemented actual comparison functions for {u,g}name.
*/

#include "gentoo.h"

#include <ctype.h>
#include <glob.h>
#include <stdlib.h>
#include <string.h>

#include <gdk/gdkkeysyms.h>

#include "children.h"
#include "cmdseq.h"
#include "configure.h"
#include "controls.h"
#include "dirhistory.h"
#include "dpformat.h"
#include "errors.h"
#include "events.h"
#include "fileutil.h"
#include "gfam.h"
#include "guiutil.h"
#include "sizeutil.h"
#include "strutil.h"
#include "styles.h"
#include "types.h"
#include "userinfo.h"

#include "cmd_dpfocus.h"

#include "dirpane.h"

/* ----------------------------------------------------------------------------------------- */

typedef gint	(*StrCmpFunc)(const gchar *a, const gchar *b);

static DPSort	the_sort;			/* Used to get state across to qsort() callback. */

static GtkStyle	*pane_selected = NULL,		/* The style used on selected pane's column buttons. */
		*focus_style   = NULL;		/* For focusing. */

static gboolean	no_repeat = FALSE;		/* Ugly hack to prevent repeat on focused row to go into infinity. Ugly. Ugly. */

/* This is used by dp_activate_queued() to keep track of state. */
static struct {
	guint	handler;
	DirPane	*old_cur;
} the_activate_queue_info = { 0, NULL };

static GSList	*pathwidgetry_builders = NULL;

enum { COL_FILE = 0, COL_INFO, COL_FTYPE, COL_FILENAME_COLLATE_KEY, COL_LINK_TARGET_INFO, COL_FLAGS,
	COL_NUMBER_OF_COLUMNS };

/* ----------------------------------------------------------------------------------------- */

static void	clear_total_stats(DirPane *dp);
static void	clear_selection_stats(SelInfo *si);

static void	dp_activate_queued(DirPane *dp);

/* ----------------------------------------------------------------------------------------- */

/* 2010-11-21 -	Rewritten. Just initialize some of the string data fields of all panes. */
void dp_initialize(DirPane *dp, size_t num)
{
	size_t	i;

	/* Clear the path settings. */
	for(i = 0; i < num; i++)
	{
		dp[i].dir.path[0] = '\0';
		dp[i].dir.pathd = NULL;
		dp[i].dir.root = NULL;
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 2010-06-02 -	Encapsulate this tiny porting/compatibility issue to spare us from #ifdef:s all over. */
gboolean dp_realized(const MainInfo *min)
{
	return gtk_widget_get_realized(min->gui->panes);
}

/* ----------------------------------------------------------------------------------------- */

/* 2002-08-02 -	Rewrote this classic, to be a bit shorter and take const-ant time, too. Whoo. */
DirPane * dp_mirror(const MainInfo *min, const DirPane *dp)
{
	return &min->gui->pane[1 - dp->index];
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-05 -	Get the full name, with path, for row number <row> of <dp>. Trivial, but
**		saves a couple of lines here and there elsewhere in the program.
** 1999-01-20 -	Now tries to avoid starting the name with two slashes for root entries.
** 2002-02-25 -	Generalized to never produce double slashes, removed special case for root.
*/
const gchar * dp_full_name(const DirPane *dp, const DirRow2 *row)
{
	static gchar	buf[PATH_MAX];

	g_snprintf(buf, sizeof buf, "%s/%s", g_file_get_path(dp->dir.root), dp_row_get_name(dp_get_tree_model(dp), row));
	return buf;
}

/* 1999-02-23 -	Get the name of a row, but quoted and with any embedded quotes
**		escaped by backslashes. Very handy when evaluating {f}-codes...
**		If <path> is TRUE, the path is included as well. Doesn't nest.
*/
const gchar * dp_name_quoted(const DirPane *dp, const DirRow2 *row, gboolean path)
{
	static gchar	buf[2 * PATH_MAX];
	const gchar	*fn, *ptr;
	gchar		*put = buf;

	fn = path ? dp_full_name(dp, row) : dp_row_get_name(dp_get_tree_model(dp), row);
	*put++ = '"';
	for(ptr = fn; *ptr; ptr++)
	{
		if(*ptr == '"' || *ptr == '\\')
			*put++ = '\\';
		*put++ = *ptr;
	}
	*put++ = '"';
	*put = '\0';

	return buf;
}

/* ----------------------------------------------------------------------------------------- */

/* 2008-09-20 -	Clear the completion cache. */
static void completion_clear(DirPane *dp)
{
	GtkListStore	*cm;

	cm = GTK_LIST_STORE(gtk_entry_completion_get_model(dp->complete.compl));
	if(cm == NULL)
		return;
	gtk_list_store_clear(cm);
}

/* 2008-09-20 -	Update the completion for the given pane, considering that 'prefix' is the
 *		current directory root. If this is the same as the cached, do nothing.
*/
static void completion_update(DirPane *dp, const gchar *prefix)
{
	GtkListStore	*cm;
	GFile		*dir;
	GFileEnumerator	*fen;

	if(strcmp(dp->complete.prefix, prefix) == 0)
		return;
	cm = GTK_LIST_STORE(gtk_entry_completion_get_model(dp->complete.compl));
	if(cm == NULL)
		return;
	/* We're about to check on-disk, so clear the cache in the GtkEntryCompletion. */
	gtk_list_store_clear(cm);
	dir = g_file_parse_name(prefix);
	if((fen = g_file_enumerate_children(dir, "standard::*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL)) != NULL)
	{
		GFileInfo	*child;
		GtkTreeIter	iter;

		while((child = g_file_enumerator_next_file(fen, NULL, NULL)) != NULL)
		{
			if(g_file_info_get_file_type(child) == G_FILE_TYPE_DIRECTORY)
			{
				gchar	buf[4 * PATH_MAX];

				/* Assumes prefix ends with slash. Slightly Unix-centric. */
				g_snprintf(buf, sizeof buf, "%s%s", prefix, g_file_info_get_name(child));
				gtk_list_store_insert_with_values(cm, &iter, -1, 0, buf, -1);
			}
			g_object_unref(G_OBJECT(child));
		}
		g_strlcpy(dp->complete.prefix, prefix, sizeof dp->complete.prefix);
		g_object_unref(fen);
	}
	g_object_unref(G_OBJECT(dir));
}

static gboolean completion_match_function(GtkEntryCompletion *completion, const gchar *key, GtkTreeIter *iter, gpointer user)
{
	gchar	*mv;

	gtk_tree_model_get(gtk_entry_completion_get_model(completion), iter, 0, &mv, -1);
	if(mv != NULL)
	{
		gsize		klen = strlen(key);	/* NOTE: This is in *bytes*, not UTF-8 chars. */
		gboolean	eq = memcmp(key, mv, klen) == 0;

		g_free(mv);
		return eq;
	}
	return FALSE;
}

/* ----------------------------------------------------------------------------------------- */

/* 2000-02-03 -	Here's the handler for the optional "huge parent" button, in the pane margin. */
static void evt_hugeparent_clicked(GtkWidget *wid, gpointer user)
{
	DirPane	*dp = user;

	csq_execute(dp->main, dp->index == 0 ? "ActivateLeft" : "ActivateRight");
	csq_execute(dp->main, "DirParent");
}

/* 1998-09-15 -	This handler gets run when user clicks the "up" button next to path entry. */
static void evt_parent_clicked(GtkWidget *wid, gpointer user)
{
	DirPane	*dp = user;

	csq_execute(dp->main, dp->index == 0 ? "ActivateLeft" : "ActivateRight");
	csq_execute(dp->main, "DirParent");
}

/* 2008-09-13 -	A simple rewrite of the older code, considering the switch to a GtkComboBoxEntry
 *		widget for the path entry/history tasks.
*/
static void evt_path_new(GtkWidget *wid, gpointer user)
{
	DirPane	*dp = user;

	dp_activate(dp);
	csq_execute_format(dp->main, "DirEnter 'dir=%s'", stu_escape(gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(dp->path))))));
	dp_path_unfocus(dp);
}

static void evt_path_changed(GtkWidget *wid, gpointer user)
{
	gint		index;
	const gchar	*now;

	now = gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(wid))));
	index = gtk_combo_box_get_active(GTK_COMBO_BOX(wid));
	if(index < 0)
	{
		gchar	*ptr;

		/* Build "prefix" version of the current path. The prefix is defined, for completion
		 * purposes, as the text up to and including the right-most directory separator. We
		 * do this in non-dynamic storage, for speed.
		*/
		ptr = g_utf8_strrchr(now, -1, G_DIR_SEPARATOR);
		if(ptr != NULL)
		{
			gchar	buf[1024];
			size_t	plen = ptr - now + 1;
			if(plen < sizeof buf)
			{
				memcpy(buf, now, plen);
				buf[plen] = '\0';
				/* Now, we have a UTF-8 prefix in buf, which is perfect. */
				completion_update(user, buf);
			}
		}
	}
	else
		evt_path_new(wid, user);
}

/* 2008-09-20 -	Cut away Tab handling, we now use the slower but more modern complete-as-you type style. */
static gboolean evt_path_key_press(GtkWidget *wid, GdkEventKey *evt, gpointer user)
{
	DirPane	*dp = user;

	if(evt->keyval == GDK_KEY_Escape)
		dp_path_unfocus(dp);
	return FALSE;
}

/* 1998-05-27 -	I had forgotten about the possibility for users to click in the path entry widget, and
**		by doing so circumventing my clever (hairy) flag system. This remedies that.
** 1998-05-29 -	Changed polarity of operation. Was broken. Sloppy me.
*/
static gboolean evt_path_focus(GtkWidget *wid, GdkEventFocus *ev, gpointer user)
{
	DirPane	*dp = user;

	kbd_context_detach(dp->main->gui->kbd_ctx, GTK_WINDOW(dp->main->gui->window));
	dp_activate(dp);

	g_signal_connect(G_OBJECT(wid), "key_press_event", G_CALLBACK(evt_path_key_press), dp);

	return FALSE;
}

static gboolean evt_path_unfocus(GtkWidget *wid, GdkEventFocus *ev, gpointer user)
{
	DirPane	*dp = user;

	kbd_context_attach(dp->main->gui->kbd_ctx, GTK_WINDOW(dp->main->gui->window));

	g_signal_handlers_disconnect_by_func(G_OBJECT(wid), G_CALLBACK(evt_path_key_press), dp);

	return FALSE;
}

/* 1998-12-19 -	The tiny little "hide" button was clicked. Act. */
static void evt_hide_clicked(GtkWidget *wid, gpointer user)
{
	DirPane	*dp = user;

	dp_activate(dp);
	dp->main->cfg.dp_format[dp->index].hide_allowed = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));
	csq_execute(dp->main, "DirRescan");
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-06 -	The given <dp> has been double-clicked. Time to execute some fun action,
**		namely the Default for the style of the clicked row.
*/
static void doubleclick(DirPane *dp)
{
	csq_execute(dp->main, "FileAction");
	if(chd_get_running(NULL) == NULL)	/* If command sequence finished, reset. Else keep around. */
		dp->dbclk_row = -1;
}

/* 1999-05-13 -	Simulate a double click on <row> in <dp>. Handy for use by the focusing
**		module.
*/
void dp_dbclk_row(DirPane *dp, gint row)
{
	if(row != -1)
	{
		dp->dbclk_row = row;
		doubleclick(dp);
	}
}

/* 1999-03-04 -	Rewritten. Now very much simpler. :) */
void dp_select(DirPane *dp, const DirRow2 *row)
{
	gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(dp->view)), (GtkTreeIter *) row);
}

/* 1999-03-04 -	Select all rows. Handy for the SelectAll command. Since selection management
**		is now largely done by the GtkCList widget for us, this is not rocket science.
** 2000-09-16 -	Noticed that using gtk_clist_select_all() reset the vertical scroll of the list
**		to zero, which annoyed me. This happens regardless of callback. Did a work around.
*/
void dp_select_all(DirPane *dp)
{
	gtk_tree_selection_select_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(dp->view)));
}

/* 1999-03-04 -	Rewritten in a lot simpler way. */
void dp_unselect(DirPane *dp, const DirRow2 *row)
{
	gtk_tree_selection_unselect_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(dp->view)), (GtkTreeIter *) row);
	/* FIXME: Doesn't handle double-click specifically, might be bad. */
}

/* 1999-03-04 -	Unselect all rows. Real simple. */
void dp_unselect_all(DirPane *dp)
{
	dp->dbclk_row = -1;
	gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(dp->view)));
}

/* 2000-03-18 -	Since the select/unselect functions no longer return success or failure, the check
**		is done explicitly here instead. Much better, really.
*/
void dp_toggle(DirPane *dp, const DirRow2 *row)
{
	if(gtk_tree_selection_iter_is_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(dp->view)), (GtkTreeIter *) row))
		dp_unselect(dp, row);
	else
		dp_select(dp, row);
}

/* 1998-06-18 -	This handles the situation when a user clicks on a row in a directory pane. Might envoke
**		action on files/dirs.
** 1998-06-04 -	Added support for a little menu popping up when the right button is pressed. First it changes
**		to the given pane.
** 1998-06-17 -	Added shift-sensing to extend the current selection. Not so crucial in my mind, since dragging
**		is so supported. But, nice never the less (and somewhat standard, so people might expect it).
** 1998-06-17 -	Added control-sensing to "extend" the current UNselection. Cool?
** 1999-03-05 -	Simplified since we now use GTK+'s built-in CList selection.
** 2003-10-08 -	Implemented Click-M-Click recognition.
** 2009-11-07 -	Simplified for GTK+ 2.0 implementation of the main dirpane list widget. Dropped customized selection
**		support (i.e. click and drag to multi-select); now it's always "system default" mode.
*/
static gboolean evt_dirpane_button_press(GtkWidget *wid, GdkEventButton *event, gpointer user)
{
	static DirPane	*last_dp = NULL;
	static GTimeVal	last_time;
	const gchar	*mcmd;
	DirPane		*dp = user;
	const gboolean	change = last_dp && dp != last_dp;
	GTimeVal	now;

	/* Handle commands mapped to mouse buttons. This includes the RMB menu, typically. */
	if((event->type == GDK_BUTTON_PRESS) && (mcmd = ctrl_mouse_map(dp->main->cfg.ctrlinfo, event)) != NULL)
	{
		dp_activate_queued(dp);
		last_dp = dp;
		evt_event_set((GdkEvent *) event);
		csq_execute(dp->main, mcmd);
		evt_event_clear(event->type);
		return TRUE;
	}

	/* Look for click-m-click, i.e. a rapid click in opposite pane after a selection. */
	g_get_current_time(&now);
	if(change)
	{
		const gchar *cmd = ctrl_clickmclick_get_cmdseq(dp->main->cfg.ctrlinfo);
		if(cmd && *cmd)
		{
			const gfloat	elapsed = 1E-6f * (now.tv_usec - last_time.tv_usec) + (now.tv_sec - last_time.tv_sec);

			if(elapsed < ctrl_clickmclick_get_delay(dp->main->cfg.ctrlinfo))
			{
				csq_execute(dp->main, cmd);
				return TRUE;
			}
		}
	}

	dp_activate_queued(dp);
	last_dp = dp;
	last_time = now;
	return FALSE;
}

/* 2009-11-07 -	User clicked a column; update sorting data. Much easier now with GTK+ 2. */
static void evt_pane_sort_column_clicked(GtkTreeSortable *sortable, gpointer user)
{
	DirPane		*dp = user;
	DPSort		*sort;
	gint		column;
	GtkSortType	type;

	dp_activate(dp);
	gtk_tree_sortable_get_sort_column_id(sortable, &column, &type);

	sort = &dp->main->cfg.dp_format[dp->index].sort;
	sort->content = column;
	sort->invert = (type == GTK_SORT_DESCENDING);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-27 -	Redisplay given pane. Will totally clear the GtkCList widget, and reformat
**		all row data into it. Does *not* resort the pane's contents; use (the new)
**		dp_resort() function for that. Also does *not* preserve the set of selected
**		rows or the vertical position; use dp_redisplay_preserve() for that.
*/
void dp_redisplay(DirPane *dp)
{
	g_warning("dp_redisplay() totally non-implemented");
}

/* 1999-04-27 -	Redisplay given pane, keeping both the selected set and the vertical position.
**		This is the one to use in most cases.
*/
void dp_redisplay_preserve(DirPane *dp)
{
	DHSel	*sel;
	gfloat	vpos;

	sel  = dph_dirsel_new(dp);
	vpos = dph_vpos_get(dp);
	dp_redisplay(dp);
	dph_vpos_set(dp, vpos);
	dph_dirsel_apply(dp, sel);
	dph_dirsel_destroy(sel);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-05-25 -	Moved this old workhorse into the new dirpane module, and discovered that
**		it wasn't commented. Lazy me. Also modified its prototype, to take the
**		DirPane to display rather than figuring it out from MainInfo.
*/
void dp_show_stats(DirPane *dp)
{
	MainInfo *min;

	if((min = dp->main) != NULL)
	{
		gchar		buf[256], selbuf[32] = "", totbuf[32] = "", *ptr = buf;
		const SelInfo	*sel = &dp->dir.sel;

		ptr += g_snprintf(ptr, sizeof buf, _("%u/%u dirs, %u/%u files"),
				  sel->num_dirs, dp->dir.tot_dirs,
				  sel->num_files, dp->dir.tot_files);
		sze_put_offset(selbuf, sizeof selbuf, sel->num_bytes,    SZE_AUTO, 1, ',');
		sze_put_offset(totbuf, sizeof totbuf, dp->dir.tot_bytes, SZE_AUTO, 1, ',');
		ptr += sprintf(ptr, _(" (%s/%s)"), selbuf, totbuf);
		if(dp->dir.fs.valid)
		{
			gchar	dbuf[32], pbuf[32];

			sze_put_offset(dbuf, sizeof dbuf, dp->dir.fs.fs_size - dp->dir.fs.fs_free, SZE_AUTO, 1, ',');
			g_snprintf(pbuf, sizeof pbuf, "%.1f%%",
				100.0 *
				((gdouble) (dp->dir.fs.fs_size - dp->dir.fs.fs_free)
				/
				(gdouble) dp->dir.fs.fs_size));
			ptr += sprintf(ptr, _(", %s (%s) used"), dbuf, pbuf);
			sze_put_offset(dbuf, sizeof dbuf, dp->dir.fs.fs_free, SZE_AUTO, 3, ',');
			ptr += sprintf(ptr, _(", %s free"), dbuf);
		}
		if(min->cfg.errors.display != ERR_DISPLAY_TITLEBAR)
			gtk_label_set_text(GTK_LABEL(min->gui->top), buf);
		else
			gui_set_main_title(dp->main, buf);
	}
}

/* 2010-08-03 -	Set the activate-status of the given pane's columns (that's what "col" refers to). Slighly hackish. */
static void dp_set_col_active(DirPane *dp, gboolean active)
{
	if(dp != NULL)
	{
		GtkTreeViewColumn	*column;
		guint			i;

		for(i = 0; (column = gtk_tree_view_get_column(GTK_TREE_VIEW(dp->view), i)) != NULL; i++)
		{
			GtkWidget	*header = gtk_tree_view_column_get_button(column);

			if(header != NULL)
 				gtk_widget_set_sensitive(header, active);
		}
		gtk_widget_set_name(dp->view, active ? "pane-current" : "pane");
	}
}

/* 2002-07-14 -	Do the pane rendering necessary to change active pane from <from> into <to>. */
static void activate_render(DirPane *from, DirPane *to)
{
	if(from != NULL)
		dp_set_col_active(from, FALSE);
	if(to != NULL)
	{
		dp_set_col_active(to, TRUE);
		gtk_widget_grab_focus(to->view);
		dp_show_stats(to);
	}
}

/* 1999-06-09 -	Activate <dp>, making it the source pane for all operations. Returns TRUE if the activation
**		meant that another pane was deactivated, FALSE if <dp> was already the activate pane.
*/
gboolean dp_activate(DirPane *dp)
{
	if(dp != NULL && dp->main->gui->cur_pane != dp)
	{
		gchar	*name;

		activate_render(dp->main->gui->cur_pane, dp);
		dp->main->gui->cur_pane = dp;
		if(dp->dir.root != NULL && (name = g_file_get_parse_name(dp->dir.root)) != NULL)
		{
			gchar	tbuf[256];

			g_snprintf(tbuf, sizeof tbuf, "%s - gentoo", name);
			win_window_set_title(dp->main->gui->window, tbuf);
			g_free(name);
		}
		return TRUE;
	}
	return FALSE;
}

/* 2002-07-13 -	This runs when GTK+ is idle, and does the pane activation rendering, once. */
static gint idle_activate(gpointer data)
{
	activate_render(the_activate_queue_info.old_cur, ((MainInfo *) data)->gui->cur_pane);
	g_source_remove(the_activate_queue_info.handler);
	the_activate_queue_info.handler = 0U;
	return 0;
}

/* 2002-07-13 -	Activate a pane, but queue the rendering until GTK+ is idle. This works around
**		a very annoying bug/misfeature which causes "ghost" pane scrolling to occur.
**		We only queue the rendering, since that seems to suffice, and we need to really
**		change gentoo's idea of "active pane" immediately or things break.
*/
static void dp_activate_queued(DirPane *dp)
{
	if(the_activate_queue_info.handler)
		g_source_remove(the_activate_queue_info.handler);
	the_activate_queue_info.handler = g_idle_add(idle_activate, dp->main);
	the_activate_queue_info.old_cur = dp->main->gui->cur_pane;
	dp->main->gui->cur_pane = dp;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-05 -	Answer whether given pane has one or more rows selected or not. */
gboolean dp_has_selection(DirPane *dp)
{
	if(dp->dbclk_row != -1)
		return TRUE;
	return gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(dp->view))) > 0 && !no_repeat;
}

/* 2011-01-03 -	Determine whether the given pane has a single selected row. */
gboolean dp_has_single_selection(const DirPane *dp)
{
	return gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(dp->view))) == 1;
}

/* 1999-03-06 -	Answer whether given <row> is currently selected. A bit more efficient
**		than getting the entire list through dp_get_selection(), but only if
**		you're not going to iterate the selection anyway.
*/
gboolean dp_is_selected(DirPane *dp, const DirRow2 *row)
{
	if(dp == NULL || row == NULL)
		return FALSE;
	return gtk_tree_selection_iter_is_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(dp->view)), (GtkTreeIter *) row);
}

static GSList * append_row(GSList *list, DirPane *dp, gint index)
{
	gchar		buf[16];
	GtkTreeIter	*iter;

	if((iter = g_malloc(sizeof *iter)) != NULL)
	{
		g_snprintf(buf, sizeof buf, "%d", index);
		if(gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(dp->dir.store), iter, buf))
			return g_slist_prepend(list, iter);
		else
			g_warning("**Unable to get iter for selection");
	}
	return list;
}

static void cb_append_row(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer user)
{
	GSList		**sel = user;
	GtkTreeIter	*di;

	if((di = g_malloc(sizeof *di)) != NULL)
	{
		*di = *iter;
		*sel = g_slist_prepend(*sel, di);
	}
}

/* 1999-03-04 -	Return a GSList of selected rows. Each item's data member is a DirRow.
**		Knows how to deal with a double click, too. This is going to be the new
**		interface for all commands.
** 1999-03-15 -	Now sorts the rows in address order, since otherwise the selection retains
**		the order in which it was done by the user, and that is simply confusing.
*/
GSList * dp_get_selection(DirPane *dp)
{
	GSList	*sel = NULL;

	no_repeat = FALSE;

	if(dp->dbclk_row != -1)
		sel = append_row(sel, dp, dp->dbclk_row);
	else
		gtk_tree_selection_selected_foreach(gtk_tree_view_get_selection(GTK_TREE_VIEW(dp->view)), cb_append_row, &sel);

	if(sel)
		fam_rescan_block();
	sel = g_slist_reverse(sel);

	return sel;
}

/* 1999-03-06 -	Get the "full" selection, regardless of whether there's a double clicked row or not.
**		This should only be used if you really know what you're doing, and never by actual
**		commands (which need the double-click support).
*/
GSList * dp_get_selection_full(const DirPane *dp)
{
	GSList	*sel = NULL;

	gtk_tree_selection_selected_foreach(gtk_tree_view_get_selection(GTK_TREE_VIEW(dp->view)), cb_append_row, &sel);
	if(sel)
		fam_rescan_block();
	return sel;
}

/* 1999-03-04 -	Free a selection list, handy when you're done traversing it. */
void dp_free_selection(GSList *sel)
{
	if(sel != NULL)
	{
		GSList	*iter;

		for(iter = sel; iter != NULL; iter = g_slist_next(iter))
			g_free(iter->data);
		g_slist_free(sel);
		fam_rescan_unblock();
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 2009-10-01 -	Count entries as they are read in. */
static void statistics_add_row(DirPane *dp, const GFileInfo *fi)
{
	if(g_file_info_get_file_type((GFileInfo *) fi) == G_FILE_TYPE_DIRECTORY)
		dp->dir.tot_dirs++;
	else
		dp->dir.tot_files++;
	dp->dir.tot_bytes += g_file_info_get_size((GFileInfo *) fi);
}

/* 2009-10-16 -	Add given row to the selection. */
static void selection_add_row(DirPane *dp, const GFileInfo *fi)
{
	if(g_file_info_get_file_type((GFileInfo *) fi) == G_FILE_TYPE_DIRECTORY)
		dp->dir.sel.num_dirs++;
	else
		dp->dir.sel.num_files++;
	dp->dir.sel.num_bytes += g_file_info_get_size((GFileInfo *) fi);
}

/* 1999-04-08 -	Clear the selected statistics. */
static void clear_selection_stats(SelInfo *sel)
{
	if(sel != NULL)
	{
		sel->num_dirs = 0;
		sel->num_files = 0;
		sel->num_bytes = 0;
	}
}

/* 1999-04-09 -	Clear the total statistics fields for <dp>. */
static void clear_total_stats(DirPane *dp)
{
	dp->dir.tot_files = dp->dir.tot_dirs = 0;
	dp->dir.tot_bytes = 0;
 	dp->dir.fs.valid = FALSE;
}

/* 1999-01-03 -	Clear the statistics for <dp>. Useful when the pane's path is about to change. */
static void clear_stats(DirPane *dp)
{
	clear_total_stats(dp);
	clear_selection_stats(&dp->dir.sel);
	dp->last_row = dp->last_row2 = -1;
}

/* 1999-04-08 -	Update the selection statistics for <dp>, by simply flushing them and recomputing from
**		scratch. Does NOT use dp_get_selection(), for performance reasons (this is run on every
**		unselection).
*/
static void update_selection_stats(DirPane *dp)
{
	GtkTreeModel	*model = dp_get_tree_model(dp);
	GtkTreeIter	iter;
	SelInfo		*sel = &dp->dir.sel;

	clear_selection_stats(sel);
	if(gtk_tree_model_get_iter_first(model, &iter))
	{
		GtkTreeSelection	*ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(dp->view));

		/* Don't use gtk_tree_selection_get_selected_rows(), it's kind of costly. */
		do
		{
			if(gtk_tree_selection_iter_is_selected(ts, &iter))
			{
				GFileInfo	*fi;

				gtk_tree_model_get(model, &iter, COL_INFO, &fi, -1);
				selection_add_row(dp, fi);
			}
		} while(gtk_tree_model_iter_next(model, &iter));
	}
}

/* 1999-04-09 -	Update the tot_XXX fields in <dp>'s directory statistics. Handy after a (Get|Clear)Size. */
void dp_update_stats(DirPane *dp)
{
	GtkTreeModel	*model = dp_get_tree_model(dp);
	GtkTreeIter	iter;

	clear_total_stats(dp);
	clear_selection_stats(&dp->dir.sel);
	if(gtk_tree_model_get_iter_first(model, &iter))
	{
		GtkTreeSelection	*ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(dp->view));

		do
		{
			GFileInfo	*fi;

			gtk_tree_model_get(model, &iter, COL_INFO, &fi, -1);
			statistics_add_row(dp, fi);
			if(gtk_tree_selection_iter_is_selected(ts, &iter))
				selection_add_row(dp, fi);
		} while(gtk_tree_model_iter_next(model, &iter));
	}
}

/* 1999-03-30 -	Update information about filesystem for given <dp>. */
static void update_fs_info(DirPane *dp)
{
	GFileInfo	*fi;

	if((fi = g_file_query_filesystem_info(dp->dir.root, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE "," G_FILE_ATTRIBUTE_FILESYSTEM_FREE, NULL, NULL)) != NULL)
	{
		dp->dir.fs.fs_size = g_file_info_get_attribute_uint64(fi, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
		dp->dir.fs.fs_free = g_file_info_get_attribute_uint64(fi, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
	}
	dp->dir.fs.valid = (fi != NULL) && dp->dir.fs.fs_size > 0;
}

void dp_path_clear(DirPane *dp)
{
	GtkListStore	*store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(dp->path)));

	gtk_list_store_clear(store);
}

void dp_path_focus(DirPane *dp)
{
	gtk_widget_grab_focus(dp->path);
}

void dp_path_unfocus(DirPane *dp)
{
	gtk_widget_grab_focus(dp->view);
}

/* 2013-07-09 -	Clear the FType column. This is useful when re-scanning panes after editing the
 *		FType definitions in the configuration window. It prevents crashes due to stale
 *		pointers being followed.
*/
static void untype_rows(DirPane *dp)
{
	GtkTreeModel	*model = dp_get_tree_model(dp);
	GtkTreeIter	iter;

	if(gtk_tree_model_get_iter_first(model, &iter))
	{
		do
		{
			gtk_list_store_set(GTK_LIST_STORE(model), &iter, COL_FTYPE, NULL, -1);
		} while(gtk_tree_model_iter_next(model, &iter));
	}
}

/* 2010-06-05 -	Internal "do it" routine to enter a new directory. Uses GIO error handling for all I/O. */
static gboolean do_enter_dir(DirPane *dp, const gchar *path, GError **err)
{
	GFile		*here;
	GFileEnumerator	*fe;

	if((here = g_vfs_parse_name(dp->main->vfs.vfs, path)) == NULL)
		return FALSE;

	if((fe = g_file_enumerate_children(here, "*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err)) != NULL)
	{
		GFileInfo	*fi, *tfi;
		GtkTreeIter	iter;

		if(dp->dir.root != NULL)
			g_object_unref(dp->dir.root);
		dp->dir.root = here;
		untype_rows(dp);	/* Clearing is not atomic, and can cause redraws. */
		gtk_list_store_clear(dp->dir.store);
		g_strlcpy(dp->dir.path, path, sizeof dp->dir.path);

		/* Decide whether the current directory is "local". */
		if((fi = g_file_query_filesystem_info(here, G_FILE_ATTRIBUTE_FILESYSTEM_USE_PREVIEW, NULL, err)) != NULL)
		{
			dp->dir.is_local = g_file_info_get_attribute_uint32(fi, G_FILE_ATTRIBUTE_FILESYSTEM_USE_PREVIEW) != G_FILESYSTEM_PREVIEW_TYPE_NEVER;
			g_object_unref(fi);
		}
		else
			dp->dir.is_local = TRUE;

		while((fi = g_file_enumerator_next_file(fe, NULL, err)) != NULL)
		{
			const gchar	*name = g_file_info_get_name(fi), *fn, *tname;
			guint32		flags;
			gchar		*ck;
			gunichar	initial;

			/* Apply classic Hide filtering rule. Nice. */
			if(!(dp->main->cfg.dir_filter(name) && fut_check_hide(dp, name)))
			{
				g_object_unref(fi);
				continue;
			}
			flags = (g_file_info_get_file_type(fi) != G_FILE_TYPE_DIRECTORY) ? DPRF_HAS_SIZE : 0;
			fn = g_file_info_get_display_name(fi);
			initial = g_utf8_get_char(fn);
			ck = g_utf8_collate_key_for_filename(fn, -1);
			/* The modern default is to be case-insensitive, glib's collation keys always are.
			 * For us bearded old-sk00l folks, let's implement a kinda-sorta case-sensitive
			 * collation key, based on the initial only. I think this will be Good Enough.
			 *
			 * It is a bit limited (@test and @TEST won't sort as you'd expect), but short.
			*/
			if(!dp->main->cfg.dp_format[dp->index].sort.nocase)
			{
				const gchar	ORDER_PUNCTUATION = '\1';
				const gchar	ORDER_UPPERCASE = '\2';
				const gchar	ORDER_LOWERCASE = '\3';

				const size_t	out = strlen(ck) + 2;
				gchar		*csck, *put;

				if((csck = g_malloc(out)) != NULL)
				{
					put = csck;
					if(g_unichar_ispunct(initial))
						*put++ = ORDER_PUNCTUATION;
					else if(g_unichar_isupper(initial))
						*put++ = ORDER_UPPERCASE;
					else if(g_unichar_islower(initial))
						*put++ = ORDER_LOWERCASE;
					memcpy(put, ck, out - 1);
					g_free(ck);
					ck = csck;
				}
			}
			/* If the local object looks like a symbolic link, chase down the link target too.
			 * This is not exactly Captain Slim on the memory side, but very handy especially
			 * since we want to do things like style links to directories like directories.
			 * Pre-GIO versions of gentoo did this too, by storing extra struct stats for links.
			 */
			if((tname = g_file_info_get_symlink_target(fi)) != NULL)
			{
				GFile	*target;

				/* Try to be clever and figure out how to interpret the link target text. */
				if(strstr(tname, "://") != NULL)	/* Does it look like an URI? */
				{
					target = g_vfs_get_file_for_uri(dp->main->vfs.vfs, tname);
				}	/* No, does it look like a relative path, then? */
				else if(strncmp(tname, "./", 2) == 0 || strncmp(tname, "..", 2) == 0)
				{
					target = g_file_resolve_relative_path(here, tname);
				}
				else	/* Assume just bare name, meaning link to child with same root. */
				{
					target = g_file_get_child(here, tname);
				}
				if(target != NULL)
				{
					GError	*terr = NULL;

					/* If we got something, try to query the info for it. */
					tfi = g_file_query_info(target, "standard::*", G_FILE_QUERY_INFO_NONE, NULL, &terr);
					g_object_unref(G_OBJECT(target));
					if(tfi != NULL && terr == NULL)
					{
						flags |= DPRF_LINK_EXISTS;
						if(g_file_info_get_file_type(tfi) == G_FILE_TYPE_DIRECTORY)
							flags |= DPRF_LINK_TO_DIR;
					}
					g_clear_error(&terr);
				}
					else
						tfi = NULL;
			}
			else
				tfi = NULL;

			gtk_list_store_insert_with_values(dp->dir.store, &iter, -1,
					COL_FILE, NULL,
					COL_INFO, fi,
					COL_FLAGS, flags,
					COL_FILENAME_COLLATE_KEY, ck,
					(tfi != NULL) ? COL_LINK_TARGET_INFO : -1, tfi,	/* Not too clever, I hope. */
					-1);
			statistics_add_row(dp, fi);
			g_free(ck);
			if(tfi)
				g_object_unref(G_OBJECT(tfi));
			g_object_unref(G_OBJECT(fi));
		}
		g_object_unref(fe);
	}
	if(fe != NULL)
	{
		GtkTreeIter	iter;

		typ_identify_begin(dp);
		if(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(dp->dir.store), &iter))
		{
			do
			{
				typ_identify(dp, &iter);
			} while(gtk_tree_model_iter_next(GTK_TREE_MODEL(dp->dir.store), &iter));
		}
		if(!typ_identify_end(dp))
			fe = NULL;
	}
	return fe != NULL;
}

/* 2003-11-14 -	Brand new version of this very old workhorse. Now reads in the directory in a single
**		pass, using realloc() to grow various buffers on the fly. Slightly more wasteful of
**		memory than the old two-pass version, but less code, more robust, and possibly a
**		little bit faster. Returns TRUE on success.
*/
gboolean dp_enter_dir(DirPane *dp, const gchar *path)
{
	GError		*err = NULL;
	gboolean	ok;

	clear_stats(dp);

	completion_clear(dp);

	the_sort = dp->main->cfg.dp_format[dp->index].sort;	/* Must be accessible by qsort() callbacks. */

	if((ok = do_enter_dir(dp, path, &err)) == TRUE)
	{
		const gboolean	has_parent = g_file_has_parent(dp->dir.root, NULL);

		update_fs_info(dp);
		gtk_widget_set_sensitive(dp->parent, has_parent);
		if(dp->hparent != NULL)
			gtk_widget_set_sensitive(dp->hparent, has_parent);
	}
	else if(err)
		err_set_gerror(dp->main, &err, path, NULL);

	return ok;
}

/* ----------------------------------------------------------------------------------------- */

/* 2002-07-20 -	Rescan contents of given pane.
** NOTE NOTE	This routine does NOT change the error status, i.e. it does not call anything
**		in the error module. This is important, since it's used in the exiting code
**		of generic commands, for instance.
** 2003-09-26 -	Don't activate <dp> for refresh, just do it anyway. Faster, hopefully safe.
*/
void dp_rescan(DirPane *dp)
{
	dph_state_save(dp);
	dp_enter_dir(dp, dp->dir.path);
	dph_state_restore(dp);
}

/* 2002-07-23 -	Special "weaker" version of dp_rescan(), with the added semantic difference that
**		this is only ever called as part of the "clean-up" *after* a command has finished.
**		The intent is to catch any changes to the pane done by the command, such as a
**		deleted, renamed, moved or otherwise changed file. The reason it has a separate
**		entrypoint is that with FAM, this needs to be stopped.
** 2009-11-15 -	Rewritten, now that "FAM" is actually just an alias for GIO monitoring.
*/
void dp_rescan_post_cmd(DirPane *dp)
{
	if(!fam_is_monitored(dp))
		dp_rescan(dp);
}

/* 2009-10-16 -	Rescans a single row of a pane. Handy for ClearSize, for instance. Not to be used for bulk reading. */
gboolean dp_rescan_row(DirPane *dp, const DirRow2 *row, GError **error)
{
	GFile		*file;
	GFileInfo	*fi = NULL;

	if(dp == NULL || row == NULL)
		return FALSE;
	if((file = dp_get_file_from_row(dp, row)) != NULL)
	{
		GtkTreeModel	*m = dp_get_tree_model(dp);

		if((fi = g_file_query_info(file, "*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error)) != NULL)
		{
			guint32	flags;

			/* Computes new flags value; clears HAS_SIZE for directories. */
			gtk_tree_model_get(m, (GtkTreeIter *) row, COL_FLAGS, &flags, -1);
			flags &= ~DPRF_HAS_SIZE;
			flags |= (g_file_info_get_file_type(fi) != G_FILE_TYPE_DIRECTORY) ? DPRF_HAS_SIZE : 0;
			gtk_list_store_set(GTK_LIST_STORE(m), (GtkTreeIter *) row, COL_INFO, fi, COL_FLAGS, flags, -1);
		}
		g_object_unref(file);
	}
	return fi != NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 2008-09-20 -	Sets the history strings, available in the combo box. This is to prevent
**		other modules (dirhistory, I'm looking at you!) from poking the widgets
**		directly. Abstract, hide, and so on.
** 2010-02-09 -	Converted to assume too much about dirhistory's internals. The list is now
**		supposed to point at structures, that begin with a gchar * URI. Yes.
*/
void dp_history_set(DirPane *dp, const GList *locations)
{
	GtkListStore	*store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(dp->path)));
	GtkTreeIter	iter;
	guint		i;

	gtk_list_store_clear(store);
	for(i = 0; locations != NULL; locations = g_list_next(locations))
	{
		const gchar	*name = dph_entry_get_parse_name(locations->data);

		if(*name)
		{
			gtk_list_store_insert_with_values(store, &iter, -1, 0, name, -1);
			++i;
		}
	}
	if(i > 0)
	{
		/* Was there any history set? If so, also jump to the first entry. */
		g_signal_handler_block(G_OBJECT(gtk_bin_get_child(GTK_BIN(dp->path))), dp->sig_path_activate);
		g_signal_handler_block(G_OBJECT(dp->path), dp->sig_path_changed);
		gtk_combo_box_set_active(GTK_COMBO_BOX(dp->path), 0);
		g_signal_handler_unblock(G_OBJECT(dp->path), dp->sig_path_changed);
		g_signal_handler_unblock(G_OBJECT(gtk_bin_get_child(GTK_BIN(dp->path))), dp->sig_path_activate);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 2009-07-04 -	Refreshes the split, making sure it's correctly sized according to current settings. */
void dp_split_refresh(MainInfo *min)
{
	GdkWindow	*pwin;
	gint		np, pos = -1;

	if(min == NULL || min->gui == NULL || min->gui->panes == NULL)
		return;
	if(!dp_realized(min))
		return;

	/* Inspect the size of the GtkPaned that holds the panes; the window's size
	** doesn't work for vertical due to buttons. This is slightly hackish.
	*/
	pwin = gtk_widget_get_window(min->gui->panes);
	np = min->cfg.dp_paning.orientation == DPORIENT_HORIZ ? gdk_window_get_width(pwin) : gdk_window_get_height(pwin);

	/* Compute the proper new position, depending on the active paning mode. */
	switch(min->cfg.dp_paning.mode)
	{
		case DPSPLIT_FREE:
			return;
		case DPSPLIT_RATIO:
			pos = np * min->cfg.dp_paning.value;
			break;
		case DPSPLIT_ABS_LEFT:
			pos = min->cfg.dp_paning.value;
			break;
		case DPSPLIT_ABS_RIGHT:
			pos = np - min->cfg.dp_paning.value;
			break;
	}
	if(pos >= 0)
	{
		g_signal_handler_block(G_OBJECT(min->gui->window), min->gui->sig_main_configure);
		g_signal_handler_block(G_OBJECT(min->gui->panes), min->gui->sig_pane_notify);
		gtk_paned_set_position(GTK_PANED(min->gui->panes), pos);
		g_signal_handler_unblock(G_OBJECT(min->gui->panes), min->gui->sig_pane_notify);
		g_signal_handler_unblock(G_OBJECT(min->gui->window), min->gui->sig_main_configure);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 2003-11-08 -	Add a pathwidgtry user. Indirect, just adds a function that is later called
**		(in dp_build()) to construct the widgetry. Returns key to use with show().
*/
guint dp_pathwidgetry_add(PageBuilder func)
{
	if(func && !g_slist_find(pathwidgetry_builders, func))
	{
		pathwidgetry_builders = g_slist_append(pathwidgetry_builders, func);
		return g_slist_length(pathwidgetry_builders);
	}
	return 0;
}

/* 2003-11-08 -	Change to a new page of dirpane path widgetry. Returns page. */
GtkWidget ** dp_pathwidgetry_show(DirPane *dp, guint key)
{
	GtkWidget	*page;

	if((page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(dp->notebook), key)) != NULL)
	{
		gtk_notebook_set_current_page(GTK_NOTEBOOK(dp->notebook), key);
		return g_object_get_data(G_OBJECT(page), "dp-pathwidgetry");
	}
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

GtkTreeModel * dp_get_tree_model(const DirPane *dp)
{
	return (dp != NULL) ? GTK_TREE_MODEL(dp->dir.store) : NULL;
}

GFile * dp_get_file_from_row(const DirPane *dp, const DirRow2 *row)
{
	if(dp == NULL || row == NULL)
		return NULL;
	return g_file_get_child(dp->dir.root, dp_row_get_name(GTK_TREE_MODEL(dp->dir.store), row));
}

GFile * dp_get_file_from_name(const DirPane *dp, const gchar *name)
{
	if(dp == NULL || name == NULL)
		return NULL;
	return g_file_get_child(dp->dir.root, name);
}

GFile * dp_get_file_from_name_display(const DirPane *dp, const gchar *name)
{
	if(dp == NULL || name == NULL)
		return NULL;
	return g_file_get_child_for_display_name(dp->dir.root, name, NULL);
}

static void row_emit_changed(GtkTreeModel *model, const DirRow2 *row)
{
	GtkTreePath	*path;

	if((path = gtk_tree_model_get_path(model, (GtkTreeIter *) row)) != NULL)
	{
		gtk_tree_model_row_changed(model, path, (GtkTreeIter *) row);
		gtk_tree_path_free(path);
	}
}

void dp_row_set_ftype(GtkTreeModel *model, const DirRow2 *row, FType *ft)
{
	gtk_list_store_set(GTK_LIST_STORE(model), (GtkTreeIter *) row, COL_FTYPE, ft, -1);
	row_emit_changed(model, row);
}

void dp_row_set_size(GtkTreeModel *model, const DirRow2 *row, guint64 size)
{
	GFileInfo	*fi;

	gtk_tree_model_get(model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	g_file_info_set_size(fi, size);
	/* Notify the owning GtkTreeModel that the data has changed; refreshes TreeViews. */
	row_emit_changed(model, row);
}

void dp_row_set_flag(GtkTreeModel *model, const DirRow2 *row, guint32 mask)
{
	guint32	old;

	gtk_tree_model_get(model, (GtkTreeIter *) row, COL_FLAGS, &old, -1);
	gtk_list_store_set(GTK_LIST_STORE(model), (GtkTreeIter *) row, COL_FLAGS, old | mask, -1);
}

const gchar * dp_row_get_name(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_name(fi);
}

const gchar * dp_row_get_name_display(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_display_name(fi);
}

const gchar * dp_row_get_name_edit(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_edit_name(fi);
}

FType * dp_row_get_ftype(const GtkTreeModel *model, const DirRow2 *row)
{
	FType	*ft = NULL;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_FTYPE, &ft, -1);
	return ft;
}

/* 2010-11-23 -	Returns the type of the object in the indicated row, or its target if it's a symlink
 *		and target is true. If it's a broken symlink, G_FILE_TYPE_UNKNOWN is returned.
*/
GFileType dp_row_get_file_type(const GtkTreeModel *model, const DirRow2 *row, gboolean target)
{
	guint32		flags;
	GFileInfo	*fi, *tfi;

	/* On the assumption that this call is somewhat expensive, grab data for both if-branches. */
	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_FLAGS, &flags, COL_INFO, &fi, COL_LINK_TARGET_INFO, &tfi, -1);
	if(target && g_file_info_get_file_type(fi) == G_FILE_TYPE_SYMBOLIC_LINK)
	{
		if((flags & DPRF_LINK_EXISTS) && tfi != NULL)
			return g_file_info_get_file_type(tfi);
		return G_FILE_TYPE_UNKNOWN;
	}
	return g_file_info_get_file_type(fi);
}

goffset dp_row_get_size(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_size(fi);
}

guint64 dp_row_get_blocks(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_attribute_uint64(fi, G_FILE_ATTRIBUTE_UNIX_BLOCKS);
}

guint64 dp_row_get_blocksize(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_attribute_uint64(fi, G_FILE_ATTRIBUTE_UNIX_BLOCKS) * g_file_info_get_attribute_uint32(fi, G_FILE_ATTRIBUTE_UNIX_BLOCK_SIZE);
}

guint32 dp_row_get_mode(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_attribute_uint32(fi, G_FILE_ATTRIBUTE_UNIX_MODE);
}

guint64 dp_row_get_time_accessed(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_attribute_uint64(fi, G_FILE_ATTRIBUTE_TIME_ACCESS);
}

guint64 dp_row_get_time_changed(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_attribute_uint64(fi, G_FILE_ATTRIBUTE_TIME_CHANGED);
}

guint64 dp_row_get_time_created(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_attribute_uint64(fi, G_FILE_ATTRIBUTE_TIME_CREATED);
}

guint64 dp_row_get_time_modified(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_attribute_uint64(fi, G_FILE_ATTRIBUTE_TIME_MODIFIED);
}

guint32 dp_row_get_device(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_attribute_uint32(fi, G_FILE_ATTRIBUTE_UNIX_RDEV);
}

guint32 dp_row_get_gid(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_attribute_uint32(fi, G_FILE_ATTRIBUTE_UNIX_GID);
}

guint32 dp_row_get_uid(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_attribute_uint32(fi, G_FILE_ATTRIBUTE_UNIX_UID);
}

guint32 dp_row_get_nlink(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_attribute_uint32(fi, G_FILE_ATTRIBUTE_UNIX_NLINK);
}

const gchar * dp_row_get_link_target(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_attribute_byte_string(fi, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET);
}

gboolean dp_row_get_flags(const GtkTreeModel *model, const DirRow2 *row, guint32 mask)
{
	guint32	flags;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_FLAGS, &flags, -1);
	return (flags & mask) == mask;
}

gboolean dp_row_get_can_read(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_attribute_boolean(fi, G_FILE_ATTRIBUTE_ACCESS_CAN_READ);
}

gboolean dp_row_get_can_write(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_attribute_boolean(fi, G_FILE_ATTRIBUTE_ACCESS_CAN_READ);
}

gboolean dp_row_get_can_execute(const GtkTreeModel *model, const DirRow2 *row)
{
	GFileInfo	*fi;

	gtk_tree_model_get((GtkTreeModel *) model, (GtkTreeIter *) row, COL_INFO, &fi, -1);
	return g_file_info_get_attribute_boolean(fi, G_FILE_ATTRIBUTE_ACCESS_CAN_EXECUTE);
}

/* ----------------------------------------------------------------------------------------- */

/* 2009-11-02 -	Porting of the old qsort_result() function, adjusts comparison result to implement directory/file-grouping. */
static gint sort_result(gint r, GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, const GFileInfo *fia, const GFileInfo *fib)
{
	guint32		flags;
	gboolean	da, db;

	/* Did the two rows compare equal? Then resolve by checking the names. */
	if(r == 0)
	{
		gchar	*cka, *ckb;

		/* Read out the previously created collation keys, and just compare them. */
		gtk_tree_model_get(model, a, COL_FILENAME_COLLATE_KEY, &cka, -1);
		gtk_tree_model_get(model, b, COL_FILENAME_COLLATE_KEY, &ckb, -1);
		r = strcmp(cka, ckb);
		/* FIXME: This feels highly inefficient ... To work around, column must be G_TYPE_POINTER. */
		g_free(ckb);
		g_free(cka);
	}

	/* Resolve type of the row. If not immediate directory, it might be a symlink to one. */
	da = g_file_info_get_file_type((GFileInfo *) fia) == G_FILE_TYPE_DIRECTORY;
	if(!da)
	{
		gtk_tree_model_get(model, a, COL_FLAGS, &flags, -1);
		da = (flags & DPRF_LINK_TO_DIR) != 0;
	}
	db = g_file_info_get_file_type((GFileInfo *) fib) == G_FILE_TYPE_DIRECTORY;
	if(!db)
	{
		gtk_tree_model_get(model, b, COL_FLAGS, &flags, -1);
		db = (flags & DPRF_LINK_TO_DIR) != 0;
	}

	switch(the_sort.mode)
	{
		case DPS_DIRS_FIRST:
			if(da == db)
				return r;
			else if(da)
				return -1;
			return 1;
		case DPS_DIRS_LAST:
			if(da == db)
				return r;
			else if(db)
				return -1;
			return 1;
		case DPS_DIRS_MIXED:
			return r;
	}
	return r;		/* I bet this doesn't ever run. Gcc doesn't. */
}

/* 2009-11-04 -	Return sort result, generating initial value by comparing <va> and <vb>. */
static gint sort_result_uint32(guint32 va, guint32 vb, GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, const GFileInfo *fia, const GFileInfo *fib)
{
	if(va < vb)
		return sort_result(-1, model, a, b, fia, fib);
	else if(va > vb)
		return sort_result(1, model, a, b, fia, fib);
	return sort_result(0, model, a, b, fia, fib);
}

/* 2009-11-04 -	Return sort result, generating initial value by comparing <va> and <vb>. */
static gint sort_result_uint64(guint64 va, guint64 vb, GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, const GFileInfo *fia, const GFileInfo *fib)
{
	if(va < vb)
		return sort_result(-1, model, a, b, fia, fib);
	else if(va > vb)
		return sort_result(1, model, a, b, fia, fib);
	return sort_result(0, model, a, b, fia, fib);
}

/* 2009-11-02 -	Compare by name. This is assumed to be the most common case, so it should be quick. */
static gint cmp_name(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	r = sort_result(0, model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

/* 2009-11-04 -	Compare by size. */
static gint cmp_size(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	guint64		sa, sb;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	sa = g_file_info_get_size(fia);
	sb = g_file_info_get_size(fib);
	r = sort_result_uint64(sa, sb, model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

/* 2009-11-04 -	Compare access times. */
static gint cmp_atime(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	guint64		ta, tb;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	ta = g_file_info_get_attribute_uint64(fia, G_FILE_ATTRIBUTE_TIME_ACCESS);
	tb = g_file_info_get_attribute_uint64(fib, G_FILE_ATTRIBUTE_TIME_ACCESS);
	r = sort_result_uint64(ta, tb, model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

/* 2009-11-04 -	Compare creation times. */
static gint cmp_crtime(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	guint64		ta, tb;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	ta = g_file_info_get_attribute_uint64(fia, G_FILE_ATTRIBUTE_TIME_CREATED);
	tb = g_file_info_get_attribute_uint64(fib, G_FILE_ATTRIBUTE_TIME_CREATED);
	r = sort_result_uint64(ta, tb, model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

/* 2009-11-04 -	Compare modification times. */
static gint cmp_mtime(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	guint64		ta, tb;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	ta = g_file_info_get_attribute_uint64(fia, G_FILE_ATTRIBUTE_TIME_MODIFIED);
	tb = g_file_info_get_attribute_uint64(fib, G_FILE_ATTRIBUTE_TIME_MODIFIED);
	r = sort_result_uint64(ta, tb, model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

/* 2010-10-19 -	Compare changed times. */
static gint cmp_chtime(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	guint64		ta, tb;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	ta = g_file_info_get_attribute_uint64(fia, G_FILE_ATTRIBUTE_TIME_CHANGED);
	tb = g_file_info_get_attribute_uint64(fib, G_FILE_ATTRIBUTE_TIME_CHANGED);
	r = sort_result_uint64(ta, tb, model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

static gint cmp_device(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	guint32		da, db;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	da = g_file_info_get_attribute_uint32(fia, G_FILE_ATTRIBUTE_UNIX_RDEV);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	db = g_file_info_get_attribute_uint32(fib, G_FILE_ATTRIBUTE_UNIX_RDEV);
	r = sort_result_uint32(da, db, model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

static gint cmp_device_major(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	guint32		da, db;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	da = g_file_info_get_attribute_uint32(fia, G_FILE_ATTRIBUTE_UNIX_RDEV);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	db = g_file_info_get_attribute_uint32(fib, G_FILE_ATTRIBUTE_UNIX_RDEV);
	r = sort_result_uint32(da >> 8, db >> 8, model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

static gint cmp_device_minor(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	guint32		da, db;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	da = g_file_info_get_attribute_uint32(fia, G_FILE_ATTRIBUTE_UNIX_RDEV);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	db = g_file_info_get_attribute_uint32(fib, G_FILE_ATTRIBUTE_UNIX_RDEV);
	r = sort_result_uint32(da & 255, db & 255, model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

static gint cmp_gid_num(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	guint32		ga, gb;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	ga = g_file_info_get_attribute_uint32(fia, G_FILE_ATTRIBUTE_UNIX_GID);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	gb = g_file_info_get_attribute_uint32(fib, G_FILE_ATTRIBUTE_UNIX_GID);
	r = sort_result_uint32(ga, gb, model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

static gint cmp_gid_str(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	guint32		ga, gb;
	const gchar	*gas, *gbs;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	ga = g_file_info_get_attribute_uint32(fia, G_FILE_ATTRIBUTE_UNIX_GID);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	gb = g_file_info_get_attribute_uint32(fib, G_FILE_ATTRIBUTE_UNIX_GID);
	if((gas = usr_lookup_gname(ga)) != NULL && (gbs = usr_lookup_gname(gb)) != NULL)
		r = sort_result(strcmp(gas, gbs), model, a, b, fia, fib);
	else
		r = sort_result_uint32(ga, gb, model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

static gint cmp_uid_num(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	guint32		ua, ub;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	ua = g_file_info_get_attribute_uint32(fia, G_FILE_ATTRIBUTE_UNIX_UID);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	ub = g_file_info_get_attribute_uint32(fib, G_FILE_ATTRIBUTE_UNIX_UID);
	r = sort_result_uint32(ua, ub, model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

static gint cmp_uid_str(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	guint32		ua, ub;
	const gchar	*uas, *ubs;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	ua = g_file_info_get_attribute_uint32(fia, G_FILE_ATTRIBUTE_UNIX_UID);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	ub = g_file_info_get_attribute_uint32(fib, G_FILE_ATTRIBUTE_UNIX_UID);
	if((uas = usr_lookup_uname(ua)) != NULL && (ubs = usr_lookup_gname(ub)) != NULL)
		r = sort_result(strcmp(uas, ubs), model, a, b, fia, fib);
	else
		r = sort_result_uint32(ua, ub, model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

static gint cmp_mode_num(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	guint32		ma, mb;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	ma = g_file_info_get_attribute_uint32(fia, G_FILE_ATTRIBUTE_UNIX_MODE);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	mb = g_file_info_get_attribute_uint32(fib, G_FILE_ATTRIBUTE_UNIX_MODE);
	r = sort_result_uint32(ma, mb, model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

static gint cmp_mode_str(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	gint		r;
	char		mab[16], mbb[16];

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	stu_mode_to_text(mab, sizeof mab, g_file_info_get_attribute_uint32(fia, G_FILE_ATTRIBUTE_UNIX_MODE));
	stu_mode_to_text(mbb, sizeof mbb, g_file_info_get_attribute_uint32(fib, G_FILE_ATTRIBUTE_UNIX_MODE));
	r = sort_result(strcmp(mab, mbb), model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

static gint cmp_nlink(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	guint32		na, nb;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	na = g_file_info_get_attribute_uint32(fia, G_FILE_ATTRIBUTE_UNIX_NLINK);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	nb = g_file_info_get_attribute_uint32(fib, G_FILE_ATTRIBUTE_UNIX_NLINK);
	r = sort_result_uint32(na, nb, model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

static gint cmp_type(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	GFileInfo	*fia, *fib;
	FType		*ta, *tb;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, COL_FTYPE, &ta, -1);
	gtk_tree_model_get(model, b, COL_INFO, &fib, COL_FTYPE, &tb, -1);
	if(ta != NULL && tb != NULL)
		r = sort_result(strcmp(ta->name, tb->name), model, a, b, fia, fib);
	else
		r = sort_result(0, model, a, b, fia, fib);
	g_object_unref(fib);
	g_object_unref(fia);

	return r;
}

static gint cmp_uri(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user)
{
	DirPane		*dp = user;
	GFileInfo	*fia, *fib;
	GFile		*fa, *fb;
	gint		r;

	gtk_tree_model_get(model, a, COL_INFO, &fia, -1);
	fa = dp_get_file_from_row(dp, a);
	gtk_tree_model_get(model, b, COL_INFO, &fib, -1);
	fb = dp_get_file_from_row(dp, b);
	if(fa != NULL && fb != NULL)
	{
		gchar	*ua, *ub;

		ua = g_file_get_uri(fa);
		ub = g_file_get_uri(fb);
		r = sort_result(strcmp(ua, ub), model, a, b, fia, fib);
		g_free(ub);
		g_free(ua);
	}
	else
		r = sort_result(0, model, a, b, fia, fib);
	g_object_unref(fb);
	g_object_unref(fib);
	g_object_unref(fa);
	g_object_unref(fia);

	return r;

}

static void evt_row_activated(GtkWidget *wid, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user)
{
	dp_dbclk_row(user, gtk_tree_path_get_indices(path)[0]);
}

/* 2009-10-01 -	The selection has somehow changed -- so recompute statistics display. */
static void evt_selection_changed(GtkTreeSelection *ts, gpointer user)
{
	update_selection_stats(user);
	dp_show_stats(user);
}

static void dp_build_list(DirPane *dp, const DPFormat *fmt)
{
	const struct {
		DPContent		content;
		GtkTreeIterCompareFunc	func;
	} sorters[] = {
			{ DPC_NAME, cmp_name },
			{ DPC_SIZE, cmp_size }, { DPC_BLOCKS, cmp_size }, { DPC_BLOCKSIZE, cmp_size },
			{ DPC_ATIME, cmp_atime },
			{ DPC_CRTIME, cmp_crtime },
			{ DPC_MTIME, cmp_mtime },
			{ DPC_CHTIME, cmp_chtime },
			{ DPC_DEVICE, cmp_device }, { DPC_DEVMAJ, cmp_device_major}, { DPC_DEVMIN, cmp_device_minor },
			{ DPC_GIDNUM, cmp_gid_num }, { DPC_GIDSTR, cmp_gid_str },
			{ DPC_UIDNUM, cmp_uid_num }, { DPC_UIDSTR, cmp_uid_str },
			{ DPC_MODENUM, cmp_mode_num }, { DPC_MODESTR, cmp_mode_str },
			{ DPC_NLINK, cmp_nlink },
			{ DPC_ICON, cmp_type } /* This might be unexpected? */,
			{ DPC_TYPE, cmp_type },
			{ DPC_URI_NOFILE, cmp_uri },
	};
	gint	i;

	dp->dir.store = gtk_list_store_new(COL_NUMBER_OF_COLUMNS, G_TYPE_OBJECT, G_TYPE_OBJECT, G_TYPE_POINTER, G_TYPE_STRING, G_TYPE_OBJECT, G_TYPE_UINT );
	for(i = 0; i < sizeof sorters / sizeof *sorters; i++)
		gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(dp->dir.store), sorters[i].content, sorters[i].func, dp, NULL);

	dp->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(dp->dir.store));
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(dp->view), -1);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(dp->view), FALSE);

	/* Connect signals. */
	g_signal_connect(G_OBJECT(dp->view), "button_press_event", G_CALLBACK(evt_dirpane_button_press), dp);
	g_signal_connect(G_OBJECT(dp->view), "row_activated", G_CALLBACK(evt_row_activated), dp);
	gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(dp->view)), GTK_SELECTION_MULTIPLE);
	gtk_tree_view_set_rubber_banding(GTK_TREE_VIEW(dp->view), fmt->rubber_banding);
	dp->sig_sel_changed = g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(dp->view))), "changed", G_CALLBACK(evt_selection_changed), dp);

	for(i = 0; i < fmt->num_columns; i++)
	{
		GtkCellRenderer		*cr;
		GtkTreeCellDataFunc	cdf;
		gpointer		user;
		GtkWidget		*lab;

		/* Ask the formatting module for a cell data function. */
		cdf = dpf_get_cell_data_func(dp, i, &user);
		if(cdf != NULL)
		{
			GtkTreeViewColumn	*tc;
			gfloat			xa;

			switch(fmt->format[i].content)
			{
			case DPC_NAME:
			case DPC_SIZE:
			case DPC_BLOCKS:
			case DPC_ATIME:
			case DPC_CRTIME:
			case DPC_MTIME:
			case DPC_CHTIME:
			case DPC_MODESTR:
			case DPC_MODENUM:
			case DPC_UIDNUM:
			case DPC_UIDSTR:
			case DPC_GIDNUM:
			case DPC_GIDSTR:
			case DPC_NLINK:
			case DPC_DEVICE:
			case DPC_DEVMIN:
			case DPC_DEVMAJ:
			case DPC_TYPE:
			case DPC_URI_NOFILE:
				cr = gtk_cell_renderer_text_new();
				break;
			case DPC_ICON:
				cr = gtk_cell_renderer_pixbuf_new();
				break;
			default:
				g_warning("Unsupported dirpane content type %d detected", fmt->format[i].content);
				continue;
			}

			tc = gtk_tree_view_column_new();
			lab = gtk_label_new(fmt->format[i].title);
			gtk_widget_show_all(lab);
			gtk_tree_view_column_set_widget(tc, lab);
			/* Compute legacy justification into alignment, and set for both header and content. */
			switch(fmt->format[i].just)
			{
			case GTK_JUSTIFY_LEFT:
				xa = 0.f;
				break;
			case GTK_JUSTIFY_CENTER:
				xa = 0.5f;
				break;
			case GTK_JUSTIFY_RIGHT:
				xa = 1.0f;
				break;
			default:
				xa = 0.5f;
			}
			gtk_tree_view_column_set_alignment(tc, xa);		/* Header. */
			g_object_set(cr, "xalign", xa, NULL);			/* Content. */
			g_object_set_data(G_OBJECT(cr), "main", dp->main);	/* Always handy. */
			/* Set the content ID as the sort ID; they're unique and all. */
			gtk_tree_view_column_set_sort_column_id(tc, fmt->format[i].content);
			/* Set the size, which we try to control ourselves. */
			gtk_tree_view_column_set_min_width(tc, fmt->format[i].width);
			gtk_tree_view_column_set_sizing(tc, GTK_TREE_VIEW_COLUMN_FIXED);
			gtk_tree_view_column_set_resizable(tc, FALSE);
			gtk_tree_view_column_set_expand(tc, FALSE);
			gtk_tree_view_append_column(GTK_TREE_VIEW(dp->view), tc);
			gtk_tree_view_column_pack_start(tc, cr, TRUE);
			/* Expose the column index. */
			g_object_set_data(G_OBJECT(tc), "index", GINT_TO_POINTER(i));
			/* This must be done after column is appended. */
			gtk_tree_view_column_set_cell_data_func(tc, cr, cdf, user, NULL);
		}
	}
	/* Set the user's preferred sorting column and order. */
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(dp->dir.store), fmt->sort.content, fmt->sort.invert ? GTK_SORT_DESCENDING : GTK_SORT_ASCENDING);
	g_signal_connect(G_OBJECT(GTK_TREE_SORTABLE(dp->dir.store)), "sort_column_changed", G_CALLBACK(evt_pane_sort_column_clicked), dp);
	gtk_container_add(GTK_CONTAINER(dp->scwin), dp->view);
}

/* 1998-05-23 -	Rewrote this one, now encloses stuff in a frame, which provides a fairly nice way to
**		indicate current directory. Now also uses the new dirpane formatting/config stuff.
** 1998-06-26 -	Cut away the code above (dp_build_list), making this function a lot leaner.
** 1998-09-06 -	Frame removed, since I've become cool enough to use styles to just change the
**		background color of the pane's column buttons. Looks great!
** 1998-10-26 -	Now supports configuring the position of the path entry (above or below).
*/
GtkWidget * dp_build(MainInfo *min, DPFormat *fmt, DirPane *dp)
{
	GtkWidget	*hbox, *ihbox;
	GtkListStore	*model;
	GSList		*iter;

	if(pane_selected != NULL)
	{
		g_object_unref(pane_selected);
		pane_selected = NULL;
	}
	if(focus_style != NULL)
	{
		g_object_unref(focus_style);
		focus_style = NULL;
	}
	if(dp == NULL)
		return NULL;

	dp->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	/* "Internal" hbox, holds the scrolled window of the pane and the optional
	** "huge" parent button. If the latter is disabled, it's redundant, but hey.
	*/
	ihbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	dp->scwin = gtk_scrolled_window_new(NULL, NULL);
	if(fmt->sbar_pos == SBP_LEFT)
		gtk_scrolled_window_set_placement(GTK_SCROLLED_WINDOW(dp->scwin), GTK_CORNER_TOP_RIGHT);
	else if(fmt->sbar_pos == SBP_RIGHT)
		gtk_scrolled_window_set_placement(GTK_SCROLLED_WINDOW(dp->scwin), GTK_CORNER_TOP_LEFT);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(dp->scwin), GTK_POLICY_AUTOMATIC, fmt->scrollbar_always ? GTK_POLICY_ALWAYS : GTK_POLICY_AUTOMATIC);

	dp_build_list(dp, fmt);

	dp->notebook = gtk_notebook_new();
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(dp->notebook), FALSE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(dp->notebook), FALSE);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	dp->parent = gtk_button_new_from_icon_name("go-up", GTK_ICON_SIZE_MENU);
	gtk_widget_set_can_focus(dp->parent, FALSE);
	g_signal_connect(G_OBJECT(dp->parent), "clicked", G_CALLBACK(evt_parent_clicked), dp);
	gtk_box_pack_start(GTK_BOX(hbox), dp->parent, FALSE, FALSE, 0);
	gtk_widget_set_tooltip_text(dp->parent, _("Move up to the parent directory"));
	dp->menu_top	 = NULL;
	dp->menu_action  = NULL;
	dp->mitem_action = NULL;

	model = gtk_list_store_new(1, G_TYPE_STRING);
	dp->path = gtk_combo_box_new_with_model_and_entry(GTK_TREE_MODEL(model));
	gtk_combo_box_set_entry_text_column(GTK_COMBO_BOX(dp->path), 0);

	dp->complete.compl = gtk_entry_completion_new();
	model = gtk_list_store_new(1, G_TYPE_STRING);
	gtk_entry_completion_set_model(dp->complete.compl, GTK_TREE_MODEL(model));
	gtk_entry_completion_set_text_column(dp->complete.compl, 0);
	gtk_entry_completion_set_inline_completion(dp->complete.compl, TRUE);
	gtk_entry_completion_set_popup_single_match(dp->complete.compl, FALSE);
	gtk_entry_completion_set_match_func(dp->complete.compl, completion_match_function, dp, NULL);
	gtk_entry_set_completion(DP_ENTRY(dp), dp->complete.compl);
	g_signal_connect(G_OBJECT(gtk_bin_get_child(GTK_BIN(dp->path))), "focus_in_event", G_CALLBACK(evt_path_focus), dp);
	g_signal_connect(G_OBJECT(gtk_bin_get_child(GTK_BIN(dp->path))), "focus_out_event", G_CALLBACK(evt_path_unfocus), dp);
	dp->sig_path_activate = g_signal_connect(G_OBJECT(gtk_bin_get_child(GTK_BIN(dp->path))), "activate", G_CALLBACK(evt_path_new), dp);
	dp->sig_path_changed = g_signal_connect(G_OBJECT(dp->path), "changed", G_CALLBACK(evt_path_changed), dp);

	gtk_box_pack_start(GTK_BOX(hbox), dp->path, TRUE, TRUE, 0);
	gtk_widget_set_tooltip_text(dp->path, _("Enter path, then press Return to go there"));
	dp->hide = gtk_toggle_button_new_with_label(_("H"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dp->hide), fmt->hide_allowed);
	g_signal_connect(G_OBJECT(dp->hide), "clicked", G_CALLBACK(evt_hide_clicked), dp);
	gtk_box_pack_start(GTK_BOX(hbox), dp->hide, FALSE, FALSE, 0);
	gtk_widget_set_tooltip_text(dp->hide, _("Click to enable/disable Hide rule (When pressed in, the hide rule is active, and matching entries are hidden)"));
	gtk_notebook_append_page(GTK_NOTEBOOK(dp->notebook), hbox, NULL);

	/* If we have any registered pathwidgetry builders, go through and add their output to notebook. */
	for(iter = pathwidgetry_builders; iter != NULL; iter = g_slist_next(iter))
	{
		PageBuilder	func = iter->data;
		GtkWidget	**page;

		if(func && (page = func(min)) != NULL)
		{
			g_object_set_data(G_OBJECT(*page), "dp-pathwidgetry", page);
			gtk_notebook_append_page(GTK_NOTEBOOK(dp->notebook), *page, NULL);
		}
	}
	if(fmt->huge_parent)
	{
		dp->hparent = gtk_button_new();
		gtk_button_set_relief(GTK_BUTTON(dp->hparent), GTK_RELIEF_NONE);
		g_signal_connect(G_OBJECT(dp->hparent), "clicked", G_CALLBACK(evt_hugeparent_clicked), dp);
		gtk_widget_set_tooltip_text(dp->hparent, _("Move up to the parent directory"));
		gtk_widget_set_can_focus(dp->hparent, FALSE);
		if(dp->index == 0)
		{
			gtk_box_pack_start(GTK_BOX(ihbox), dp->hparent, FALSE, FALSE, 0);
			gtk_box_pack_start(GTK_BOX(ihbox), dp->scwin, TRUE, TRUE, 0);
		}
		else
		{
			gtk_box_pack_start(GTK_BOX(ihbox), dp->scwin, TRUE, TRUE, 0);
			gtk_box_pack_start(GTK_BOX(ihbox), dp->hparent, FALSE, FALSE, 0);
		}
	}
	else
	{
		gtk_box_pack_start(GTK_BOX(ihbox), dp->scwin, TRUE, TRUE, 0);
		dp->hparent = NULL;
	}

	if(fmt->path_above)
	{
		gtk_box_pack_start(GTK_BOX(dp->vbox), dp->notebook, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(dp->vbox), ihbox, TRUE, TRUE, 0);
	}
	else
	{
		gtk_box_pack_start(GTK_BOX(dp->vbox), ihbox, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(dp->vbox), dp->notebook, FALSE, FALSE, 0);
	}

	if(fmt->set_font)
	{
		PangoFontDescription	*fdesc;

		if((fdesc = pango_font_description_from_string(fmt->font_name)) != NULL)
		{
			gtk_widget_override_font(dp->view, fdesc);
			pango_font_description_free(fdesc);
		}
	}

	gtk_widget_show_all(dp->vbox);
	dp->dbclk_row = -1;
	clear_stats(dp);

	return dp->vbox;
}
