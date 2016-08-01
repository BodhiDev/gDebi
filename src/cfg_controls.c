/*
** 1999-03-16 -	Configure controls. Initially, this will only have global keyboard shortcuts
**		to deal with, but it may get more in the future. This module relies heavily
**		on the services provided by the 'controls' module, of course.
*/

#include "gentoo.h"

#include "cmdseq_dialog.h"

#include "cmdseq.h"
#include "controls.h"
#include "dialog.h"
#include "guiutil.h"
#include "strutil.h"

#include "configure.h"
#include "cfg_module.h"
#include "cfg_cmdseq.h"				/* For ccs_current(). */
#include "cfg_controls.h"

#define	NODE	"Controls"

/* ----------------------------------------------------------------------------------------- */

#include "graphics/icon_mouse1.xpm"
#include "graphics/icon_mouse2.xpm"
#include "graphics/icon_mouse3.xpm"
#include "graphics/icon_mouse4.xpm"
#include "graphics/icon_mouse5.xpm"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	GtkWidget	*vbox;			/* Root page container. Very standard. */

	GtkWidget	*kframe;		/* Keyboard frame. */
	GtkListStore	*kstore;		/* Stores keyboard shortcuts. */
	GtkWidget	*kview;			/* A tree view for the keyboard shortcuts. */
	GtkWidget	*kdtable;		/* Key definition table. */
	GtkWidget	*kdkey;			/* Definition key name (entry). */
	GtkWidget	*kdcmd;			/* Definition cmdsequence (entry). */
	GtkWidget	*kdcmdpick;		/* Definition command pick button. */
	GtkWidget	*kadd;			/* Keyboard add button. */
	GtkWidget	*kdel;			/* Keyboard delete button. */

	GtkWidget	*mframe;		/* Dirpane mouse button config frame. */
	GtkListStore	*mstore;		/* Stores mouse button configs. */
	GtkWidget	*mview;			/* A tree view for mouse button settings. */
	GtkWidget	*mscwin;		/* Scrolling window (obsolete). */
	GtkWidget	*mdtable;		/* Definition table. */
	GtkWidget	*mdcombo;		/* Combobox showing mouse button for mapping. */
	GtkWidget	*mdcmd;
	GtkWidget	*mdcmdpick;
	GtkWidget	*mhbox;
	GtkWidget	*madd, *mdel;

	GdkPixbuf	*mpbuf[5];		/* Mouse icon pixbufs. */

	GtkWidget	*cmccmd;		/* Entry widget for Click-M-Click command. */
	GtkWidget	*cmcdelay;		/* HScale widget for cmc delay. */

	GtkWidget	*nonumlock;		/* Ignore numlock check button. */

	MainInfo	*min;			/* This is handy sometimes. */
	CtrlInfo	*ctrlinfo;		/* The control info we're editing. */

	gboolean	modified;		/* Indicates that a change has been made. */
} P_Controls;

enum {
	KEY_COLUMN_KEYNAME = 0,
	KEY_COLUMN_CMDSEQ,
	KEY_COLUMN_KEY,

	KEY_COLUMN_COUNT
};

enum {
	MOUSE_COLUMN_ICON = 0,
	MOUSE_COLUMN_BUTTON,
	MOUSE_COLUMN_STATE,
	MOUSE_COLUMN_CMDSEQ,
	MOUSE_COLUMN_MOUSE,

	MOUSE_COLUMN_COUNT
};

static P_Controls	the_page;

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-16 -	Reset the key definition widgets to their idle state. */
static void reset_key_widgets(P_Controls *page)
{
	gtk_entry_set_text(GTK_ENTRY(page->kdkey), "");
	gtk_entry_set_text(GTK_ENTRY(page->kdcmd), "");
	gtk_widget_set_sensitive(page->kdtable, FALSE);
	gtk_widget_set_sensitive(page->kadd, TRUE);
	gtk_widget_set_sensitive(page->kdel, FALSE);
}

/* 1999-06-13 -	Reset mouse editing widgets. */
static void reset_mouse_widgets(P_Controls *page)
{
	gtk_combo_box_set_active(GTK_COMBO_BOX(page->mdcombo), -1);
	gtk_entry_set_text(GTK_ENTRY(page->mdcmd), "");
	gtk_widget_set_sensitive(page->mdtable, FALSE);
	gtk_widget_set_sensitive(page->madd, TRUE);
	gtk_widget_set_sensitive(page->mdel, FALSE);
}

/* 1999-03-16 -	Reset all widgets on page to their most idle state. */
static void reset_widgets(P_Controls *page)
{
	const gchar	*cmd;

	reset_key_widgets(page);
	reset_mouse_widgets(page);
	cmd = ctrl_clickmclick_get_cmdseq(page->ctrlinfo);
	gtk_entry_set_text(GTK_ENTRY(page->cmccmd), cmd ? cmd : "");
	gtk_adjustment_set_value(gtk_range_get_adjustment(GTK_RANGE(page->cmcdelay)), ctrl_clickmclick_get_delay(page->ctrlinfo));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->nonumlock), ctrl_numlock_ignore_get(page->ctrlinfo));
}

