/*
** 1998-07-13 -	A more or less general helper module. Uses the dialog module to provide
**		a clist showing the contents of a hash table. Very useful for selecting
**		a command (native or user); originally written for the cfg_buttons module.
** 1998-08-01 -	Discovered (and fixed) a weird bug; the clist's selection event handler was
**		being called as the list was being populated, which caused a read-out of
**		row data not yet installed. Seg-fault. Can't understand why it just popped
**		up today... Fixed it with an ugly little flag (open).
** 1998-08-04 -	Now handles immediate closing, without user clicking in list first. :)
** 1999-03-13 -	Adapted for new dialog module, and generally touched up a little.
** 1999-06-19 -	Changes due to rewritten dialog support module (again). Also changed the
**		semantics: this is now a synchronous dialog.
** 2008-07-28 -	Rewritten to use GtkTreeView and friends rather than GtkCList, which is
**		deprecated in GTK+ 2.0. Feel the future.
*/

#include "gentoo.h"
#include "dialog.h"
#include "hash_dialog.h"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	Dialog		*dlg;
	GtkListStore	*model;
	GtkWidget	*view;
} HDlgInfo;

/* ----------------------------------------------------------------------------------------- */

/* 2008-07-28 -	This gets called when the user double-clicks a row. Close the dialog. */
static void evt_row_activated(GtkWidget *wid, GtkTreePath *path, GtkTreeViewColumn *col, gpointer user)
{
	HDlgInfo	*hdl = user;

	dlg_dialog_sync_close(hdl->dlg, 0);
}

/* ----------------------------------------------------------------------------------------- */

/* 2008-07-28 -	This is a g_hash_table_foreach() callback, that populates a list store. */
static void populate_model(gpointer key, gpointer value, gpointer user)
{
	GtkListStore	*store = user;
	GtkTreeIter	iter;

	/* FIXME: Here, we assume that the KEY is a string. */
	gtk_list_store_insert_with_values(store, &iter, -1, 0, key, 1, value, 2, key, -1);
}

/* 1999-06-19 -	Main has dialog entry point. */
const gchar * hdl_dialog_sync_new_wait(GHashTable *hash, const gchar *title)
{
	static HDlgInfo		hdl;
	GtkWidget		*scwin;
	GtkCellRenderer		*cr;
	GtkTreeViewColumn	*vc;
	GtkTreeModel		*sm;		

	/* Throw each hashed object (value) into the list store. Our store has
	 * three columns: one is a pointer to each KEY in the given hash, the
	 * next is the key's VALUE, and the third is a string made from the two.
	 * Currently, we assume that the KEY is also a string.
	*/
	hdl.model = gtk_list_store_new(3, G_TYPE_POINTER, G_TYPE_POINTER, G_TYPE_STRING);
	g_hash_table_foreach(hash, populate_model, hdl.model);

	/* Now, wrap the list model in a sortable model, and sort on that string column. */
	sm = gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(hdl.model));
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(sm), 2, GTK_SORT_ASCENDING);

	hdl.view = gtk_tree_view_new_with_model(sm);
	cr = gtk_cell_renderer_text_new();
	vc = gtk_tree_view_column_new_with_attributes("(title)", cr, "text", 2, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(hdl.view), vc);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(hdl.view), FALSE);
	g_signal_connect(G_OBJECT(hdl.view), "row_activated", G_CALLBACK(evt_row_activated), &hdl);

	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scwin), hdl.view);

	hdl.dlg = dlg_dialog_sync_new(scwin, title, NULL);
	gtk_window_set_default_size(GTK_WINDOW(dlg_dialog_get_dialog(hdl.dlg)), 320, 544);
	if(dlg_dialog_sync_wait(hdl.dlg) == DLG_POSITIVE)
	{
		GtkTreeSelection	*sel;
		GtkTreeIter		iter;

		/* Fish out the selection, and the selected row. */
		sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(hdl.view));
		if(gtk_tree_selection_get_selected(sel, NULL, &iter))
		{
			gchar	*str;

			/* We have a string column, and need to return a string. Guess the next step. */
			gtk_tree_model_get(sm, &iter, 2, &str, -1);
			return str;
		}		
	}
	return NULL;
}
