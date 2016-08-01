/*
** 1998-08-14 -	Configure styles, e.g. things such as back- & foreground colors, icons, and
**		actions. This might become involved.
** 1998-08-23 -	Did lots of work. Now you can actually add and delete styles. Got involved.
** 1998-08-24 -	Completed a first version, fixed plenty of bugs too...
** 1998-08-26 -	Added fun "Copy" button group for visual properties. Also added a deselect
**		signal handler for root style. Oops.
** 1998-08-30 -	Now uses the ico_get_all() function to retrieve a list of all icon file names.
**		Added three (!) forgotten modified-sets. Damn.
** 1998-09-02 -	Added action property configuration, and cleaned up their save/load handling.
** 1998-09-16 -	Made some optimizations, mainly in set_preview() (one more signal blocker).
** 1998-09-27 -	Now uses the brand new cmdseq_dialog for choosing actions. Works fine.
** 1999-03-05 -	Due to new selection handling, we no longer have control over how selected items
**		look. That got rid of some code.
** 1999-03-13 -	Rewrote the dialog module (from scratch), which lead to some changes here too.
** 1999-05-07 -	Now uses the (new) color dialog module. Got rid of lots of crufty code.
** 2000-07-02 -	Took a step closer to the rest of the world, by marking strings for translation.
*/

#include <ctype.h>

#include "gentoo.h"

#include "styles.h"
#include "types.h"
#include "dirpane.h"
#include "dialog.h"
#include "dpformat.h"
#include "iconutil.h"
#include "strutil.h"
#include "fileutil.h"
#include "miscutil.h"
#include "xmlutil.h"
#include "guiutil.h"
#include "style_dialog.h"
#include "cmdseq_dialog.h"
#include "color_dialog.h"
#include "icon_dialog.h"

#include "configure.h"
#include "cfg_module.h"
#include "cfg_paths.h"		/* For cpt_get_path(). */
#include "cfg_types.h"		/* For ctp_get_types(). */
#include "cfg_cmdseq.h"		/* For ccs_get_current(). */

#include "cfg_styles.h"

#define	NODE	"FileStyles"

/* ----------------------------------------------------------------------------------------- */

/* For GtkTreeModel access. */
enum {
	COLUMN_ICON,
	COLUMN_NAME
};

typedef struct {			/* Visual properties notebook page. */
	GtkWidget		*vbox;
	GtkWidget		*preview;	/* CList showing preview of style (old-school!). */
	GtkTreeViewColumn	*pre_icon;	/* Icon preview. */
	GtkCellRenderer		*pre_icon_r;
	GtkTreeViewColumn	*pre_name;	/* Name (text) preview. */
	GtkCellRenderer		*pre_name_r;
	GtkWidget		*override[3];	/* Override check buttons for back- & foreground colors, plus icon. */
	GtkWidget		*edit[3];	/* The "edit" (or "pick") command buttons. */
} PVisual;

typedef struct {			/* Action properties notebook page. */
	GtkWidget	*vbox;
	GtkListStore	*store;
	GtkWidget	*view;
	GtkWidget	*editcmd;	/* Shortcut to go to the Command editing configuration page. */
	GtkWidget	*adel;		/* Delete (or override) command button. */
} PAction;

enum {
	ACTION_COLUMN_NAME,
	ACTION_COLUMN_CMDSEQ,
	ACTION_COLUMN_ACTION,
	ACTION_COLUMN_WEIGHT,

	ACTION_COLUMN_COUNT
};

typedef struct {
	GtkWidget	*vbox;
	GuiHandlerGroup	*handlers;	/* Collects editing widgets, for signal blocking. */
	GtkWidget	*scwin;
	GtkWidget	*tree;		/* Main style tree widget. */
	gulong		sig_expand;	/* Signal handler for tree item expansion. */
	gulong		sig_collapse;	/* Signal handler for tree item collapse. */
	GtkTreeStore	*store;

	GtkWidget	*dvbox;		/* Vbox holding definition widgets. */
	GtkWidget	*dname;		/* Name of selected style. */
	GtkWidget	*dparent;	/* Parent of selected style. */
	GtkWidget	*dreparent;	/* Button for reparenting dialog. */

	GtkWidget	*dpnbook;	/* Property notebook. */
	PVisual		dpvisual;
	PAction		dpaction;

	GtkWidget	*del;		/* The style "Delete" button. */

	MainInfo	*min;
	StyleInfo	*si;
	const gchar	*curr_prop;	/* Current property, when editing one. */
	gint		curr_arow;	/* Action property clist row. */
	gboolean	modified;
} P_Styles;