/* ----------------------------------------------------------------------------------------- */

static void list_store_append_key(P_Controls *page, const CtrlKey *key, GtkTreeIter *iter)
{
	GtkTreeIter	myiter;

	/* If caller didn't provide an iter to use, use our local. */
	if(iter == NULL)
		iter = &myiter;
	gtk_list_store_insert_with_values(page->kstore, iter, -1,
			KEY_COLUMN_KEYNAME, ctrl_key_get_keyname(key),
			KEY_COLUMN_CMDSEQ,  ctrl_key_get_cmdseq(key),
			KEY_COLUMN_KEY,     key,
			-1);
}

/* 1999-03-16 -	Populate keyboard clist. */
static void populate_key_list(P_Controls *page)
{
	const GSList	*iter;

	gtk_list_store_clear(page->kstore);
	for(iter = ctrl_keys_get_list(page->ctrlinfo); iter != NULL; iter = g_slist_next(iter))
		list_store_append_key(page, iter->data, NULL);
}

static void list_store_append_mouse(P_Controls *page, const CtrlMouse *mouse, GtkTreeIter *iter)
{
	gchar   	*bname[] = { N_("Left"), N_("Middle"), N_("Right"), N_("Wheel Up"), N_("Wheel Down") };
	guint		btn;
	GtkTreeIter	myiter;

	/* If caller didn't provide an iter to use, use our local. */
	if(iter == NULL)
		iter = &myiter;
	btn = ctrl_mouse_get_button(mouse) - 1;
	gtk_list_store_insert_with_values(page->mstore, iter, -1,
			MOUSE_COLUMN_ICON,   page->mpbuf[btn],
			MOUSE_COLUMN_BUTTON, _(bname[btn]),
			MOUSE_COLUMN_STATE,  gtk_accelerator_name(0U, ctrl_mouse_get_state(mouse)),
			MOUSE_COLUMN_CMDSEQ, ctrl_mouse_get_cmdseq(mouse),
			MOUSE_COLUMN_MOUSE,  mouse,
			-1);
}

/* 1999-06-13 -	Populate the mouse command mapping list. */
static void populate_mouse_list(P_Controls *page)
{
	const GSList	*iter;

	gtk_list_store_clear(page->mstore);
	for(iter = ctrl_mouse_get_list(page->ctrlinfo); iter != NULL; iter = g_slist_next(iter))
		list_store_append_mouse(page, iter->data, NULL);
}

/* ----------------------------------------------------------------------------------------- */

/* 2009-03-14 -	Keyboard list selection changed. Updated widgetry for editing. */
static void evt_kselection_changed(GtkTreeSelection *ts, gpointer user)
{
	P_Controls	*page = user;
	GtkTreeModel	*model;
	GtkTreeIter	iter;
	CtrlKey		*key = NULL;

	if(gtk_tree_selection_get_selected(ts, &model, &iter))
		gtk_tree_model_get(model, &iter, KEY_COLUMN_KEY, &key, -1);
	gtk_entry_set_text(GTK_ENTRY(page->kdkey), key != NULL ? ctrl_key_get_keyname(key) : "");
	gtk_entry_set_text(GTK_ENTRY(page->kdcmd), key != NULL ? ctrl_key_get_cmdseq(key) : "");
	gtk_widget_set_sensitive(page->kdtable, key != NULL);
	gtk_widget_set_sensitive(page->kdel, key != NULL);
}

/* 1999-03-16 -	User pressed something in the key definition entry. Convert to name and set it.
** 2009-03-14 -	Adapted for new GTK+ 2.0 list selection management. 10 years is a good interval. :)
 */
static gint evt_kkey_press(GtkWidget *wid, GdkEventKey *evt, gpointer user)
{
	P_Controls	*page = user;
	GtkTreeModel	*model;
	GtkTreeIter	iter;
	CtrlKey		*key = NULL;

	if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->kview)), &model, &iter))
	{
		gchar	*keyname = gtk_accelerator_name(evt->keyval, evt->state);

		gtk_tree_model_get(model, &iter, 2, &key, -1);
		ctrl_key_set_keyname(page->ctrlinfo, key, keyname);
		gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, keyname, -1);
		gtk_entry_set_text(GTK_ENTRY(page->kdkey), keyname);
		g_signal_stop_emission_by_name(G_OBJECT(wid), "key_press_event");
		page->modified = TRUE;
	}
	return TRUE;
}

/* 1999-03-16 -	User edited the command sequence, so store the new one. */
static gint evt_kcmd_changed(GtkWidget *wid, gpointer user)
{
	P_Controls	*page = user;
	GtkTreeModel	*model;
	GtkTreeIter	iter;
	CtrlKey		*key = NULL;

	if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->kview)), &model, &iter))
	{
		const gchar	*cmdseq = gtk_entry_get_text(GTK_ENTRY(wid));

		gtk_tree_model_get(model, &iter, 2, &key, -1);
		if(!ctrl_key_has_cmdseq(page->ctrlinfo, key, cmdseq))
		{
			ctrl_key_set_cmdseq(page->ctrlinfo, key, cmdseq);
			gtk_list_store_set(GTK_LIST_STORE(model), &iter, 1, cmdseq, -1);
			page->modified = TRUE;
		}
	}
	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-16 -	User clicked the "pick" button for key command sequence.
