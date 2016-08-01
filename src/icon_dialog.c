/*
** 1999-05-24 -	Broke this functionality out from the style configuration module since, er,
**		I felt like it. Rewrote it in the process, too. :) Uses the sligtly more
**		modern dialog module interface, thus returns directly the name of the icon
**		that was selected (or NULL if user cancels).
** 1999-06-19 -	Changes for the new dialog module.
*/

#include "gentoo.h"

#include "iconutil.h"
#include "guiutil.h"
#include "strutil.h"

#include "dialog.h"
#include "icon_dialog.h"

/* ----------------------------------------------------------------------------------------- */

/* Column indices in our list model. */
enum {
	COLUMN_ICON = 0,
	COLUMN_NAME
};

/* 2008-07-29 -	User double-clicked a row in the tree. Close the dialog. */
static void evt_row_activated(GtkWidget *wid, GtkTreePath *path, GtkTreeViewColumn *col, gpointer user)
{
	Dialog	*dlg = user;

	dlg_dialog_sync_close(dlg, 0);
}

const gchar * idl_dialog_sync_new_wait(MainInfo *min, const gchar *path, const gchar *title, const gchar *current, gboolean show_progress)
{
	static gchar	retsel[1024];
	GList		*icons, *here;
	gboolean	terminate = FALSE;
	gpointer	progress = NULL;
	guint		position, length;
	GtkWidget	*scwin;
	GtkListStore	*store;
	GtkWidget	*view;
	GdkPixbuf	*pixbuf;
	GtkTreeIter	iter, *sel = NULL;
	gchar		*selected = NULL;

	if(min == NULL)
		return NULL;

	if(title == NULL)
		title = _("Pick Icon");

	if((icons = ico_get_all(min, path)) == NULL)
		return NULL;

	if(show_progress)
	{
		progress = gui_progress_begin(_("Loading Icon Graphics..."), _("Cancel"));
		length = g_list_length(icons);
	}

	/* Build a list model, to have somewhere to store the icons. */
	store = gtk_list_store_new(2, GDK_TYPE_PIXBUF, G_TYPE_STRING);

	/* Load all icons. */
	for(here = icons, position = 0U; here != NULL; here = g_list_next(here), position++)
	{
		if(progress != NULL)
		{
			if((terminate = gui_progress_update(progress, (gfloat) position / length, (gchar *) here->data)))
				break;
		}
		if(ico_no_icon(here->data))
			continue;
		pixbuf = ico_icon_get_pixbuf(min, here->data);
		/* FIXME: Because of the way the icon-loader does caching, and how non-pixbuf loading
		 * of an icon might already have been done, thus populating the cache with a non-pixbuf
		 * entry, this doesn't filter for a NULL pixbuf. It's better to include all icons in
		 * the list, for now. This will hopefully solve itself once panes go GtkTreeView.
		*/
		gtk_list_store_insert_with_values(store, &iter, -1, 0, pixbuf, 1, here->data, -1);
		if((current != NULL) && (strcmp(current, here->data) == 0) && sel == NULL)
			sel = gtk_tree_iter_copy(&iter);
	}
	if(progress != NULL)
		gui_progress_end(progress);

	if(!terminate)
	{
		GtkCellRenderer		*cr;
		GtkTreeViewColumn	*vc;
		GtkTreeSelection	*ts;
		GtkTreeIter		seliter;
		Dialog			*dlg;

		view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
		cr = gtk_cell_renderer_pixbuf_new();
		vc = gtk_tree_view_column_new_with_attributes("(title)", cr, "pixbuf", COLUMN_ICON, NULL);
		gtk_tree_view_append_column(GTK_TREE_VIEW(view), vc);
		cr = gtk_cell_renderer_text_new();
		vc = gtk_tree_view_column_new_with_attributes("(title)", cr, "text", COLUMN_NAME, NULL);
		gtk_tree_view_append_column(GTK_TREE_VIEW(view), vc);
		gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
		ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));

		if(sel != NULL && ts != NULL)
		{
			gtk_tree_selection_select_iter(ts, sel);
			gtk_tree_iter_free(sel);
			sel = NULL;
		}

		scwin = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		gtk_container_add(GTK_CONTAINER(scwin), view);

		dlg = dlg_dialog_sync_new(scwin, title, NULL);
		gtk_window_set_default_size(GTK_WINDOW(dlg_dialog_get_dialog(dlg)), 320, 544);
		g_signal_connect(G_OBJECT(view), "row_activated", G_CALLBACK(evt_row_activated), dlg);

		if(dlg_dialog_sync_wait(dlg) != DLG_POSITIVE)
			terminate = TRUE;
		if(gtk_tree_selection_get_selected(ts, NULL, &seliter))
			gtk_tree_model_get(GTK_TREE_MODEL(store), &seliter, COLUMN_NAME, &selected, -1);
		dlg_dialog_sync_destroy(dlg);
		g_snprintf(retsel, sizeof retsel, "%s", selected);
		g_free(selected);
	}
	ico_free_all(icons);

	return terminate ? NULL : retsel;
}