static P_Styles	the_page;

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-24 -	Set the preview widget(s). */
static void set_widgets_preview(P_Styles *page, Style *stl)
{
	GtkListStore	*store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(page->dpvisual.preview)));
	GtkTreeIter	iter;

	if(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter))
	{
		const gchar	*iname;

		if((iname = stl_style_property_get_icon(stl, SPN_ICON_UNSEL)) != NULL)
		{
			GdkPixbuf	*pbuf;

			if((pbuf = ico_icon_get_pixbuf(page->min, iname)) != NULL)
				gtk_list_store_set(store, &iter, COLUMN_ICON, pbuf, -1);
		}
	}
	dpf_cell_set_style_colors(page->dpvisual.pre_icon_r, stl, FALSE, FALSE);
	dpf_cell_set_style_colors(page->dpvisual.pre_name_r, stl, TRUE,  TRUE);
}

/* 1999-05-24 -	Set action list. */
static void set_widgets_action(P_Styles *page, Style *stl)
{
	GList	*alist;

	gtk_list_store_clear(page->dpaction.store);
	if((alist = stl_style_property_get_actions(stl)) != NULL)
	{
		const GList	*iter;
		GtkTreeIter	titer;

		for(iter = alist; iter != NULL; iter = g_list_next(iter))
		{
			const gboolean	ovr = stl_style_property_get_override(stl, iter->data);
			gtk_list_store_insert_with_values(page->dpaction.store, &titer, -1,
						ACTION_COLUMN_NAME, iter->data,
						ACTION_COLUMN_CMDSEQ, stl_style_property_get_action(stl, iter->data),
						ACTION_COLUMN_ACTION, iter->data,
						ACTION_COLUMN_WEIGHT, ovr ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
						-1);
		}
		g_list_free(alist);
	}
}

/* 1999-05-24 -	Set various editing widgets to display <stl>'s details. */
static void set_widgets(P_Styles *page, Style *stl)
{
	const gchar	*pname, *vpname[] = { SPN_COL_UNSEL_BG, SPN_COL_UNSEL_FG, SPN_ICON_UNSEL };
	gboolean	or;
	guint		i;

	if((page == NULL) || (stl == NULL))
		return;

	gui_handler_group_block(page->handlers);

	gtk_entry_set_text(GTK_ENTRY(page->dname), stl_style_get_name(stl));
	if((pname = stl_style_get_name(stl_styleinfo_style_get_parent(page->si, stl))) != NULL)
		gtk_entry_set_text(GTK_ENTRY(page->dparent), pname);
	else
		gtk_entry_set_text(GTK_ENTRY(page->dparent), _("(None)"));
	gtk_widget_set_sensitive(page->dreparent, pname != NULL);

	set_widgets_preview(page, stl);
	for(i = 0; i < 3; i++)
	{
		or = stl_style_property_get_override(stl, vpname[i]);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->dpvisual.override[i]), or);
		gtk_widget_set_sensitive(page->dpvisual.override[i], (i < 1) || (pname != NULL));
		gtk_widget_set_sensitive(page->dpvisual.edit[i], or);
	}
	set_widgets_action(page, stl);
	gtk_widget_set_sensitive(page->dvbox, TRUE);
	gtk_widget_set_sensitive(page->del, stl_styleinfo_style_root(page->si) != stl);

	gui_handler_group_unblock(page->handlers);
}

/* 1999-05-25 -	Reset action editing widgets. */
static void reset_widgets_action(P_Styles *page)
{
	gtk_widget_set_sensitive(page->dpaction.editcmd, FALSE);
	gtk_label_set_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(page->dpaction.adel))), _("Delete Action"));
	gtk_widget_set_sensitive(page->dpaction.adel, FALSE);
}

/* 1999-05-24 -	Reset widgets. Handy when there is no longer a selection. */
static void reset_widgets(P_Styles *page)
{
	page->curr_prop  = NULL;
	page->curr_arow  = -1;
	gtk_entry_set_text(GTK_ENTRY(page->dname), "");
	gtk_entry_set_text(GTK_ENTRY(page->dparent), "");
	reset_widgets_action(page);
	gtk_widget_set_sensitive(page->dvbox, FALSE);
	gtk_widget_set_sensitive(page->del, FALSE);
}

/* ----------------------------------------------------------------------------------------- */

static Style * style_get_selected(const P_Styles *page, GtkTreeIter *iter)
{
	GtkTreeIter	myiter;

	if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->tree)), NULL, &myiter))
	{
		if(iter != NULL)
			*iter = myiter;
		return stl_styleinfo_get_style_iter(page->si, page->store, &myiter);
	}
	return NULL;
}

/* 2009-03-24 -	New-style selection tracking. */
static void evt_style_selection_changed(GtkTreeSelection *sel, gpointer user)
{
	P_Styles	*page = user;
	Style		*style;

	if((style = style_get_selected(page, NULL)) != NULL)
	{
		set_widgets(page, style);
		reset_widgets_action(page);
	}
	else
		reset_widgets(page);
}

/* 2009-02-06 -	A row was expanded, update the underlying Style object. */
static void evt_style_row_expanded(GtkTreeView *view, GtkTreeIter *iter, GtkTreePath *path, gpointer user_data)
{
	GtkTreeModel	*m;
	Style		*stl;

	m = gtk_tree_view_get_model(view);
	gtk_tree_model_get(m, iter, 1, &stl, -1);
	stl_style_set_expand(stl, TRUE);
}