** 1999-03-29 -	Rewritten for new, simpler, csq_dialog() semantics.
*/
static gint evt_kcmdpick_clicked(GtkWidget *wid, gpointer user)
{
	P_Controls	*page = user;
	const gchar	*cmd;

	if((cmd = csq_dialog_sync_new_wait(page->min, ccs_get_current())) != NULL)
		gtk_entry_set_text(GTK_ENTRY(page->kdcmd), cmd);
	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-16 -	User just hit the "Add" key button so, er, add a key. */
static gint evt_kadd_clicked(GtkWidget *wid, gpointer user)
{
	P_Controls	*page = user;
	CtrlKey		*key;
	GtkTreeIter	iter;
	GtkTreePath	*path;

	key = ctrl_key_add_unique(page->ctrlinfo);
	ctrl_key_set_cmdseq(page->ctrlinfo, key, "");
	gtk_list_store_insert_with_values(page->kstore, &iter, -1,
				0, ctrl_key_get_keyname(key),
				1, ctrl_key_get_cmdseq(key),
				2, key,
				-1);
	gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->kview)), &iter);
	path = gtk_tree_model_get_path(GTK_TREE_MODEL(page->kstore), &iter);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(page->kview), path, NULL, TRUE, 0.5f, 0.0f);
	gtk_tree_path_free(path);
	gtk_widget_grab_focus(page->kdkey);
	page->modified = TRUE;

	return TRUE;
}

/* 1999-03-16 -	User hit the "Delete" button for keys. */
static gint evt_kdel_clicked(GtkWidget *wid, gpointer user)
{
	P_Controls	*page = user;
	GtkTreeModel	*model;
	GtkTreeIter	iter;
	CtrlKey		*key = NULL;

	if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->kview)), &model, &iter))
	{
		gtk_tree_model_get(model, &iter, 2, &key, -1);
		ctrl_key_remove(page->ctrlinfo, key);
		gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
		page->modified = TRUE;
	}
	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 2009-03-14 -	The selection in the mouse binding list has changed. Deal with it. */
static void evt_mselection_changed(GtkTreeSelection *ts, gpointer user)
{
	P_Controls	*page = user;
	GtkTreeModel	*model;
	GtkTreeIter	iter;
	CtrlMouse	*mouse = NULL;

	if(gtk_tree_selection_get_selected(ts, &model, &iter))
		gtk_tree_model_get(model, &iter, MOUSE_COLUMN_MOUSE, &mouse, -1);
	gtk_entry_set_text(GTK_ENTRY(page->mdcmd), mouse != NULL ? ctrl_mouse_get_cmdseq(mouse) : "");
	gtk_combo_box_set_active(GTK_COMBO_BOX(page->mdcombo), ctrl_mouse_get_button(mouse) - 1);
	gtk_widget_set_sensitive(page->mdtable, mouse != NULL);
	gtk_widget_set_sensitive(page->mdel, mouse != NULL);
}

static void evt_mbutton_changed(GtkWidget *wid, gpointer user)
{
	P_Controls	*page = user;
	GtkTreeSelection *ts;
	GtkTreeIter	ti, biter;
	CtrlMouse	*mouse;
	guint		btn;

	ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(page->mview));
	if(ts == NULL || !gtk_tree_selection_get_selected(ts, NULL, &ti))
		return;
	gtk_tree_model_get(GTK_TREE_MODEL(page->mstore), &ti, MOUSE_COLUMN_MOUSE, &mouse, -1);
	btn = gtk_combo_box_get_active(GTK_COMBO_BOX(page->mdcombo));
	ctrl_mouse_set_button(mouse, btn + 1);

	/* Almost-too-clever: grab the translated mouse button label from the GtkComboBox's model. */
	if(gtk_combo_box_get_active_iter(GTK_COMBO_BOX(page->mdcombo), &biter))
	{
		gchar	*button;

		gtk_tree_model_get(gtk_combo_box_get_model(GTK_COMBO_BOX(page->mdcombo)), &biter, 1, &button, -1);

		gtk_list_store_set(GTK_LIST_STORE(page->mstore), &ti,
					MOUSE_COLUMN_ICON, page->mpbuf[btn],
					MOUSE_COLUMN_BUTTON, button,
					-1);
		g_free(button);
	}
}

