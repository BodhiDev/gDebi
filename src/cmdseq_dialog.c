/*
** 1998-09-27 -	A pretty cool dialog, that allows the selection of a command sequence
**		OR a built-in command. Should be very convenient.
** 1998-12-16 -	Now tracks and remembers the collapsed/expanded state of the two sub-trees.
** 1999-03-13 -	Adjustments for new dialog module.
** 1999-05-08 -	Slight adjustments; now keeps the command sequence as a GString.
** 1999-05-09 -	Celbrated my birthday by changing my old (sucky) auto-completion code into
**		use of glib's g_completion_XXX() API. A lot nicer, although a bit tricky
**		and (for me) non-intuitive to use. I think it works now, though.
** 1999-06-19 -	Adapted for new dialog module.
** 1999-08-29 -	Did trivial modifications to retain the command entered between uses.
*/

#include "gentoo.h"

#include <gdk/gdkkeysyms.h>

#include "dialog.h"
#include "guiutil.h"
#include "strutil.h"

#include "cmdseq_dialog.h"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	GString		*cmd;		/* Keep at top for correct initialization. */
	Dialog		*dlg;
	GtkWidget	*vbox;
	GtkWidget	*entry;
	GList		*builtin, *cmdseq;
	GList		*clist, *citer;
	GtkTreeStore	*store;
	GtkListStore	*lstore;	/* Only used for completion, to work around sub-tree issues. */
	gboolean	expanded[2];
} CDlg;

static CDlg	the_cdlg = { NULL };

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-27 -	Add <key> to a list, sorted. */
static void insert_key(gpointer key, gpointer data, gpointer user)
{
	GList	**list = user;

	*list = g_list_insert_sorted(*list, key, (GCompareFunc) strcmp);
}

/* 1998-09-27 -	Take a hash table with string keys, and build a linear list from it, sorting
**		the keys in lexicographically.
*/
static GList * hash_to_list(GHashTable *hash)
{
	GList	*list = NULL;

	if(hash != NULL)
		g_hash_table_foreach(hash, insert_key, &list);

	return list;
}

/* ----------------------------------------------------------------------------------------- */

/* 2004-12-03 -	Cursor moved, i.e. item was selected. */
static void evt_cursor_changed(GtkWidget *wid, gpointer user)
{
	CDlg		*cdlg = user;
	GtkTreePath	*path;
	GtkTreeIter	iter;
	gchar		*text = NULL;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(wid), &path, NULL);
	if(path == NULL)
		return;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(cdlg->store), &iter, path);
	if(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(cdlg->store), &iter) != 0)
		return;
	gtk_tree_model_get(GTK_TREE_MODEL(cdlg->store), &iter, 0, &text, -1);
	if(text != NULL)
	{
		gtk_entry_set_text(GTK_ENTRY(cdlg->entry), text);
		gtk_widget_grab_focus(cdlg->entry);
		g_string_assign(cdlg->cmd, text);
		g_free(text);
	}
}

/* 2004-11-21 -	Expanded/collapsed state of a tree row changed; update state. */
static void evt_row_collapsed_expanded(GtkWidget *treeview, GtkTreeIter *iter, GtkTreePath *path, gpointer user)
{
	gint	*ind = gtk_tree_path_get_indices(path);
	CDlg	*cdlg = user;

	cdlg->expanded[ind[0]] = gtk_tree_view_row_expanded(GTK_TREE_VIEW(treeview), path);
}

/* 2009-10-22 -	Accept double-clicked row, for speed. */
static void evt_row_activated(GtkWidget *wid, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user)
{
	CDlg	*cdlg = user;

	dlg_dialog_sync_close(cdlg->dlg, DLG_POSITIVE);
}

/* 2004-11-21 -	Append a subtree from a list of strings. */
static void append_subtree_list(const GList *list, GtkTreeStore *store, GtkTreeIter *parent)
{
	for(; list != NULL; list = g_list_next(list))
	{
		GtkTreeIter	iter;

		gtk_tree_store_append(store, &iter, parent);
		gtk_tree_store_set(store, &iter, 0, list->data, -1);
	}
}

static void append_liststore_list(const GList *list, GtkListStore *store)
{
	for(; list != NULL; list = g_list_next(list))
	{
		GtkTreeIter	iter;

		gtk_list_store_insert_with_values(store, &iter, -1, 0, list->data, -1);
	}
}

/* 2012-05-23 -	Callback for GTK+'s built-in completion support. This replaces the old
**		TAB-triggered completion, since that in turn was based on GCompletion
**		(in glib), which has been deprecated.
*/
static gboolean cb_match_func(GtkEntryCompletion *ec, const gchar *key, GtkTreeIter *iter, gpointer user)
{
	CDlg		*cdlg = user;
	const gchar	*text = gtk_entry_get_text(GTK_ENTRY(cdlg->entry));
	gchar		*cname;
	gboolean	ret;

	gtk_tree_model_get(GTK_TREE_MODEL(gtk_entry_completion_get_model(ec)), iter, 0, &cname, -1);
	ret = strstr(cname, text) != NULL;
	g_free(cname);

	return ret;
}