/* 2009-02-06 -	A row was collapsed, update the underlying Style object. */
static void evt_style_row_collapsed(GtkTreeView *view, GtkTreeIter *iter, GtkTreePath *path, gpointer user_data)
{
	GtkTreeModel	*m;
	Style		*stl;

	m = gtk_tree_view_get_model(view);
	gtk_tree_model_get(m, iter, 1, &stl, -1);
	stl_style_set_expand(stl, FALSE);
}

/* 2009-02-06 -	This is a gtk_tree_model_foreach() callback, that simply applies the collapsed/
**		expanded status to a tree row showing a style. This is a property of the view,
**		so it cannot be done from inside the styles module.
*/
static gboolean set_expand(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer user)
{
	Style	*stl;

	gtk_tree_model_get(model, iter, 1, &stl, -1);
	if(stl_style_get_expand(stl))
		gtk_tree_view_expand_row(GTK_TREE_VIEW(user), path, FALSE);
	else
		gtk_tree_view_collapse_row(GTK_TREE_VIEW(user), path);
	return FALSE;
}

/* 1999-05-24 -	Repopulate the tree. */
static void populate_tree(P_Styles *page)
{
	if(page->si != NULL)
	{
		page->store = stl_styleinfo_build_partial(page->si, NULL);
		/* Block expand/collapse signals. */
		g_signal_handler_block(page->tree, page->sig_collapse);
		g_signal_handler_block(page->tree, page->sig_expand);
		gtk_tree_view_set_model(GTK_TREE_VIEW(page->tree), GTK_TREE_MODEL(page->store));
		gtk_tree_model_foreach(GTK_TREE_MODEL(page->store), set_expand, page->tree);
		g_signal_handler_unblock(page->tree, page->sig_expand);
		g_signal_handler_unblock(page->tree, page->sig_collapse);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-24 -	Set a new name for the current style. */
static void evt_style_name_changed(GtkWidget *wid, gpointer user)
{
	P_Styles	*page = user;
	const gchar	*name;
	Style		*style;
	GtkTreeIter	iter;

	if((style = style_get_selected(page, &iter)) != NULL)
	{
		if((name = gtk_entry_get_text(GTK_ENTRY(wid))) != NULL)
		{
			ctp_replace_style(stl_style_get_name(style), style);
			stl_styleinfo_set_name_iter(page->si, page->store, &iter, name);
			page->modified = TRUE;
		}
	}
}

/* 1999-05-24 -	User clicked the details (magnifying glass) button to set new parent for current style. */
static void evt_style_parent_clicked(GtkWidget *wid, gpointer user)
{
	P_Styles	*page = user;
	Style		*style;
	Style		*np;

	if((style = style_get_selected(page, NULL)) != NULL)
	{
		if((np = sdl_dialog_sync_new_wait(page->si, style)) != NULL)
		{
			stl_styleinfo_style_set_parent(page->si, style, np);
			populate_tree(page);
			page->modified = TRUE;
		}
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-24 -	User toggled one of the visual property override checkbuttons. Update
**		the current style accordingly.
*/
static void evt_vprop_override_toggled(GtkWidget *wid, gpointer user)
{
	P_Styles	*page = user;
	Style		*style;
	const gchar	*pname;
	gboolean	or;

	if((style = style_get_selected(page, NULL)) == NULL)
		return;

	pname = g_object_get_data(G_OBJECT(wid), "user");
	or = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));
	if(or)
	{
		if(strcmp(pname, SPN_ICON_UNSEL) == 0)
			stl_style_property_set_icon(style, pname, stl_style_property_get_icon(style, pname));
		else	/* If not an icon, it's a color. */
		{
			const GdkColor	*def;

			/* FIXME: This assumes only background can be non-overridden in root. */
			if((def = stl_style_property_get_color(style, pname)) != NULL)
				stl_style_property_set_color(style, pname, def);
		}
	}
	else
		stl_style_property_remove(style, pname);
	set_widgets(page, style);
	page->modified = TRUE;
}

/* 1999-05-24 -	A color property has changed. Update previews and stuff. */
static void evt_vprop_color_changed(const GdkRGBA *color, gpointer user)
{
	P_Styles	*page = user;
	Style		*style;
	GdkColor	color_old;

	if((style = style_get_selected(page, NULL)) == NULL)
		return;

	gui_color_from_rgba(&color_old, color);
	stl_style_property_set_color(style, page->curr_prop, &color_old);
	set_widgets(page, style);
}

/* 1999-05-24 -	User hit the "Edit..." (or, for the icon, "Pick...") button below override toggle. */
static void evt_vprop_edit_clicked(GtkWidget *wid, gpointer user)
{
	P_Styles	*page = user;
	Style		*style;

	if((style = style_get_selected(page, NULL)) == NULL)
		return;

	page->curr_prop = g_object_get_data(G_OBJECT(wid), "user");

	if(strcmp(page->curr_prop, SPN_ICON_UNSEL) == 0)
	{
		const gchar	*icon;

		if((icon = idl_dialog_sync_new_wait(page->min, cpt_get_path(PTID_ICON), NULL,
						  stl_style_property_get_icon(style, page->curr_prop),
							TRUE)) != NULL)
		{
			stl_style_property_set_icon(style, page->curr_prop, icon);
			page->modified = TRUE;
			set_widgets(page, style);
		}
	}
	else
	{
		const GdkColor	*col;
		GdkColor	col_old;
		GdkRGBA		initial, *ip = NULL;

		if((col = stl_style_property_get_color(style, page->curr_prop)) != NULL)
		{
			col_old = *col;
			gui_rgba_from_color(&initial, col);
			ip = &initial;
		}
		if(cdl_dialog_sync_new_wait(_("Edit Color"), evt_vprop_color_changed, ip, page) != DLG_POSITIVE)
		{
			if(col != NULL)
				stl_style_property_set_color(style, page->curr_prop, &col_old);
		}
		else
			page->modified = TRUE;
		set_widgets(page, style);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 2014-12-25 -	Set sensitivity of the shortcut button for command-editing. User-defined commands only. */
static void set_aprop_editcmd_button(const P_Styles *page, Style *style)
{
	const gchar	*action = stl_style_property_get_action(style, page->curr_prop);

	if(action != NULL && *action != '\0' && g_hash_table_contains(ccs_get_current(), action))
	{
		gtk_widget_set_sensitive(page->dpaction.editcmd, islower((unsigned char) action[0]));
	}
}

/* 2009-04-22 -	Set the proper label for the Delete Action button. Depends on override. */
static gboolean set_aprop_delete_button(P_Styles *page, Style *style)
{
	gboolean	unique, ovr;

	unique = stl_style_property_is_unique(style, page->curr_prop);
	gtk_label_set_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(page->dpaction.adel))),
				unique ? _("Delete Action") : _("Revert to Inherited Command"));
	ovr = stl_style_property_get_override(style, page->curr_prop);
	gtk_widget_set_sensitive(page->dpaction.adel, ovr);

	return ovr;
}

/* 2009-04-22 -	Update widgets when action property selection changes. */
static void evt_aprop_selection_changed(GtkTreeSelection *sel, gpointer user)
{
	P_Styles	*page = user;
	Style		*style;
	GtkTreeIter	myiter;

	if((style = style_get_selected(page, NULL)) == NULL)
	{
		gtk_widget_set_sensitive(page->dpaction.editcmd, FALSE);
		return;
	}
	if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->dpaction.view)), NULL, &myiter))
	{
		gboolean		ovr;
		GtkTreeViewColumn	*col;
		GList			*cellrenderers;

		gtk_tree_model_get(GTK_TREE_MODEL(page->dpaction.store), &myiter, ACTION_COLUMN_ACTION, &page->curr_prop, -1);
		set_aprop_editcmd_button(page, style);
		ovr = set_aprop_delete_button(page, style);
		/* Set the name column's editable mode, depending on override. */
		col = gtk_tree_view_get_column(GTK_TREE_VIEW(page->dpaction.view), 0);
		cellrenderers = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(col));
		if(cellrenderers != NULL)
		{
			g_object_set(G_OBJECT(cellrenderers->data), "editable", ovr, NULL);
			g_list_free(cellrenderers);
		}
		/* Make name column bold/normal depending on override. */
		gtk_list_store_set(page->dpaction.store, &myiter, ACTION_COLUMN_WEIGHT, ovr ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL, -1);
	}
}