/* 1999-06-13 -	User just hit the "Edit Modifiers..." button. Bring up a little dialog. */
static void evt_mstate_clicked(GtkWidget *wid, gpointer user)
{
	P_Controls	*page = user;
	GtkTreeSelection *ts;
	GtkTreeIter	ti;
	CtrlMouse	*mouse;
	Dialog		*dlg;
	guint		i, x, y,
			mask[] = { GDK_SHIFT_MASK, GDK_CONTROL_MASK, GDK_MOD1_MASK, GDK_MOD2_MASK,
				   GDK_MOD3_MASK, GDK_MOD4_MASK, GDK_MOD5_MASK };
	GtkWidget	*grid, *label, *check[sizeof mask / sizeof mask[0]];

	ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(page->mview));
	if(ts == NULL || !gtk_tree_selection_get_selected(ts, NULL, &ti))
		return;
	gtk_tree_model_get(GTK_TREE_MODEL(page->mstore), &ti, MOUSE_COLUMN_MOUSE, &mouse, -1);

	grid = gtk_grid_new();
	label = gtk_label_new(_("The following modifier key(s) must\n"
				"be held down when the mouse button\n"
				"is clicked to trigger the command"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 2, 1);
	for(i = 0, x = 0, y = 1; i < sizeof mask / sizeof mask[0]; i++, y++)
	{
		check[i] = gtk_check_button_new_with_label(gtk_accelerator_name(0, mask[i]));
		if(ctrl_mouse_get_state(mouse) & mask[i])
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check[i]), TRUE);
		gtk_grid_attach(GTK_GRID(grid), check[i], x, y, 1, 1);
		if(i == (sizeof mask / sizeof *mask) / 2)
			x++, y = 0;
	}
	dlg = dlg_dialog_sync_new(grid, _("Edit Modifiers"), NULL);
	if(dlg_dialog_sync_wait(dlg) == DLG_POSITIVE)
	{
		guint	ns = 0U;

		for(i = 0; i < sizeof mask / sizeof mask[0]; i++)
		{
			if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check[i])))
				ns |= mask[i];
		}
		ctrl_mouse_set_state(mouse, ns);
		gtk_list_store_set(GTK_LIST_STORE(page->mstore), &ti, MOUSE_COLUMN_STATE, gtk_accelerator_name(0, ns), -1);
		page->modified = TRUE;
	}
	dlg_dialog_sync_destroy(dlg);
}

/* 1999-06-13 -	User is editing the command name. Update mapping. */
static void evt_mcmd_changed(GtkWidget *wid, gpointer user)
{
	P_Controls	*page = user;
	GtkTreeSelection *ts;
	GtkTreeIter	ti;
	CtrlMouse	*mouse;
	const gchar	*cmdseq;

	ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(page->mview));
	if(ts == NULL || !gtk_tree_selection_get_selected(ts, NULL, &ti))
		return;
	gtk_tree_model_get(GTK_TREE_MODEL(page->mstore), &ti, MOUSE_COLUMN_MOUSE, &mouse, -1);
	if((cmdseq = gtk_entry_get_text(GTK_ENTRY(page->mdcmd))) != NULL)
	{
		ctrl_mouse_set_cmdseq(mouse, cmdseq);
		gtk_list_store_set(page->mstore, &ti, MOUSE_COLUMN_CMDSEQ, cmdseq, -1);
		page->modified = TRUE;
	}
}

/* 1999-06-13 -	User clicked details button for mouse mapping command. Pop up dialog. */
static void evt_mcmdpick_clicked(GtkWidget *wid, gpointer user)
{
	P_Controls	*page = user;
	const gchar	*cmd;

	if((cmd = csq_dialog_sync_new_wait(page->min, ccs_get_current())) != NULL)
		gtk_entry_set_text(GTK_ENTRY(page->mdcmd), cmd);
}

/* 1999-06-20 -	User clicked the "Add" button for mouse bindings, so add one. */
static void evt_madd_clicked(GtkWidget *wid, gpointer user)
{
	P_Controls	*page = user;
	CtrlMouse	*mouse;
	GtkTreeIter	iter;
	GtkTreePath	*path;

	mouse = ctrl_mouse_add(page->ctrlinfo, 1U, 0U, "");
	if(mouse != NULL)
	{
		list_store_append_mouse(page, mouse, &iter);
		gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->mview)), &iter);
		path = gtk_tree_model_get_path(GTK_TREE_MODEL(page->mstore), &iter);
		gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(page->mview), path, NULL, TRUE, 0.5f, 0.0f);
		gtk_tree_path_free(path);
		page->modified = TRUE;
	}
}

/* 1999-06-21 -	The Delete-button has been clicked, remove current mouse command mapping. */
static void evt_mdel_clicked(GtkWidget *wid, gpointer user)
{
	P_Controls	*page = user;
	GtkTreeModel	*model;
	GtkTreeIter	iter;
	CtrlMouse	*mouse;

	if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->mview)), &model, &iter))
	{
		gtk_tree_model_get(model, &iter, MOUSE_COLUMN_MOUSE, &mouse, -1);
		ctrl_mouse_remove(page->ctrlinfo, mouse);
		gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
		page->modified = TRUE;
	}
}

/* 2003-10-09 -	User set a new Click-M-Click command. */
static void evt_clickmclick_changed(GtkWidget *wid, gpointer user)
{
	P_Controls	*page = user;

	if(page)
	{
		const gchar *text = gtk_entry_get_text(GTK_ENTRY(wid));

		ctrl_clickmclick_set_cmdseq(page->ctrlinfo, text);
		gtk_widget_set_sensitive(page->cmcdelay, *text);
		page->modified = TRUE;
	}
}

