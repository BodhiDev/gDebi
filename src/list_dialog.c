/*
** 2010-06-13 -	Implementation of the list/string editor dialog.
*/

#include "gentoo.h"

#include "dialog.h"

#include "list_dialog.h"

typedef struct {
	Dialog		*dlg;
	GtkListStore	*store;
	GtkWidget	*view;
	GtkWidget	*add;
	GtkWidget	*delete;
	const gchar	*new_text;	/* Default for new adds. */
	gchar		separator;
	LDlgItemEditor	editor;
	gpointer	user;
	gchar		edit_path[64];
} LDlgInfo;

/* 2010-06-13 -	Splits the given string by the separator char, and appends each string into the model. */
static void string_to_model(GtkListStore *store, const gchar *string, gunichar separator)
{
	GString	*sep;
	gchar	**vec;

	sep = g_string_sized_new(8);
	sep = g_string_append_unichar(sep, separator);
	vec = g_strsplit(string, sep->str, -1);
	if(vec != NULL)
	{
		guint	i;

		for(i = 0; vec[i] != NULL; ++i)
		{
			GtkTreeIter	iter;

			gtk_list_store_insert_with_values(store, &iter, -1, 0, vec[i], -1);
		}
		g_strfreev(vec);
	}
	g_string_free(sep, TRUE);
}

/* 2010-06-14 -	Convert the contents of the tree model into a single string, using the separator. */
static gboolean model_to_string(gchar *string, gsize max, const GtkListStore *store, gunichar separator)
{
	GtkTreeIter	iter;
	gboolean	ret = TRUE;

	if(max == 0)
		return FALSE;

	if(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter))
	{
		gchar	*prev = NULL, *put = string;

		max--;	/* T-800 has made a reservation. */
		do
		{
			gchar	*str = NULL;

			gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 0, &str, -1);
			if(str != NULL)
			{
				gsize	len = strlen(str);

				/* Does it fit? */
				if(len + 1 <= max)	/* FIXME: Assumes separator is 1-byte. */
				{
					if(prev != NULL)
						*put++ = separator & 0xff;	/* FIXME: Assumes separator is 1-byte. */
					memcpy(put, str, len);
					put += len;
					*put = '\0';
					max -= len;
				}
				else
					ret = FALSE;
				prev = str;
				g_free(str);
			}
		} while(ret && gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter));
		*put = '\0';
	}
	return ret;
}

/* 2010-06-26 -	Inserting text into cell renderer entry; filter out separator. */
static void evt_cell_renderer_entry_insert_text(GtkEditable *editable, gchar *new_text, gint new_text_length, gint *position, gpointer user)
{
	LDlgInfo	*ldi = user;
	gchar		*get = new_text, *put = get;

	/* Remove any occurance of the separator character, in-place. See any Giraffes to ride? :| */
	while(*get)
	{
		if(*get == ldi->separator)
			get++;
		else
			*put++ = *get++;
	}
	*put = '\0';
}

static void evt_cell_renderer_edited(GtkCellRendererText *cr, const gchar *path, const gchar *new_text, gpointer user)
{
	GtkTreeIter	iter;

	if(gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(user), &iter, path))
	{
		gtk_list_store_set(GTK_LIST_STORE(user), &iter, 0, new_text, -1);
	}
}

static void evt_cell_renderer_entry_icon_press(GtkEntry *entry, GtkEntryIconPosition icon_pos, GdkEvent *event, gpointer user)
{
	LDlgInfo	*ldi = user;
	GString		*value;

	/* Use a GString to allow the user's editor callback to modify the value. */
	value = g_string_new(gtk_entry_get_text(entry));
	if(ldi->editor(value, ldi->user))
	{
		/* Re-setting the entry's text doesn't help, so re-use the model-editing code. */
		evt_cell_renderer_edited(NULL, ldi->edit_path, value->str, ldi->store);
	}
	g_string_free(value, TRUE);
}

/* 2010-06-26 -	Editing starting, set up filtering to avoid inputing the separator. */
static void evt_cell_renderer_editing_started(GtkCellRenderer *renderer, GtkCellEditable *editable, gchar *path, gpointer user)
{
	LDlgInfo	*ldi = user;

	if(!GTK_IS_ENTRY(editable)) 
		return;
	if(ldi->editor != NULL)
	{
		gtk_entry_set_icon_from_icon_name(GTK_ENTRY(editable), GTK_ENTRY_ICON_SECONDARY, "edit-find");
		g_signal_connect(G_OBJECT(editable), "icon_press", G_CALLBACK(evt_cell_renderer_entry_icon_press), user);
		g_snprintf(ldi->edit_path, sizeof ldi->edit_path, "%s", path);
	}
	g_signal_connect(G_OBJECT(editable), "insert_text", G_CALLBACK(evt_cell_renderer_entry_insert_text), user);
}