/* 2009-04-22 -	An action property name was edited. If the name changed, to the rename by calling the Styles module. */
static void evt_aprop_name_edited(GtkCellRendererText *cr, gchar *spath, gchar *new_name, gpointer user)
{
	P_Styles	*page = user;
	Style		*stl;
	GtkTreePath	*path;
	gchar		*name;

	if((stl = style_get_selected(page, NULL)) == NULL)
		return;

	if((path = gtk_tree_path_new_from_string(spath)) != NULL)
	{
		GtkTreeIter	iter;

		if(gtk_tree_model_get_iter(GTK_TREE_MODEL(page->dpaction.store), &iter, path))
		{
			gtk_tree_model_get(GTK_TREE_MODEL(page->dpaction.store), &iter,
						ACTION_COLUMN_NAME, &name,
						-1);
			/* Only do the change if the new_name really is new. */
			if(strcmp(name, new_name) != 0)
			{
				if(stl_style_property_rename(stl, name, new_name))
					set_widgets_action(page, stl);
			}
			g_free(name);
		}
		gtk_tree_path_free(path);
	}
}

/* 2009-04-22 -	User wants to select a command from a dialog, for the current action property's action. */
static void evt_aprop_cmdseq_pick_activated(GtkWidget *wid, gpointer user)
{
	P_Styles	*page = user;
	const gchar	*cmd;

	if((cmd = csq_dialog_sync_new_wait(page->min, ccs_get_current())) != NULL)
	{
		Style	*style;
		GtkTreeIter	myiter;

		if((style = style_get_selected(page, NULL)) == NULL)
			return;
		if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->dpaction.view)), NULL, &myiter))
		{
			gtk_tree_model_get(GTK_TREE_MODEL(page->dpaction.store), &myiter, ACTION_COLUMN_ACTION, &page->curr_prop, -1);
			stl_style_property_set_action(style, page->curr_prop, cmd);
			gtk_list_store_set(page->dpaction.store, &myiter, ACTION_COLUMN_CMDSEQ, cmd, -1);
			page->modified = TRUE;
		}
	}
}