/* 2003-10-09 -	User wants help in picking a command for Click-M-Click. */
static void evt_clickmclick_pick_clicked(GtkWidget *wid, gpointer user)
{
	P_Controls	*page = user;

	if(page)
	{
		const gchar	*cmd = csq_dialog_sync_new_wait(page->min, ccs_get_current());
		if(cmd)
			gtk_entry_set_text(GTK_ENTRY(page->cmccmd), cmd);
	}
}

/* 2003-10-09 -	Maximum allowed delay for Click-M-Click recognition changed. */
static void evt_clickmclick_delay_changed(GtkAdjustment *adj, gpointer user)
{
	P_Controls	*page = user;

	if(page)
		ctrl_clickmclick_set_delay(page->ctrlinfo, gtk_adjustment_get_value(adj));
}

/* ----------------------------------------------------------------------------------------- */

/* 2000-02-18 -	User hit the "Ignore Num Lock?" toggle. Update editing state. */
static void evt_nonumlock_clicked(GtkWidget *wid, gpointer user)
{
	P_Controls	*page = user;

	if(page != NULL)
	{
		ctrl_numlock_ignore_set(page->ctrlinfo, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)));
		page->modified = TRUE;
	}
}

/* ----------------------------------------------------------------------------------------- */

static void build_mouse_menu(P_Controls *page)
{
	gchar		**gfx[]  = { icon_mouse1_xpm, icon_mouse2_xpm, icon_mouse3_xpm, icon_mouse4_xpm, icon_mouse5_xpm },
			*label[] = { N_("Left"), N_("Middle"), N_("Right"), N_("Wheel Up"), N_("Wheel Down") };
	guint		i;
	GtkListStore	*mmodel;
	GtkCellRenderer	*cr;

	for(i = 0; i < sizeof gfx / sizeof *gfx; i++)
		page->mpbuf[i] = gdk_pixbuf_new_from_xpm_data((const gchar **) gfx[i]);

	/* Now build a full TreeModel for the GtkOptionMenu replacement. Pfew!
	** NOTE: Unlike key and mouse true models, we don't store the button index
	** in the model; instead we just use get/set active() on the GtkComboBox.
	 */
	mmodel = gtk_list_store_new(2, GDK_TYPE_PIXBUF, G_TYPE_STRING);
	for(i = 0; i < sizeof gfx / sizeof *gfx; ++i)
	{
		GtkTreeIter	iter;
		gtk_list_store_insert_with_values(mmodel, &iter, -1, 0, page->mpbuf[i], 1, _(label[i]), -1);
	}
	page->mdcombo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(mmodel));
	cr = gtk_cell_renderer_pixbuf_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(page->mdcombo), cr, FALSE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(page->mdcombo), cr,
	                                "pixbuf", 0,
	                                NULL);

	cr = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(page->mdcombo), cr, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT(page->mdcombo), cr,
	                                "text", 1,
	                                NULL);

	g_signal_connect(G_OBJECT(page->mdcombo), "changed", G_CALLBACK(evt_mbutton_changed), page);
	gtk_grid_attach(GTK_GRID(page->mdtable), page->mdcombo, 1, 0, 1, 1);
}