static void evt_view_row_activated(GtkTreeView *tv, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user)
{
	gtk_tree_view_set_cursor(tv, path, column, TRUE);
}

static void evt_view_selection_changed(GtkTreeSelection *sel, gpointer user)
{
	LDlgInfo	*ldi = user;
	const gboolean	has_sel = gtk_tree_selection_count_selected_rows(sel) > 0;

	gtk_widget_set_sensitive(ldi->delete, has_sel);
}

/* 2010-06-26 -	Add a new item, just below the currently selected one. Make it editable. */
static void evt_add_clicked(GtkWidget *wid, gpointer user)
{
	LDlgInfo	*ldi = user;
	GtkTreeIter	iter, new;
	GtkTreeModel	*model;
	GtkTreePath	*path;

	if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(ldi->view)), &model, &iter))
		gtk_list_store_insert_after(GTK_LIST_STORE(model), &new, &iter);
	else
		gtk_list_store_append(GTK_LIST_STORE(model), &new);
	gtk_list_store_set(GTK_LIST_STORE(model), &new, 0, ldi->new_text, -1);
	if((path = gtk_tree_model_get_path(model, &new)) != NULL)
	{
		GtkTreeViewColumn	*col = gtk_tree_view_get_column(GTK_TREE_VIEW(ldi->view), 0);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(ldi->view), path, col, TRUE);
		gtk_tree_path_free(path);
	}
}

/* 2010-06-26 -	Delete the currently selected item. */
static void evt_delete_clicked(GtkWidget *wid, gpointer user)
{
	LDlgInfo	*ldi = user;
	GtkTreeIter	iter;
	GtkTreeModel	*model;

	if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(ldi->view)), &model, &iter))
		return;
	gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
}

void ldl_dialog_sync_new_wait(gchar *list, gsize max, gunichar separator, const gchar *title)
{
	ldl_dialog_sync_new_full_wait(list, max, separator, title, NULL, NULL);
}

void ldl_dialog_sync_new_full_wait(gchar *list, gsize max, gunichar separator, const gchar *title, LDlgItemEditor editor, gpointer user)
{
	LDlgInfo		ldi;
	GtkWidget		*vbox, *scwin, *hbox;
	GtkCellRenderer		*cr;
	GtkTreeViewColumn	*vc;
	GtkTreeSelection	*sel;

	ldi.store = gtk_list_store_new(1, G_TYPE_STRING);
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	ldi.view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ldi.store));
	cr = gtk_cell_renderer_text_new();
	g_object_set(G_OBJECT(cr), "editable", TRUE, NULL);
	g_signal_connect(G_OBJECT(cr), "editing-started", G_CALLBACK(evt_cell_renderer_editing_started), &ldi);
	g_signal_connect(G_OBJECT(cr), "edited", G_CALLBACK(evt_cell_renderer_edited), ldi.store);
	vc = gtk_tree_view_column_new_with_attributes("(string)", cr, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(ldi.view), vc);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(ldi.view), FALSE);
	gtk_tree_view_set_reorderable(GTK_TREE_VIEW(ldi.view), TRUE);
	g_signal_connect(G_OBJECT(ldi.view), "row_activated", G_CALLBACK(evt_view_row_activated), NULL);
	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scwin), ldi.view);
	gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	ldi.add = gtk_button_new_with_label(_("Add"));
	g_signal_connect(G_OBJECT(ldi.add), "clicked", G_CALLBACK(evt_add_clicked), &ldi);
	gtk_box_pack_start(GTK_BOX(hbox), ldi.add, TRUE, TRUE, 0);
	ldi.delete = gtk_button_new_with_label(_("Delete"));
	g_signal_connect(G_OBJECT(ldi.delete), "clicked", G_CALLBACK(evt_delete_clicked), &ldi);
	gtk_box_pack_start(GTK_BOX(hbox), ldi.delete, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	string_to_model(ldi.store, list, separator);

	sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(ldi.view));
	g_signal_connect(G_OBJECT(sel), "changed", G_CALLBACK(evt_view_selection_changed), &ldi);
	gtk_tree_selection_set_mode(GTK_TREE_SELECTION(sel), GTK_SELECTION_BROWSE);

	ldi.new_text = _("(New Item)");
	ldi.separator = separator;

	ldi.editor = editor;
	ldi.user = user;

	ldi.dlg = dlg_dialog_sync_new(vbox, title, _("OK|Cancel"));
	gtk_window_set_default_size(GTK_WINDOW(dlg_dialog_get_dialog(ldi.dlg)), 320, 544);
	
	dlg_dialog_sync_wait(ldi.dlg);
	model_to_string(list, max, ldi.store, separator);
	g_object_unref(ldi.store);
}