/* 2009-04-22 -	Populate the action name editing entry's popup menu. This gives us a chance to add a menuitem that
**		brings up the good old "Select Command" dialog. This used to be done by a magnifying-glass button
**		to the right of the entry, but now the entry is *in* the GtkTreeView.
*/
static void evt_aprop_cmdseq_populate_popup(GtkEntry *entry, GtkMenu *menu, gpointer user)
{
	GtkWidget	*wid;

	wid = gtk_menu_item_new_with_label(_("Select Command ..."));	/* FIXME: Add back (deprecated) icon?! */
	g_signal_connect(G_OBJECT(wid), "activate", G_CALLBACK(evt_aprop_cmdseq_pick_activated), user);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), wid);
	gtk_widget_show_all(wid);
}

/* 2009-04-22 -	This gets called when an action property name editing is started. This is the time to attach a
**		popup-population handler, which gets called if the user right-clicks the entry.
*/
static void evt_aprop_cmdseq_edit_started(GtkCellRenderer *renderer, GtkCellEditable *editable, gchar *spath, gpointer user)
{
	/* Magically know that the editable is a GtkEntry, and attach a menu signal handler. */
	g_signal_connect(G_OBJECT(editable), "populate_popup", G_CALLBACK(evt_aprop_cmdseq_populate_popup), user);
}

static void evt_aprop_cmdseq_edited(GtkCellRendererText *cr, gchar *spath, gchar *new_cmdseq, gpointer user)
{
	P_Styles	*page = user;
	Style		*stl;
	GtkTreePath	*path;
	const gchar	*cmdseq;

	if(page->curr_prop == NULL)
		return;
	if((stl = style_get_selected(page, NULL)) == NULL)
		return;
	if((path = gtk_tree_path_new_from_string(spath)) == NULL)
		return;
	/* Only do the change if the new_cmdseq really is new. */
	cmdseq = stl_style_property_get_action(stl, page->curr_prop);
	if(strcmp(cmdseq, new_cmdseq) != 0)
	{
		GtkTreeIter	iter;

		if(gtk_tree_model_get_iter(GTK_TREE_MODEL(page->dpaction.store), &iter, path))
		{
			stl_style_property_set_action(stl, page->curr_prop, new_cmdseq);
			gtk_list_store_set(page->dpaction.store, &iter, ACTION_COLUMN_CMDSEQ, new_cmdseq, -1);
			gtk_list_store_set(page->dpaction.store, &iter, ACTION_COLUMN_WEIGHT, PANGO_WEIGHT_BOLD, -1);
			set_aprop_delete_button(page, stl);
			page->modified = TRUE;
		}
	}
	gtk_tree_path_free(path);
}

/* 1999-05-25 -	Add a new action property. Pops up a dialog asking for the name. */
static void evt_aprop_add_clicked(GtkWidget *wid, gpointer user)
{
	P_Styles	*page = user;
	Style		*style;
	Dialog		*dlg;
	GtkWidget	*hbox, *label, *entry;

	if((style = style_get_selected(page, NULL)) == NULL)
		return;

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new(_("Name"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	entry = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(entry), STL_PROPERTY_NAME_SIZE - 1);
	gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
	dlg = dlg_dialog_sync_new(hbox, _("New Action Property"), NULL);
	gtk_widget_grab_focus(entry);
	if(dlg_dialog_sync_wait(dlg) == DLG_POSITIVE)
	{
		const gchar	*name;

		if(((name = gtk_entry_get_text(GTK_ENTRY(entry))) != NULL) && *name != '\0')
		{
			stl_style_property_set_action(style, name, _("something"));
			set_widgets_action(page, style);
			reset_widgets_action(page);
			page->modified = TRUE;
		}
	}
	dlg_dialog_sync_destroy(dlg);
}

/* 2014-12-26 -	Go to the command editor for the currently chosen command. Convenient? */
static void evt_editcmd_clicked(GtkWidget *wid, gpointer user)
{
	const P_Styles	*page = user;
	const Style	*style;
	const gchar	*cmdseq;

	if((style = style_get_selected(page, NULL)) == NULL)
		return;
	cmdseq = stl_style_property_get_action(style, page->curr_prop);
	if(ccs_goto_cmdseq(cmdseq))
	{
		cfg_goto_page("Definitions");	/* This won't win any prices for low coupling. */
	}
}

/* 1999-05-25 -	User clicked the "Delete" (or "Revert...") button. Do it. */
static void evt_aprop_delete_clicked(GtkWidget *wid, gpointer user)
{
	P_Styles	*page = user;
	Style		*style;

	if((style = style_get_selected(page, NULL)) == NULL)
		return;

	if((page->curr_prop != NULL))
	{
		stl_style_property_remove(style, page->curr_prop);
		set_widgets_action(page, style);
		reset_widgets_action(page);
		page->modified = TRUE;
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-26 -	Add a new style. Use the currently selected style, if one exists, as parent.
**		If there is no selection, add style with Root as parent.
*/
static void evt_style_add_clicked(GtkWidget *wid, gpointer user)
{
	P_Styles	*page = user;

	if(page != NULL)
	{
		Style		*stl, *parent;
		GtkTreeIter	iter;

		stl = stl_style_new_unique_name(page->si);
		/* Parent is either current selection, or root. */
		if((parent = style_get_selected(page, &iter)) == NULL)
			parent = stl_styleinfo_style_root(page->si);
		stl_styleinfo_style_add(page->si, parent, stl);
		stl_style_set_expand(parent, TRUE);
		populate_tree(page);
		if(stl_styleinfo_tree_find_style(page->si, page->store, stl, &iter))
		{
			gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->tree)), &iter);
			gtk_editable_select_region(GTK_EDITABLE(page->dname), 0, -1);
			gtk_widget_grab_focus(page->dname);
		}
	}
}