static GtkWidget * cct_init(MainInfo *min, gchar **name)
{
	P_Controls	*page = &the_page;
	GtkWidget	*vbox, *frame, *label, *wid, *hbox, *scwin;
	GtkAdjustment	*adj;
	GtkCellRenderer	*cr;
	GtkTreeViewColumn *vc;
	GtkTreeSelection *ts;

	if(name == NULL)
		return NULL;
	*name = _("Controls");

	page->min = min;
	page->modified = FALSE;

	page->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	page->kframe = gtk_frame_new(_("Global Keyboard Shortcuts"));
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	page->kstore = gtk_list_store_new(KEY_COLUMN_COUNT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
	page->kview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(page->kstore));
	cr = gtk_cell_renderer_text_new();
	vc = gtk_tree_view_column_new_with_attributes("(Key)", cr, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(page->kview), vc);
	vc = gtk_tree_view_column_new_with_attributes("(Command)", cr, "text", 1, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(page->kview), vc);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(page->kview), FALSE);
	ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(page->kview));
	gtk_tree_selection_set_mode(ts, GTK_SELECTION_SINGLE);
	g_signal_connect(G_OBJECT(ts), "changed", G_CALLBACK(evt_kselection_changed), page);
	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scwin), page->kview);
	gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);

	page->kdtable = gtk_grid_new();
	label = gtk_label_new(_("Key"));
	gtk_grid_attach(GTK_GRID(page->kdtable), label, 0, 0, 1, 1);
	page->kdkey = gtk_entry_new();
	g_signal_connect(G_OBJECT(page->kdkey), "key_press_event", G_CALLBACK(evt_kkey_press), page);
	gtk_editable_set_editable(GTK_EDITABLE(page->kdkey), FALSE);
	gtk_grid_attach(GTK_GRID(page->kdtable), page->kdkey, 1, 0, 3, 1);
	label = gtk_label_new(_("Command"));
	gtk_grid_attach(GTK_GRID(page->kdtable), label, 0, 1, 1, 1);
	page->kdcmd = gtk_entry_new();
	gtk_widget_set_hexpand(page->kdcmd, TRUE);
	gtk_widget_set_halign(page->kdcmd, GTK_ALIGN_FILL);
	g_signal_connect(G_OBJECT(page->kdcmd), "changed", G_CALLBACK(evt_kcmd_changed), page);
	gtk_grid_attach(GTK_GRID(page->kdtable), page->kdcmd, 1, 1, 1, 1);
	page->kdcmdpick = gui_details_button_new();
	g_signal_connect(G_OBJECT(page->kdcmdpick), "clicked", G_CALLBACK(evt_kcmdpick_clicked), page);
	gtk_grid_attach(GTK_GRID(page->kdtable), page->kdcmdpick, 2, 1, 1, 1);
	gtk_box_pack_start(GTK_BOX(vbox), page->kdtable, FALSE, FALSE, 0);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	page->kadd = gtk_button_new_with_label(_("Add"));
	g_signal_connect(G_OBJECT(page->kadd), "clicked", G_CALLBACK(evt_kadd_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), page->kadd, TRUE, TRUE, 5);
	page->kdel = gtk_button_new_with_label(_("Delete"));
	g_signal_connect(G_OBJECT(page->kdel), "clicked", G_CALLBACK(evt_kdel_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), page->kdel, TRUE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);
	gtk_container_add(GTK_CONTAINER(page->kframe), vbox);
	gtk_box_pack_start(GTK_BOX(page->vbox), page->kframe, TRUE, TRUE, 0);

	page->mframe = gtk_frame_new(_("Dirpane Mouse Buttons"));
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	page->mstore = gtk_list_store_new(MOUSE_COLUMN_COUNT, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
	page->mview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(page->mstore));
	cr = gtk_cell_renderer_pixbuf_new();
	vc = gtk_tree_view_column_new_with_attributes("(BIcon)", cr, "pixbuf", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(page->mview), vc);
	cr = gtk_cell_renderer_text_new();
	vc = gtk_tree_view_column_new_with_attributes("(Button)", cr, "text", 1, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(page->mview), vc);
	vc = gtk_tree_view_column_new_with_attributes("(Modifier)", cr, "text", 2, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(page->mview), vc);
	vc = gtk_tree_view_column_new_with_attributes("(Command)", cr, "text", 3, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(page->mview), vc);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(page->mview), FALSE);
	ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(page->mview));
	gtk_tree_selection_set_mode(ts, GTK_SELECTION_SINGLE);
	g_signal_connect(G_OBJECT(ts), "changed", G_CALLBACK(evt_mselection_changed), page);
	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scwin), page->mview);
	gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);

	page->mdtable = gtk_grid_new();
	label = gtk_label_new(_("Button"));
	gtk_grid_attach(GTK_GRID(page->mdtable), label, 0, 0, 1, 1);
	build_mouse_menu(page);
	wid = gtk_button_new_with_label(_("Edit Modifiers..."));
	g_signal_connect(G_OBJECT(wid), "clicked", G_CALLBACK(evt_mstate_clicked), page);
	gtk_grid_attach(GTK_GRID(page->mdtable), wid, 2, 0, 2, 1);
	label = gtk_label_new(_("Command"));
	gtk_grid_attach(GTK_GRID(page->mdtable), label, 0, 1, 1, 1);
	page->mdcmd = gtk_entry_new();
	gtk_widget_set_hexpand(page->mdcmd, TRUE);
	gtk_widget_set_halign(page->mdcmd, GTK_ALIGN_FILL);
	g_signal_connect(G_OBJECT(page->mdcmd), "changed", G_CALLBACK(evt_mcmd_changed), page);
	gtk_grid_attach(GTK_GRID(page->mdtable), page->mdcmd, 1, 1, 2, 1);
	page->mdcmdpick = gui_details_button_new();
	g_signal_connect(G_OBJECT(page->mdcmdpick), "clicked", G_CALLBACK(evt_mcmdpick_clicked), page);
	gtk_grid_attach(GTK_GRID(page->mdtable), page->mdcmdpick, 3, 1, 1, 1);
	gtk_box_pack_start(GTK_BOX(vbox), page->mdtable, FALSE, FALSE, 0);
	page->mhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	page->madd = gtk_button_new_with_label(_("Add"));
	g_signal_connect(G_OBJECT(page->madd), "clicked", G_CALLBACK(evt_madd_clicked), page);
	gtk_box_pack_start(GTK_BOX(page->mhbox), page->madd, TRUE, TRUE, 5);
	page->mdel = gtk_button_new_with_label(_("Delete"));
	g_signal_connect(G_OBJECT(page->mdel), "clicked", G_CALLBACK(evt_mdel_clicked), page);
	gtk_box_pack_start(GTK_BOX(page->mhbox), page->mdel, TRUE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), page->mhbox, FALSE, FALSE, 5);
	gtk_container_add(GTK_CONTAINER(page->mframe), vbox);
	gtk_box_pack_start(GTK_BOX(page->vbox), page->mframe, TRUE, TRUE, 0);

	frame = gtk_frame_new(_("Click-M-Click Gesture"));
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new(_("Command"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	page->cmccmd = gtk_entry_new();
	g_signal_connect(G_OBJECT(page->cmccmd), "changed", G_CALLBACK(evt_clickmclick_changed), page);
	gtk_box_pack_start(GTK_BOX(hbox), page->cmccmd, TRUE, TRUE, 0);
	wid = gui_details_button_new();
	g_signal_connect(G_OBJECT(wid), "clicked", G_CALLBACK(evt_clickmclick_pick_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), wid, FALSE, FALSE, 0);
	label = gtk_label_new(_("Time Limit"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);
	adj = gtk_adjustment_new(0.400f, 0.100f, 1.5f, 0.1f, 0.1f, 0.0f);
	g_signal_connect(G_OBJECT(adj), "value_changed", G_CALLBACK(evt_clickmclick_delay_changed), page);
	page->cmcdelay = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, GTK_ADJUSTMENT(adj));
	gtk_scale_set_value_pos(GTK_SCALE(page->cmcdelay), GTK_POS_RIGHT);
	gtk_scale_set_digits(GTK_SCALE(page->cmcdelay), 3);
	gtk_box_pack_start(GTK_BOX(hbox), page->cmcdelay, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(frame), hbox);
	gtk_box_pack_start(GTK_BOX(page->vbox), frame, FALSE, FALSE, 0);

	page->nonumlock = gtk_check_button_new_with_label(_("Ignore Num Lock For All Bindings?"));
	g_signal_connect(G_OBJECT(page->nonumlock), "clicked", G_CALLBACK(evt_nonumlock_clicked), page);
	gtk_box_pack_start(GTK_BOX(page->vbox), page->nonumlock, FALSE, FALSE, 0);

	gtk_widget_show_all(page->vbox);
	return page->vbox;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-16 -	Update the controls config page by grabbing current settings. */
static void cct_update(MainInfo *min)
{
	the_page.ctrlinfo = ctrl_copy(min->cfg.ctrlinfo);
	populate_key_list(&the_page);
	populate_mouse_list(&the_page);
	reset_widgets(&the_page);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-17 -	The user just accepted the settings, so make them current. */
static void cct_accept(MainInfo *min)
{
	P_Controls	*page = &the_page;

	if(page->modified)
	{
		if(ctrl_mouse_ambiguity_exists(page->ctrlinfo))
		{
			dlg_dialog_async_new_simple(_("Note: The mouse button control settings\n"
						    "are ambiguous: the same button+modifier\n"
						    "is used for more than one function. This\n"
						    "might make their behaviour pretty weird..."),
							_("Warning"), _("_OK"), NULL, NULL);
		}
		ctrl_destroy(min->cfg.ctrlinfo);
		min->cfg.ctrlinfo = page->ctrlinfo;
		page->ctrlinfo = NULL;
		cfg_set_flags(CFLG_RESET_KEYBOARD);
		page->modified = FALSE;
	}
}

/* ----------------------------------------------------------------------------------------- */

static void keys_save(CtrlInfo *ctrl, FILE *out)
{
	GSList	*keys, *iter;

	xml_put_node_open(out, "Keys");
	if((keys = ctrl_keys_get_list(ctrl)) != NULL)
	{
		for(iter = keys; iter != NULL; iter = g_slist_next(iter))
		{
			const gchar	*key, *cmd;

			if((key = ctrl_key_get_keyname(iter->data)) && (cmd = ctrl_key_get_cmdseq(iter->data)))
			{
				xml_put_node_open(out, "Key");
				xml_put_text(out, "keyname", key);
				xml_put_text(out, "cmdseq", cmd);
				xml_put_node_close(out, "Key");
			}
		}
		g_slist_free(keys);
	}
	xml_put_node_close(out, "Keys");
}

/* 1999-06-20 -	Save out all mouse command bindings. */
static void mouse_save(CtrlInfo *ctrl, FILE *out)
{
	GSList	*mouse, *iter;

	xml_put_node_open(out, "MouseButtons");
	if((mouse = ctrl_mouse_get_list(ctrl)) != NULL)
	{
		for(iter = mouse; iter != NULL; iter = g_slist_next(iter))
		{
			xml_put_node_open(out, "MouseButton");
			xml_put_uinteger(out, "button", ctrl_mouse_get_button(iter->data));
			xml_put_uinteger(out, "state", ctrl_mouse_get_state(iter->data));
			xml_put_text(out, "cmdseq", ctrl_mouse_get_cmdseq(iter->data));
			xml_put_node_close(out, "MouseButton");
		}
		g_slist_free(mouse);
	}
	xml_put_node_close(out, "MouseButtons");
}

static void clickmclick_save(const CtrlInfo *ctrl, FILE *out)
{
	xml_put_node_open(out, "ClickMClick");
	xml_put_text(out, "cmdseq", ctrl_clickmclick_get_cmdseq(ctrl));
	xml_put_real(out, "delay",  ctrl_clickmclick_get_delay(ctrl));
	xml_put_node_close(out, "ClickMClick");
}

/* 2004-04-25 -	Save general bindings. */
static void general_save(const CtrlInfo *ctrl, FILE *out)
{
	GSList	*list;

	if((list = ctrl_general_get_contexts(ctrl)) != NULL)	/* Only emit toplevel if there are bindings. */
	{
		const GSList	*iter;

		xml_put_node_open(out, "Generals");
		for(iter = list; iter != NULL; iter = g_slist_next(iter))
		{
			xml_put_node_open(out, "General");
			xml_put_text(out, "context", (const gchar *) iter->data);
			xml_put_text(out, "cmdseq", ctrl_general_get_cmdseq(ctrl, (const gchar *) iter->data));
			xml_put_node_close(out, "General");
		}
		g_slist_free(list);
		xml_put_node_close(out, "Generals");
	}
}

/* 1999-03-17 -	Save the current control settings right out in given <file>. */
static gint cct_save(MainInfo *min, FILE *out)
{
	xml_put_node_open(out, "Controls");
	keys_save(min->cfg.ctrlinfo, out);
	mouse_save(min->cfg.ctrlinfo, out);
	clickmclick_save(min->cfg.ctrlinfo, out);
	general_save(min->cfg.ctrlinfo, out);
	xml_put_boolean(out, "ignore_numlock", ctrl_numlock_ignore_get(min->cfg.ctrlinfo));
	xml_put_node_close(out, "Controls");

	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-17 -	Load in and install a single key mapping. */
static void key_load(const XmlNode *node, gpointer user)
{
	const gchar	*name, *cmd;

	if(xml_get_text(node, "keyname", &name) && xml_get_text(node, "cmdseq", &cmd))
		ctrl_key_add(((MainInfo *) user)->cfg.ctrlinfo, name, csq_cmdseq_map_name(cmd, "keyboard shortcut"));
}

/* 1999-03-17 -	Load in all key mappings. */
static void keys_load(MainInfo *min, const XmlNode *node)
{
	xml_node_visit_children(node, key_load, min);
}

/* 1999-06-20 -	Load and install a single mouse command mapping. */
static void mousebutton_load(const XmlNode *node, gpointer user)
{
	const gchar	*cmd;
	guint		button, state;

	if(xml_get_uinteger(node, "button", &button) && xml_get_uinteger(node, "state", &state) && xml_get_text(node, "cmdseq", &cmd))
		ctrl_mouse_add(((MainInfo *) user)->cfg.ctrlinfo, button, state, csq_cmdseq_map_name(cmd, "dirpane mouse button"));
}

/* 1999-06-20 -	Load in the mouse command bindings. */
static void mousebuttons_load(MainInfo *min, const XmlNode *node)
{
	xml_node_visit_children(node, mousebutton_load, min);
}

/* 2004-04-26 -	Load a general command binding. */
static void general_load(const XmlNode *node, gpointer user)
{
	const gchar	*ctx, *cmd;

	if(xml_get_text(node, "context", &ctx) && xml_get_text(node, "cmdseq", &cmd))
		ctrl_general_set_cmdseq(((MainInfo *) user)->cfg.ctrlinfo, ctx, cmd);
}

/* 2004-04-26 -	Load a bunch of general command bindings. */
static void generals_load(MainInfo *min, const XmlNode *node)
{
	xml_node_visit_children(node, general_load, min);
}

/* 1999-03-17 -	Load in the control config information. Replaces current. */
static void cct_load(MainInfo *min, const XmlNode *node)
{
	const XmlNode	*data;

	if((node = xml_tree_search(node, "Controls")) != NULL)
	{
		gboolean	tmp;

		if((data = xml_tree_search(node, "Keys")) != NULL)
		{
			ctrl_keys_uninstall_all(min->cfg.ctrlinfo);
			ctrl_key_remove_all(min->cfg.ctrlinfo);
			keys_load(min, data);
		}
		if((data = xml_tree_search(node, "MouseButtons")) != NULL)
		{
			ctrl_mouse_remove_all(min->cfg.ctrlinfo);
			mousebuttons_load(min, data);
		}
		if((data = xml_tree_search(node, "ClickMClick")) != NULL)
		{
			const gchar	*cmd;
			gfloat		delay;
			if(xml_get_text(data, "cmdseq", &cmd) && xml_get_real(data, "delay", &delay))
			{
				ctrl_clickmclick_set_cmdseq(min->cfg.ctrlinfo, cmd);
				ctrl_clickmclick_set_delay(min->cfg.ctrlinfo, delay);
			}
		}
		if((data = xml_tree_search(node, "Generals")) != NULL)
		{
			ctrl_general_clear(min->cfg.ctrlinfo);
			generals_load(min, data);
		}
		if(xml_get_boolean(node, "ignore_numlock", &tmp))
			ctrl_numlock_ignore_set(min->cfg.ctrlinfo, tmp);
	}
}

/* ----------------------------------------------------------------------------------------- */

const CfgModule * cct_describe(MainInfo *min)
{
	static const CfgModule	desc = { NODE, cct_init, cct_update, cct_accept, cct_save, cct_load, NULL };

	return &desc;
}