/* 2004-11-21 -	Build tree holding built-in and user commands. New GTK+ 2.0 version. */
static GtkWidget * build_tree(MainInfo *min, CDlg *cdlg)
{
	GtkTreeIter		iter;
	GtkWidget		*view;
	GtkCellRenderer		*cr;
	GtkTreeViewColumn	 *vc;
	GtkTreePath		*path;
	GtkEntryCompletion	*compl;
	gchar			label[32];

	/* Create tree store, populate with contents of built-in and user-defined lists. */
	cdlg->store = gtk_tree_store_new(1, G_TYPE_STRING);
	gtk_tree_store_append(cdlg->store, &iter, NULL);
	g_snprintf(label, sizeof label, _("Built-Ins (%u)"), g_list_length(cdlg->builtin));
	gtk_tree_store_set(cdlg->store, &iter, 0, label, -1);
	append_subtree_list(cdlg->builtin, cdlg->store, &iter);

	gtk_tree_store_append(cdlg->store, &iter, NULL);
	g_snprintf(label, sizeof label, _("User Defined (%u)"), g_list_length(cdlg->cmdseq));
	gtk_tree_store_set(cdlg->store, &iter, 0, label, -1);
	append_subtree_list(cdlg->cmdseq, cdlg->store, &iter);

	cdlg->lstore = gtk_list_store_new(1, G_TYPE_STRING);
	append_liststore_list(cdlg->builtin, cdlg->lstore);
	append_liststore_list(cdlg->cmdseq,  cdlg->lstore);

	/* Create view, renderer, and column, and stuff it all together. */
	view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(cdlg->store));
	cr = gtk_cell_renderer_text_new();
	vc = gtk_tree_view_column_new_with_attributes("(Commands)", cr, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), vc);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

	/* Expand whatever was previously expanded. */
	if(cdlg->expanded[0])
	{
		path = gtk_tree_path_new_from_indices(0, -1);
		gtk_tree_view_expand_row(GTK_TREE_VIEW(view), path, FALSE);
		gtk_tree_path_free(path);
	}
	if(cdlg->expanded[1])
	{
		path = gtk_tree_path_new_from_indices(1, -1);
		gtk_tree_view_expand_row(GTK_TREE_VIEW(view), path, FALSE);
		gtk_tree_path_free(path);
	}

	/* Connect expand/collapse-tracking signals. */
	g_signal_connect(G_OBJECT(view), "row_collapsed", G_CALLBACK(evt_row_collapsed_expanded), cdlg);
	g_signal_connect(G_OBJECT(view), "row_expanded", G_CALLBACK(evt_row_collapsed_expanded), cdlg);

	/* And track command selections, too. */
	g_signal_connect(G_OBJECT(view), "cursor_changed", G_CALLBACK(evt_cursor_changed), cdlg);

	/* Support accept on double-click, for speedier feel. */
	g_signal_connect(G_OBJECT(view), "row_activated", G_CALLBACK(evt_row_activated), cdlg);

	/* Create GtkEntryCompletion and link to tree. */
	compl = gtk_entry_completion_new();
	gtk_entry_completion_set_match_func(compl, cb_match_func, cdlg, NULL);
	gtk_entry_completion_set_text_column(compl, 0);
	gtk_entry_completion_set_model(compl, GTK_TREE_MODEL(cdlg->lstore));
	gtk_entry_completion_set_inline_selection(compl, TRUE);
	gtk_entry_set_completion(GTK_ENTRY(cdlg->entry), compl);

	return view;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-27 -	Open up a dialog where the user can select (or type) a command name.
**		The <cmdseq> argument should contain the current user-defined command sequences.
**		If it is NULL, the possibly-not-so-current ones in min->cfg.commands are used.
** 1999-03-29 -	Simplified semantics; now runs synchronously, and simply returns command name (or NULL).
*/
const gchar * csq_dialog_sync_new_wait(MainInfo *min, GHashTable *cmdseq)
{
	CDlg		*cdlg = &the_cdlg;
	const gchar	*ret = NULL;
	GtkWidget	*label, *scwin, *tree;

	if(cmdseq == NULL)			/* No alternative command sequence hash? */
		cmdseq = min->cfg.commands.cmdseq;

	cdlg->builtin = hash_to_list(min->cfg.commands.builtin);
	cdlg->cmdseq = hash_to_list(cmdseq);
	if(cdlg->cmd == NULL)
		cdlg->cmd = g_string_new(NULL);

	cdlg->clist = cdlg->citer = NULL;

	cdlg->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	label = gtk_label_new(_("Select a command, or type part of its name."));
	gtk_box_pack_start(GTK_BOX(cdlg->vbox), label, FALSE, FALSE, 0);
	scwin  = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	cdlg->entry = gui_dialog_entry_new();
	if((tree = build_tree(min, cdlg)) != NULL)
	{
		gtk_container_add(GTK_CONTAINER(scwin), tree);
		gtk_box_pack_start(GTK_BOX(cdlg->vbox), scwin, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(cdlg->vbox), cdlg->entry, FALSE, FALSE, 0);

		gtk_entry_set_text(GTK_ENTRY(cdlg->entry), cdlg->cmd->str);
		gtk_editable_select_region(GTK_EDITABLE(cdlg->entry), 0, -1);

		cdlg->dlg = dlg_dialog_sync_new(cdlg->vbox, _("Select Command"), NULL);
		gtk_window_set_default_size(GTK_WINDOW(dlg_dialog_get_dialog(cdlg->dlg)), 320, 544);
		gtk_widget_grab_focus(cdlg->entry);
		if(dlg_dialog_sync_wait(cdlg->dlg) == DLG_POSITIVE)
		{
			g_string_assign(cdlg->cmd, gtk_entry_get_text(GTK_ENTRY(cdlg->entry)));
			ret = cdlg->cmd->str;
		}
		dlg_dialog_sync_destroy(cdlg->dlg);
		g_list_free(cdlg->builtin);
		g_list_free(cdlg->cmdseq);
		g_object_unref(cdlg->lstore);
	}
	return ret;
}