/* 1999-05-27 -	Delete the currently selected style. If the style has children, the user must
**		confirm the operation, since all children will be deleted, too.
*/
static void evt_style_del_clicked(GtkWidget *wid, gpointer user)
{
	P_Styles	*page = user;
	Style		*style;
	gint		ok = DLG_POSITIVE;

	if((style = style_get_selected(page, NULL)) == NULL)
		return;

	if(stl_styleinfo_style_has_children(page->si, style))
		ok = dlg_dialog_sync_new_simple_wait(_("Deleting this style will also delete\n"
			"all its children. Are you sure?"), _("Confirm Delete"), _("_Delete|_Cancel"));
	if(ok == DLG_POSITIVE)
	{
		GList	*chlist, *iter;
		Style	*stl;

		stl = stl_styleinfo_style_root(page->si);
		chlist = stl_styleinfo_style_get_children(page->si, style, TRUE);
		for(iter = chlist; iter != NULL; iter = g_list_next(iter))
			ctp_replace_style(stl_style_get_name(iter->data), stl);
		g_list_free(chlist);
		stl_styleinfo_style_remove(page->si, style);
		populate_tree(page);
		reset_widgets(page);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-22 -	Build the visual property editing widgets. */
static void build_pvisual(P_Styles *page)
{
	PVisual			*pv = &page->dpvisual;
	const gchar		*vplab[] = { N_("Background Color"), N_("Foreground Color"), N_("Icon") },
				*vpname[] = { SPN_COL_UNSEL_BG, SPN_COL_UNSEL_FG, SPN_ICON_UNSEL };
	gchar			ptxt[] = N_("(Row Style Preview Text)");
	GtkListStore		*store;
	GtkTreeIter		iter;
	GtkWidget		*hbox, *frame, *vbox, *label;
	guint			i;

	pv->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new(_("Preview"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	store = gtk_list_store_new(2, GDK_TYPE_PIXBUF, G_TYPE_STRING);
	gtk_list_store_insert_with_values(store, &iter, -1, COLUMN_NAME, _(ptxt), -1);
	pv->preview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	pv->pre_icon_r = gtk_cell_renderer_pixbuf_new();
	pv->pre_icon = gtk_tree_view_column_new_with_attributes("(title)", pv->pre_icon_r, "pixbuf", COLUMN_ICON, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(pv->preview), pv->pre_icon);
	pv->pre_name_r = gtk_cell_renderer_text_new();
	pv->pre_name = gtk_tree_view_column_new_with_attributes("(title)", pv->pre_name_r, "text", COLUMN_NAME, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(pv->preview), pv->pre_name);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(pv->preview), FALSE);
	gtk_widget_set_name(pv->preview, "cstPreview");
	gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(pv->preview)), GTK_SELECTION_SINGLE);	/* Or none, like before? */
	gtk_box_pack_start(GTK_BOX(hbox), pv->preview, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(pv->vbox), hbox, FALSE, FALSE, 2);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	for(i = 0; i < 3; i++)
	{
		frame = gtk_frame_new(_(vplab[i]));
		vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
		pv->override[i] = gtk_check_button_new_with_label(_("Override Parent's?"));
		g_object_set_data(G_OBJECT(pv->override[i]), "user", (gpointer) vpname[i]);
		g_signal_connect(G_OBJECT(pv->override[i]), "toggled", G_CALLBACK(evt_vprop_override_toggled), page);
		gtk_box_pack_start(GTK_BOX(vbox), pv->override[i], FALSE, FALSE, 0);
		pv->edit[i] = gtk_button_new_with_label(i < 2 ? _("Edit...") : _("Pick..."));
		g_object_set_data(G_OBJECT(pv->edit[i]), "user", (gpointer) vpname[i]);
		g_signal_connect(G_OBJECT(pv->edit[i]), "clicked", G_CALLBACK(evt_vprop_edit_clicked), page);
		gtk_box_pack_start(GTK_BOX(vbox), pv->edit[i], FALSE, FALSE, 0);
		gtk_container_add(GTK_CONTAINER(frame), vbox);
		gtk_box_pack_start(GTK_BOX(hbox), frame, TRUE, TRUE, 5);
	}
	gtk_box_pack_start(GTK_BOX(pv->vbox), hbox, FALSE, FALSE, 0);
}

/* 1999-05-22 -	Build action property editing widgetry. */
static void build_paction(P_Styles *page)
{
	PAction		*pa = &page->dpaction;
	GtkWidget	*hbox, *btn, *scwin;
	GtkCellRenderer	*cr;
	GtkTreeViewColumn *vc;

	pa->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	pa->store = gtk_list_store_new(ACTION_COLUMN_COUNT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_INT);
	pa->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(pa->store));
	cr = gtk_cell_renderer_text_new();
	g_object_set(G_OBJECT(cr), "editable", TRUE, NULL);
	g_signal_connect(G_OBJECT(cr), "edited", G_CALLBACK(evt_aprop_name_edited), page);
	vc = gtk_tree_view_column_new_with_attributes("(Name)", cr, "text", ACTION_COLUMN_NAME, "weight", ACTION_COLUMN_WEIGHT, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(pa->view), vc);
	cr = gtk_cell_renderer_text_new();
	g_object_set(G_OBJECT(cr), "editable", TRUE, NULL);
	g_signal_connect(G_OBJECT(cr), "editing_started", G_CALLBACK(evt_aprop_cmdseq_edit_started), page);
	g_signal_connect(G_OBJECT(cr), "edited", G_CALLBACK(evt_aprop_cmdseq_edited), page);
	vc = gtk_tree_view_column_new_with_attributes("(CmdSeq)", cr, "text", ACTION_COLUMN_CMDSEQ, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(pa->view), vc);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(pa->view), FALSE);
	g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(pa->view))), "changed", G_CALLBACK(evt_aprop_selection_changed), page);
	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request(scwin, -1, 100);
	gtk_container_add(GTK_CONTAINER(scwin), pa->view);
	gtk_box_pack_start(GTK_BOX(pa->vbox), scwin, TRUE, TRUE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_set_homogeneous(GTK_BOX(hbox), TRUE);
	btn = gtk_button_new_with_label(_("Add Action..."));
	g_signal_connect(G_OBJECT(btn), "clicked", G_CALLBACK(evt_aprop_add_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), btn, TRUE, TRUE, 5);
	pa->editcmd = gtk_button_new_with_label(_("Edit Command"));
	g_signal_connect(G_OBJECT(pa->editcmd), "clicked", G_CALLBACK(evt_editcmd_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), pa->editcmd, TRUE, TRUE, 5);
	pa->adel = gtk_button_new_with_label(_("Delete Action"));
	g_signal_connect(G_OBJECT(pa->adel), "clicked", G_CALLBACK(evt_aprop_delete_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), pa->adel, TRUE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(pa->vbox), hbox, FALSE, FALSE, 5);
}

