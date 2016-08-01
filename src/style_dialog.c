/*
** 1998-09-12 -	This module contains code to allow the user to select a style from a
**		simple list representation. This is used in two places; the "reparent"
**		button in cfg_styles, and the "style" button in cfg_types. Since the
**		old (short, inline) approach was severely broken, this needs a module
**		of its own.
** 1999-05-24 -	More or less completely rewritten. About 1/4 the size of the old one.
** 1999-06-19 -	Adapted for the new dialog module.
*/

#include "gentoo.h"

#include <stdlib.h>

#include "styles.h"
#include "dialog.h"
#include "style_dialog.h"

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-24 -	Main style dialog entry point. */
Style * sdl_dialog_sync_new_wait(StyleInfo *si, Style *ignore)
{
	GtkWidget	*scwin, *tv;
	Style		*sel = NULL;
	Dialog		*dlg;
	GtkCellRenderer	*cr;
	GtkTreeViewColumn *vc;

	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

	tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(stl_styleinfo_build_partial(si, ignore)));
	cr = gtk_cell_renderer_text_new();
	vc = gtk_tree_view_column_new_with_attributes("(Styles)", cr, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tv), vc);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tv), FALSE);
	gtk_container_add(GTK_CONTAINER(scwin), tv);
	gtk_widget_set_size_request(scwin, 256, 448);
	dlg = dlg_dialog_sync_new(scwin, _("Select Style"), NULL);
	if(dlg_dialog_sync_wait(dlg) == DLG_POSITIVE)
	{
		GtkTreeIter	iter;

		if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tv)), NULL, &iter))
			gtk_tree_model_get(gtk_tree_view_get_model(GTK_TREE_VIEW(tv)), &iter, 1, &sel, -1);
	}
	dlg_dialog_sync_destroy(dlg);

	return sel;
}