/* 1999-05-22 -	Build style configuration GUI page. */
static GtkWidget * cst_init(MainInfo *min, gchar **name)
{
	P_Styles	*page = &the_page;
	GtkWidget	*grid, *label, *frame, *hbox, *btn;
	GtkCellRenderer	*cr;
	GtkTreeViewColumn *vc;

	page->min = min;
	page->si  = NULL;
	page->modified = FALSE;
	page->store = NULL;

	page->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	page->handlers = gui_handler_group_new();

	page->scwin = gtk_scrolled_window_new(FALSE, FALSE);
	page->tree = gtk_tree_view_new();
	cr = gtk_cell_renderer_text_new();
	vc = gtk_tree_view_column_new_with_attributes("(Styles)", cr, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(page->tree), vc);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(page->tree), FALSE);
	g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->tree))), "changed", G_CALLBACK(evt_style_selection_changed), page);
	page->sig_expand   = g_signal_connect(G_OBJECT(page->tree), "row_expanded", G_CALLBACK(evt_style_row_expanded), page);
	page->sig_collapse = g_signal_connect(G_OBJECT(page->tree), "row_collapsed", G_CALLBACK(evt_style_row_collapsed), page);
	gtk_container_add(GTK_CONTAINER(page->scwin), page->tree);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(page->scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(page->vbox), page->scwin, TRUE, TRUE, 0);

	page->dvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	grid = gtk_grid_new();
	label = gtk_label_new(_("Name"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
	page->dname = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(page->dname), STL_STYLE_NAME_SIZE - 1);
	gui_handler_group_connect(page->handlers, G_OBJECT(page->dname), "changed", G_CALLBACK(evt_style_name_changed), page);
	gtk_grid_attach(GTK_GRID(grid), page->dname, 1, 0, 2, 1);
	label = gtk_label_new(_("Parent"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
	page->dparent = gtk_entry_new();
	gtk_widget_set_hexpand(page->dparent, TRUE);
	gtk_widget_set_halign(page->dparent, GTK_ALIGN_FILL);
	gtk_entry_set_max_length(GTK_ENTRY(page->dparent), STL_STYLE_NAME_SIZE - 1);
	gtk_editable_set_editable(GTK_EDITABLE(page->dparent), FALSE);
	gtk_grid_attach(GTK_GRID(grid), page->dparent, 1, 1, 1, 1);
	page->dreparent = gui_details_button_new();
	g_signal_connect(G_OBJECT(page->dreparent), "clicked", G_CALLBACK(evt_style_parent_clicked), page);
	gtk_grid_attach(GTK_GRID(grid), page->dreparent, 2, 1, 1, 1);
	gtk_box_pack_start(GTK_BOX(page->dvbox), grid, FALSE, FALSE, 0);

	frame = gtk_frame_new(_("Inherited Properties"));
	page->dpnbook = gtk_notebook_new();
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(page->dpnbook), GTK_POS_LEFT);
	build_pvisual(page);
	gtk_notebook_append_page(GTK_NOTEBOOK(page->dpnbook), page->dpvisual.vbox, gtk_label_new(_("Visual")));
	build_paction(page);
	gtk_notebook_append_page(GTK_NOTEBOOK(page->dpnbook), page->dpaction.vbox, gtk_label_new(_("Actions")));
	gtk_container_add(GTK_CONTAINER(frame), page->dpnbook);

	gtk_box_pack_start(GTK_BOX(page->dvbox), frame, FALSE, FALSE, 5);
	gtk_box_pack_start(GTK_BOX(page->vbox), page->dvbox, FALSE, FALSE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	btn = gtk_button_new_with_label(_("Add"));
	g_signal_connect(G_OBJECT(btn), "clicked", G_CALLBACK(evt_style_add_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), btn, TRUE, TRUE, 5);
	page->del = gtk_button_new_with_label(_("Delete"));
	g_signal_connect(G_OBJECT(page->del), "clicked", G_CALLBACK(evt_style_del_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), page->del, TRUE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(page->vbox), hbox, FALSE, FALSE, 5);

	gtk_widget_show_all(page->vbox);

	cfg_tree_level_begin(_("File Recognition"));
	cfg_tree_level_append(_("Styles"), page->vbox);

	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-12 -	Update style display. */
static void cst_update(MainInfo *min)
{
	P_Styles	*page = &the_page;

	page->si = stl_styleinfo_copy(min->cfg.style);
	populate_tree(page);
	reset_widgets(page);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-27 -	Accept changes, if any. Causes a call to the relink function in types config.
** 1999-06-13 -	Fixed a rather sneaky problem: if the style for a _type_ changed, it would point
**		into the editing copies here. If the editing copies went away because of the
**		'modified' flag being FALSE, those types were left with dangling style pointers.
**		The fix was simple.
*/
static void cst_accept(MainInfo *min)
{
	P_Styles	*page = &the_page;

	if(page->modified)
	{
		ctp_relink_styles(min->cfg.style, page->si);
		stl_styleinfo_destroy(min->cfg.style);
		min->cfg.style = page->si;
		page->si = NULL;
		page->modified = FALSE;
		cfg_set_flags(CFLG_RESCAN_LEFT | CFLG_RESCAN_RIGHT);
	}
	else	/* Make sure the types link to the existing styles. */
		ctp_relink_styles(page->si, min->cfg.style);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-27 -	Save the current style configuration. Thanks to the styles module, this
**		is really not complicated. :)
*/
static gint cst_save(MainInfo *min, FILE *out)
{
	stl_styleinfo_save(min, min->cfg.style, out, NODE);

	return TRUE;
}

/* 1998-08-24 -	Load style configuration info from given XML tree. */
static void cst_load(MainInfo *min, const XmlNode *node)
{
	/* Free the built-in first. */
	stl_styleinfo_destroy(min->cfg.style);
	min->cfg.style = stl_styleinfo_load(node);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-27 -	When the config window closes, free the editing copies since they're bulky. */
static void cst_hide(MainInfo *min)
{
	P_Styles	*page = &the_page;

	stl_styleinfo_destroy(page->si);
	page->si = NULL;
	populate_tree(page);	/* This will destroy the tree widget and *not* create a new one, since there's no si. */
	reset_widgets(page);
}

/* ----------------------------------------------------------------------------------------- */

const CfgModule * cst_describe(MainInfo *min)
{
	static const CfgModule	desc = { NODE, cst_init, cst_update, cst_accept, cst_save, cst_load, cst_hide };

	return &desc;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-27 -	Get the most current style info, namely the editing version. */
StyleInfo * cst_get_styleinfo(void)
{
	return the_page.si;
}
