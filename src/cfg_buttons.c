/*
** 1998-09-15 -	Tired of hacking around in old code, I simply rewrite this configuration
**		page module from scratch. Nice since the definition of buttons and their
**		structuring has changed radically (for the second time this week...).
** 1998-09-28 -	Made the button editing directly in the page, no need for a dialog.
** 1998-12-23 -	Plenty of cleanups thanks to slightly changed buttons.c module.
** 1999-03-13 -	Adjustments for new dialog module.
** 1999-05-01 -	Huge rewrites to wedge the new multi-face button widget in here. Also made
**		the buttons.c module a lot more opaque, and even moved the load/save code
**		in there.
** 1999-06-19 -	Adapted for the new dialog module. Really helped.
*/

#include "gentoo.h"

#include "buttons.h"
#include "dialog.h"
#include "strutil.h"
#include "guiutil.h"
#include "odmultibutton.h"
#include "xmlutil.h"
#include "cmdseq.h"
#include "cmdseq_dialog.h"
#include "color_dialog.h"

#include "configure.h"
#include "cfg_module.h"
#include "cfg_cmdseq.h"

#include "cfg_buttons.h"

/* ----------------------------------------------------------------------------------------- */

#define NODE	"ButtonSheets"

typedef enum { BM_NORMAL, BM_SELECTED, BM_COPY, BM_COPY_COLORS, BM_SWAP } BMode;

typedef struct P_Buttons	P_Buttons;

typedef struct {
	GtkWidget	*frame;			/* Frame holding the definition widgets. */
	GtkWidget	*label;			/* Textual label (entry widget). */
	guint		label_sig;
	GtkWidget	*cmdseq;		/* Command sequence (entry). */
	guint		cmdseq_sig;
	GtkWidget	*key;			/* Keyboard shortcut (entry). */
	GtkWidget	*col_bg;		/* Background color display, dialog request & clear button (multi-face). */
	GtkWidget	*col_fg;		/* Ditto, foreground. */
} BDef;

struct P_Buttons {
	GtkWidget	*vbox;			/* This practice is so old it has a beard. */
	GtkWidget	*scwin;			/* A scrolled window that holds the button rows. */
	GtkWidget	*shvbox;		/* The actual button sheet. */

	GtkWidget	*dvbox;			/* All definitions. */
	GtkWidget	*dhbox;			/* Actual function definitions (primary/secondary frames). */
	BDef		def[2];			/* Primary & secondary face definitions. */
	GtkWidget	*dtooltip;		/* String widget for tooltip. */
	GtkWidget	*dnarrow;		/* A check button for the narrow-flag. */
	guint		dnarsig;		/* Signal identifer for the narrow flag. */
	GtkWidget	*dshowtip;		/* Check button for "Show Tooltip?". */

	GtkWidget	*bcmd[4];		/* Button commands ("Edit", "Copy" etc.). */

	GtkWidget	*rcmd[5];		/* Row command buttons. */

	MainInfo	*min;
	const gchar	*cur_sheet;		/* Label of sheet we're editing. */
	gboolean	modified;
	ButtonInfo	buttons;		/* Editing copy of the button info. */
	ButtonSheet	*sheet;			/* Points at the sheet being shown (currently always "Default"). */
	GtkWidget	*button;		/* Currently selected button. */
	BtnFace		col_change_face;	/* A kludge to get it into cdl_dialog() callback. Only for color changing! */
	BMode		mode;
};

static P_Buttons	the_page;

/* ----------------------------------------------------------------------------------------- */

static gint	evt_button_clicked(GtkWidget *wid, gpointer user);

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-14 -	When <state> is TRUE, set the background color of <btn> to something special,
**		indicating that it is selected. When it's FALSE, reset the color to normal.
** 1999-05-05 -	Thanks no niceties in the new widget used for buttons, this function has become
**		far simpler. No more (yucky) GtkStyles. Just a simple widget call. Function renamed.
*/
static void set_button_state(GtkWidget *btn, gboolean state)
{
	od_multibutton_set_config_selected(OD_MULTIBUTTON(btn), state);
}

/* ----------------------------------------------------------------------------------------- */

static void set_blocked_entry(GtkWidget *entry, const gchar *str, guint sig)
{
	if(sig > 0)
		g_signal_handler_block(G_OBJECT(entry), sig);

	if(str != NULL)
		gtk_entry_set_text(GTK_ENTRY(entry), str);
	else
		gtk_entry_set_text(GTK_ENTRY(entry), "");

	if(sig > 0)
		g_signal_handler_unblock(G_OBJECT(entry), sig);
}

/* 1998-09-15 -	Reset all widgets to their most idle states. */
static void reset_widgets(P_Buttons *page)
{
	gint	i;

	page->button = NULL;
	for(i = 0; i < BTN_FACES; i++)
	{
		set_blocked_entry(page->def[i].label, "", page->def[i].label_sig);
		set_blocked_entry(page->def[i].cmdseq, "", page->def[i].cmdseq_sig);
		gtk_entry_set_text(GTK_ENTRY(page->def[i].key), "");
	}
	gtk_entry_set_text(GTK_ENTRY(page->dtooltip), "");
	g_signal_handler_block(G_OBJECT(page->dnarrow), page->dnarsig);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->dnarrow), FALSE);
	g_signal_handler_unblock(G_OBJECT(page->dnarrow), page->dnarsig);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->dshowtip), FALSE);
	gtk_widget_set_sensitive(page->dvbox, FALSE);
	for(i = 0; i < sizeof page->bcmd / sizeof page->bcmd[0]; i++)
		gtk_widget_set_sensitive(page->bcmd[i], FALSE);

	for(i = 1; i < sizeof page->rcmd / sizeof page->rcmd[0]; i++)
		gtk_widget_set_sensitive(page->rcmd[i], FALSE);
}

/* 1998-09-15 -	Set widgets to some fun states, now that a button is selected. */
static void set_widgets(P_Buttons *page)
{
	Button	*but;
	guint	i;

	if((page->button != NULL) && ((but = btn_button_get(page->button)) != NULL))
	{
		const gchar	*str;

		for(i = 0; i < BTN_FACES; i++)
		{
			set_blocked_entry(page->def[i].label, btn_button_get_label(but, i), page->def[i].label_sig);
			set_blocked_entry(page->def[i].cmdseq, btn_button_get_cmdseq(but, i), page->def[i].cmdseq_sig);
			set_blocked_entry(page->def[i].key, btn_button_get_key(but, i), 0);
		}
		if((str = btn_button_get_tooltip(but)) != NULL)
			gtk_entry_set_text(GTK_ENTRY(page->dtooltip), str);
		else
			gtk_entry_set_text(GTK_ENTRY(page->dtooltip), "");
		g_signal_handler_block(G_OBJECT(page->dnarrow), page->dnarsig);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->dnarrow), btn_button_get_flags_boolean(but, BTF_NARROW));
		g_signal_handler_unblock(G_OBJECT(page->dnarrow), page->dnarsig);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->dshowtip), btn_button_get_flags_boolean(but, BTF_SHOW_TOOLTIP));
	}
	gtk_widget_set_sensitive(page->dvbox, TRUE);
/*	gtk_widget_grab_focus(page->def[0].label);*/
	for(i = 0; i < sizeof page->bcmd / sizeof page->bcmd[0]; i++)
		gtk_widget_set_sensitive(page->bcmd[i], TRUE);

	for(i = 1; i < sizeof page->rcmd / sizeof page->rcmd[0]; i++)
		gtk_widget_set_sensitive(page->rcmd[i], TRUE);
}

/* 1998-09-15 -	Populate the scrolled window with a button sheet. */
static void populate_scwin(P_Buttons *page)
{
	if(page->shvbox != NULL)	/* The shvbox is made scrollable by gtk_container_add(), which inserts an extra parent. Hm. */
		gtk_container_remove(GTK_CONTAINER(page->scwin), gtk_widget_get_parent(page->shvbox));
	page->shvbox = NULL;

	if((page->shvbox = btn_buttonsheet_build(page->min, &page->buttons, page->cur_sheet, TRUE, G_CALLBACK(evt_button_clicked), page)) != NULL)
	{
		gtk_container_add(GTK_CONTAINER(page->scwin), page->shvbox);
		gtk_widget_show_all(page->shvbox);
	}
	reset_widgets(page);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-15 -	Set the page's mode to normal (no button selected). */
static void set_mode_normal(P_Buttons *page)
{
	if(page->button != NULL)
		set_button_state(page->button, FALSE);
	page->button = NULL;
	page->mode   = BM_NORMAL;
	reset_widgets(page);
}

/* 1998-09-15 -	Set normal mode, with <wid> selected. */
static void set_mode_selected(P_Buttons *page, GtkWidget *wid)
{
	if(page->button != NULL)
		set_button_state(page->button, FALSE);
	if(page->button == wid)
	{
		set_mode_normal(page);
		return;
	}
	set_button_state(wid, TRUE);
	page->button = wid;
	page->mode   = BM_SELECTED;
	set_widgets(page);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-15 -	One of the buttons in the sheet was clicked. */
static gint evt_button_clicked(GtkWidget *wid, gpointer user)
{
	Button		*but = btn_button_get(wid);
	P_Buttons	*page = user;
	gint		index = od_multibutton_get_index(OD_MULTIBUTTON(wid));

	switch(page->mode)
	{
		case BM_NORMAL:
		case BM_SELECTED:
			set_mode_selected(page, wid);
			gtk_widget_grab_focus(page->def[index].label);
			break;
		case BM_COPY:
			page->modified = TRUE;
			btn_button_copy(but, btn_button_get(page->button));
			set_mode_normal(page);
			populate_scwin(page);
			break;
		case BM_COPY_COLORS:
			page->modified = TRUE;
			btn_button_copy_colors(but, btn_button_get(page->button));
			set_mode_normal(page);
			populate_scwin(page);
			break;
		case BM_SWAP:
			page->modified = TRUE;
			btn_button_swap(but, btn_button_get(page->button));
			set_mode_normal(page);
			populate_scwin(page);
			break;
	}
	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-15 -	A button command ("Copy", "Swap" etc) was clicked. React.
** 1998-09-28 -	Cut out the stuff dealing with the "Edit" button, since the button's gone.
*/
static gint evt_bcmd_clicked(GtkWidget *wid, gpointer user)
{
	P_Buttons	*page = user;

	switch(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "index")))
	{
		case 0:
			page->mode = BM_COPY;
			break;
		case 1:
			page->mode = BM_COPY_COLORS;
			break;
		case 2:
			page->mode = BM_SWAP;
			break;
		case 3:
			btn_button_clear(btn_button_get(page->button));
			set_mode_normal(page);
			populate_scwin(page);
			page->modified = TRUE;
			break;
	}
	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-15 -	Pop up a dialog asking (with <text>) for a width. Defaults to <width>, and
**		calls <func> if the user clicked OK.
*/
static void width_dialog(const gchar *text, gint width, void (*func)(gpointer userdata, guint width), gpointer userdata)
{
	Dialog		*dlg;
	GtkWidget	*vbox, *label, *spin;
	GtkAdjustment	*adj;

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	label = gtk_label_new(text);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	spin = gtk_spin_button_new(NULL, 1, 0);
	if((adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(spin))) != NULL)
	{
		gtk_adjustment_set_lower(adj, 1.0f);
		gtk_adjustment_set_upper(adj, G_MAXINT);		/* When it comes to arbitrary limits, this is a good one. */
		gtk_adjustment_set_step_increment(adj, 1);
		gtk_adjustment_set_page_increment(adj, 16);
		gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(spin), adj);
	}
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), width);
	gtk_box_pack_start(GTK_BOX(vbox), spin, FALSE, FALSE, 0);
	dlg = dlg_dialog_sync_new(vbox, _("Set Row Width"), NULL);
	if(dlg_dialog_sync_wait(dlg) == DLG_POSITIVE)
		func(userdata, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin)));
	dlg_dialog_sync_destroy(dlg);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-15 -	Add a row of <width> buttons. This is a width_dialog() callback function. */
static void add_row(gpointer user, guint width)
{
	P_Buttons	*page = user;
	ButtonRow	*row = NULL;

	page->modified = TRUE;

	if(page->sheet == NULL)
	{
		page->sheet = btn_buttonsheet_new(_("Default"));
		btn_buttoninfo_add_sheet(&page->buttons, page->sheet);
	}
	if(page->sheet != NULL)
	{
		if(page->button != NULL)
			row = g_object_get_data(G_OBJECT(page->button), "row");
		btn_buttonsheet_insert_row(page->sheet, row, width);
		set_mode_normal(page);
		populate_scwin(page);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-15 -	Change the width of a row of buttons. A width_dialog() callback. */
static void resize_row(gpointer user, guint width)
{
	P_Buttons	*page = user;
	ButtonRow	*row;

	if((row = g_object_get_data(G_OBJECT(page->button), "row")) != NULL)
	{
		if(width != btn_buttonrow_get_width(row))	/* Don't call set_width() in vain. */
		{
			page->modified = TRUE;
			btn_buttonrow_set_width(row, width);
			set_mode_normal(page);
			populate_scwin(page);
		}
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-19 -	Move the row of the currently selected button <delta> rows (-1=>up, 1=>down). */
static void move_row(P_Buttons *page, gint delta)
{
	ButtonRow	*row;

	if(page->button == NULL)
		return;
	if(delta != -1 && delta != 1)
		return;

	if((row = g_object_get_data(G_OBJECT(page->button), "row")) != NULL)
	{
		page->modified = TRUE;
		btn_buttonsheet_move_row(page->sheet, row, delta);
		set_mode_normal(page);
		populate_scwin(page);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-01-13 -	This gets called as the user types around in the tooltip string. Inefficient. */
static gint evt_dtooltip_changed(GtkWidget *wid, gpointer user)
{
	P_Buttons	*page = user;
	Button		*but;
	const gchar	*text;

	if((page != NULL) && (page->button != NULL) && ((but = btn_button_get(page->button)) != NULL))
	{
		if((text = gtk_entry_get_text(GTK_ENTRY(wid))) != NULL)
		{
			btn_button_set_tooltip(but, text);
			page->modified = TRUE;
		}
	}
	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-28 -	The narrow-button was clicked, so grab & store its new value. */
static gint evt_dnarrow_clicked(GtkWidget *wid, gpointer user)
{
	P_Buttons	*page = user;
	Button		*but;

	if((page != NULL) && ((but = btn_button_get(page->button)) != NULL))
	{
		page->modified = TRUE;
		btn_button_set_flags_boolean(but, BTF_NARROW, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)));
		set_mode_normal(page);
		reset_widgets(page);
		populate_scwin(page);
	}
	return TRUE;
}

/* 1999-01-13 -	User clicked the "Show Tooltip?" button, so grab current state. */
static gint evt_dshowtip_clicked(GtkWidget *wid, gpointer user)
{
	P_Buttons	*page = user;
	Button		*but;

	if((page != NULL) && (page->button != NULL) && ((but = btn_button_get(page->button)) != NULL))
	{
		btn_button_set_flags_boolean(but, BTF_SHOW_TOOLTIP, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)));
		page->modified = TRUE;
	}
	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

static void delete_row(P_Buttons *page)
{
	if(dlg_dialog_sync_new_simple_wait(_("Really delete current button row?"), _("Please Confirm"), _("_Delete|_Cancel")) == DLG_POSITIVE)
	{
		btn_buttonsheet_delete_row(page->sheet, (ButtonRow *) g_object_get_data(G_OBJECT(page->button), "row"));
		set_mode_normal(page);
		populate_scwin(page);
		page->modified = TRUE;
	}
}

/* 1998-09-15 -	Row command button clicked, do something serious. */
static gint evt_rcmd_clicked(GtkWidget *wid, gpointer user)
{
	P_Buttons	*page = user;
	ButtonRow	*brw = NULL;
	gint		width = 8;

	if(page->button != NULL)
	{
		brw = g_object_get_data(G_OBJECT(page->button), "row");
		width = btn_buttonrow_get_width(brw);
	}
	switch(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "index")))
	{
		case 0:			/* Add? */
			width_dialog(_("Select Width for New Row"), width, add_row, page);
			break;
		case 1:			/* Change width? */
			width_dialog(_("Change Width of Row"), width, resize_row, page);
			break;
		case 2:			/* Move up? */
			move_row(page, -1);
			break;
		case 3:			/* Move down? */
			move_row(page, 1);
			break;
		case 4:			/* Delete? */
			delete_row(page);
			break;
	}
	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-01 -	User changed a button label. Update definition accordingly. */
static void evt_label_changed(GtkWidget *wid, gpointer user)
{
	P_Buttons	*page = user;
	const gchar	*text;
	BtnFace		face;

	if((page != NULL) && (page->button != NULL))
	{
		face = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(wid), "face"));
		if((text = gtk_entry_get_text(GTK_ENTRY(wid))) != NULL)
		{
			btn_button_set_label_widget(page->button, face, text);
			page->modified = TRUE;
		}
	}
}

/* 1999-05-01 -	User edited the command sequence name. Update button definition. */
static void evt_cmdseq_changed(GtkWidget *wid, gpointer user)
{
	P_Buttons	*page = user;
	Button		*btn;

	if((page != NULL) && ((btn = btn_button_get(page->button)) != NULL))
	{
		const gchar	*text;
		BtnFace		face;

		face = (BtnFace) GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(wid), "face"));
		if((text = gtk_entry_get_text(GTK_ENTRY(wid))) != NULL)
		{
			btn_button_set_cmdseq(btn, face, text);
			page->modified = TRUE;
		}
	}
}

/* 1999-05-01 -	User clicked on the "details" button (magnifying glass) for cmdseq. Bring up
**		a dialog.
*/
static void evt_cmdseq_pick_clicked(GtkWidget *wid, gpointer user)
{
	P_Buttons	*page = user;
	Button		*btn;

	if((page != NULL) && ((btn = btn_button_get(page->button)) != NULL))
	{
		BtnFace		face;
		const gchar	*csq;

		face = (BtnFace) GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(wid), "face"));
		if((csq = csq_dialog_sync_new_wait(page->min, ccs_get_current())) != NULL)
			gtk_entry_set_text(GTK_ENTRY(page->def[face].cmdseq), csq);	/* Rely on "changed" signal. */
	}
}

/* 1999-05-01 -	A key has been pressed in the "Key" entry box. Get key name and set it. */
static void evt_key_press(GtkWidget *wid, GdkEventKey *evt, gpointer user)
{
	P_Buttons	*page = user;
	Button		*btn;
	gchar		*key;

	if((key = gtk_accelerator_name(evt->keyval, evt->state)) != NULL)
	{
		if((page != NULL) && ((btn = btn_button_get(page->button)) != NULL))
		{
			BtnFace	face;

			face = (BtnFace) GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(wid), "face"));
			gtk_entry_set_text(GTK_ENTRY(wid), key);
			btn_button_set_key(btn, face, gtk_entry_get_text(GTK_ENTRY(wid)));
			page->modified = TRUE;
		}
		g_free(key);
	}
}

/* 1999-05-01 -	Clear the key for the clicked face. */
static void evt_key_clear_clicked(GtkWidget *wid, gpointer user)
{
	P_Buttons	*page = user;
	Button		*btn;

	if((page != NULL) && ((btn = btn_button_get(page->button)) != NULL))
	{
		BtnFace	face;

		face = (BtnFace) GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(wid), "face"));
		gtk_entry_set_text(GTK_ENTRY(page->def[face].key), "");
		btn_button_set_key(btn, face, NULL);
		page->modified = TRUE;
	}
}

/* 1999-05-02 -	This gets called by cdl_dialog() (the color_dialog module) when the bg color changes. */
static void col_bg_changed(const GdkRGBA *color, gpointer user)
{
	P_Buttons	*page = user;
	BtnFace		face;
	GdkColor	color_old;

	face = page->col_change_face;
	gui_color_from_rgba(&color_old, color);
	btn_button_set_color_bg_widget(page->button, face, &color_old);
}

/* 1999-05-02 -	User clicked the background color multi-faced button. Either bring up a dialog,
**		letting user pick a new color, or reset the button's color to the default.
*/
static void evt_col_bg_clicked(GtkWidget *wid, gpointer user)
{
	BtnFace		function = od_multibutton_get_index(OD_MULTIBUTTON(wid));
	P_Buttons	*page = user;
	Button		*btn;
	gint		tface = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "face"));

	if((page != NULL) && ((btn = btn_button_get(page->button)) != NULL))
	{
		if(function == BTN_PRIMARY)	/* Dialog-time? */
		{
			GdkColor	old_old;
			GdkRGBA		old, *op = NULL;
			guint		res;

			if(btn_button_get_color_bg(btn, tface, &old_old))
			{
				gui_rgba_from_color(&old, &old_old);
				op = &old;
			}
			page->col_change_face = tface;
			res = cdl_dialog_sync_new_wait(_("Edit Background Color"), col_bg_changed, op, page);
			if(res == DLG_POSITIVE)
				page->modified = TRUE;
			else if(op != NULL)
				btn_button_set_color_bg_widget(page->button, tface, &old_old);
		}
		else		/* Time to reset color to system default. */
		{
			btn_button_set_color_bg_widget(page->button, tface, NULL);
			page->modified = TRUE;
		}
	}
}

/* 1999-05-02 -	This gets called by cdl_dialog() (the color_dialog module) when the fg color changes. */
static void col_fg_changed(const GdkRGBA *color, gpointer user)
{
	P_Buttons	*page = user;
	BtnFace		face;
	GdkColor	color_old;

	face = page->col_change_face;
	gui_color_from_rgba(&color_old, color);
	btn_button_set_color_fg_widget(page->button, face, &color_old);
}

/* 2008-11-28 -	User clicked the foreground color multi-faced button. Either bring up a dialog,
**		letting user pick a new color, or reset the button's color to the default.
*/
static void evt_col_fg_clicked(GtkWidget *wid, gpointer user)
{
	BtnFace		function = od_multibutton_get_index(OD_MULTIBUTTON(wid));
	P_Buttons	*page = user;
	Button		*btn;
	gint		tface = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "face"));

	if((page != NULL) && ((btn = btn_button_get(page->button)) != NULL))
	{
		if(function == BTN_PRIMARY)	/* Dialog-time? */
		{
			GdkColor	old_old;
			GdkRGBA		old, *op = NULL;
			guint		res;

			if(btn_button_get_color_fg(btn, tface, &old_old))
			{
				gui_rgba_from_color(&old, &old_old);
				op = &old;
			}
			page->col_change_face = tface;
			res = cdl_dialog_sync_new_wait(_("Edit Foreground Color"), col_fg_changed, op, page);
			if(res == DLG_POSITIVE)
				page->modified = TRUE;
			else if(op != NULL)
				btn_button_set_color_fg_widget(page->button, tface, &old_old);
		}
		else		/* Time to reset color to system default. */
		{
			btn_button_set_color_fg_widget(page->button, tface, NULL);
			page->modified = TRUE;
		}
	}
}

/* 1999-04-24 -	Initialize widgetry for a button definition box. Thanks to the new wonderful
**		widget (contributed by J. Hanson <johan@tiq.com>) we now have two actions per
**		button. Very Opus-ish, and very good.
*/
static void init_bdef(MainInfo *min, P_Buttons *page, BDef *def, const gchar *lab, BtnFace face)
{
	GtkWidget	*grid, *label, *btn, *hbox;

	def->frame = gtk_frame_new(lab);
	grid = gtk_grid_new();

	label = gtk_label_new(_("Label"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
	def->label = gtk_entry_new();
	gtk_widget_set_hexpand(def->label, TRUE);
	gtk_widget_set_halign(def->label, GTK_ALIGN_FILL);
	gtk_entry_set_max_length(GTK_ENTRY(def->label), BTN_LABEL_SIZE - 1);
	g_object_set_data(G_OBJECT(def->label), "face", GUINT_TO_POINTER(face));
	def->label_sig = g_signal_connect(G_OBJECT(def->label), "changed", G_CALLBACK(evt_label_changed), page);
	gtk_grid_attach(GTK_GRID(grid), def->label, 1, 0, 2, 1);

	label = gtk_label_new(_("Command"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
	def->cmdseq = gtk_entry_new();
	g_object_set_data(G_OBJECT(def->cmdseq), "face", GUINT_TO_POINTER(face));
	def->cmdseq_sig = g_signal_connect(G_OBJECT(def->cmdseq), "changed", G_CALLBACK(evt_cmdseq_changed), page);
	gtk_grid_attach(GTK_GRID(grid), def->cmdseq, 1, 1, 1, 1);
	btn = gui_details_button_new();
	g_object_set_data(G_OBJECT(btn), "face", GUINT_TO_POINTER(face));
	g_signal_connect(G_OBJECT(btn), "clicked", G_CALLBACK(evt_cmdseq_pick_clicked), page);
	gtk_grid_attach(GTK_GRID(grid), btn, 2, 1, 1, 1);

	label = gtk_label_new(_("Key"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 2, 1, 1);
	def->key = gtk_entry_new();
	gtk_editable_set_editable(GTK_EDITABLE(def->key), FALSE);
	g_object_set_data(G_OBJECT(def->key), "face", GUINT_TO_POINTER(face));
	g_signal_connect(G_OBJECT(def->key), "key_press_event", G_CALLBACK(evt_key_press), page);
	gtk_grid_attach(GTK_GRID(grid), def->key, 1, 2, 1, 1);

	btn = gtk_button_new_from_icon_name("edit-clear", GTK_ICON_SIZE_MENU);
	g_object_set_data(G_OBJECT(btn), "face", GUINT_TO_POINTER(face));
	g_signal_connect(G_OBJECT(btn), "clicked", G_CALLBACK(evt_key_clear_clicked), page);
	gtk_grid_attach(GTK_GRID(grid), btn, 2, 2, 1, 1);

	label = gtk_label_new(_("Colors"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 3, 1, 1);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	def->col_bg = od_multibutton_new();
	od_multibutton_set_text(OD_MULTIBUTTON(def->col_bg), 0, _("Background..."),    NULL, NULL, GUINT_TO_POINTER(face));
	od_multibutton_set_text(OD_MULTIBUTTON(def->col_bg), 1, _("Reset to Default"), NULL, NULL, GUINT_TO_POINTER(face));
	g_object_set_data(G_OBJECT(def->col_bg), "face", GINT_TO_POINTER(face));
	g_signal_connect(G_OBJECT(def->col_bg), "clicked", G_CALLBACK(evt_col_bg_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), def->col_bg, TRUE, TRUE, 0);
	def->col_fg = od_multibutton_new();
	od_multibutton_set_text(OD_MULTIBUTTON(def->col_fg), 0, _("Foreground..."),    NULL, NULL, GUINT_TO_POINTER(face));
	od_multibutton_set_text(OD_MULTIBUTTON(def->col_fg), 1, _("Reset to Default"), NULL, NULL, GUINT_TO_POINTER(face));
	g_object_set_data(G_OBJECT(def->col_fg), "face", GINT_TO_POINTER(face));
	g_signal_connect(G_OBJECT(def->col_fg), "clicked", G_CALLBACK(evt_col_fg_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), def->col_fg, TRUE, TRUE, 0);
	gtk_grid_attach(GTK_GRID(grid), hbox, 1, 3, 3, 1);

	gtk_container_add(GTK_CONTAINER(def->frame), grid);
}

static void evt_sheet_activate(GtkWidget *wid, guint index, gpointer user)
{
	P_Buttons	*page = user;

	if(index == 0U)
		page->cur_sheet = NULL;
	else
		page->cur_sheet = "Shortcuts";	/* Hardcoded, yes. */
	page->sheet = btn_buttonsheet_get(&page->buttons, page->cur_sheet);
	populate_scwin(page);
}

static GtkWidget * cbt_init(MainInfo *min, gchar **name)
{
	P_Buttons	*page = &the_page;
	const gchar	*mlab[] = { N_("Default"), N_("Shortcuts"), NULL },
			*blab[] = { N_("Copy To"), N_("Copy Colors To"), N_("Swap With"), N_("Clear") },
			*rlab[] = { N_("Add Row..."), N_("Row Width..."), N_("Up"), N_("Down"), N_("Delete Row") };
	GtkWidget	*hbox, *optm, *sep, *label, *frame;
	guint		i;
	gint		expand;

	page->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	page->min  = min;
	page->button = NULL;
	page->cur_sheet = NULL;

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new(_("Sheet"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	optm = gui_build_combo_box(mlab, G_CALLBACK(evt_sheet_activate), page);
	gtk_box_pack_start(GTK_BOX(hbox), optm, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(page->vbox), hbox, FALSE, FALSE, 0);

	page->scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(page->scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(page->vbox), page->scwin, TRUE, TRUE, 0);
	page->shvbox = NULL;

	page->dvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	page->dhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	init_bdef(min, page, &page->def[0], _("Primary"), BTN_PRIMARY);
	gtk_box_pack_start(GTK_BOX(page->dhbox), page->def[0].frame, TRUE, TRUE, 0);
	init_bdef(min, page, &page->def[1], _("Secondary"), BTN_SECONDARY);
	gtk_box_pack_start(GTK_BOX(page->dhbox), page->def[1].frame, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(page->dvbox), page->dhbox, TRUE, TRUE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new(_("Tooltip"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	page->dtooltip = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(page->dtooltip), BTN_TOOLTIP_SIZE - 1);
	g_signal_connect(G_OBJECT(page->dtooltip), "changed", G_CALLBACK(evt_dtooltip_changed), page);
	gtk_box_pack_start(GTK_BOX(hbox), page->dtooltip, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(page->dvbox), hbox, FALSE, FALSE, 0);

	frame = gtk_frame_new(_("Flags"));
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	page->dnarrow = gtk_check_button_new_with_label(_("Narrow?"));
	page->dnarsig = g_signal_connect(G_OBJECT(page->dnarrow), "clicked", G_CALLBACK(evt_dnarrow_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), page->dnarrow, TRUE, TRUE, 0);

	page->dshowtip = gtk_check_button_new_with_label(_("Show Tooltip?"));
	g_signal_connect(G_OBJECT(page->dshowtip), "clicked", G_CALLBACK(evt_dshowtip_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), page->dshowtip, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(frame), hbox);

	gtk_box_pack_start(GTK_BOX(page->dvbox), frame, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(page->vbox), page->dvbox, FALSE, FALSE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	for(i = 0; i < sizeof page->bcmd / sizeof page->bcmd[0]; i++)
	{
		page->bcmd[i] = gtk_button_new_with_label(_(blab[i]));
		g_object_set_data(G_OBJECT(page->bcmd[i]), "index", GINT_TO_POINTER(i));
		g_signal_connect(G_OBJECT(page->bcmd[i]), "clicked", G_CALLBACK(evt_bcmd_clicked), page);
		gtk_box_pack_start(GTK_BOX(hbox), page->bcmd[i], TRUE, TRUE, 0);
	}
	gtk_box_pack_start(GTK_BOX(page->vbox), hbox, FALSE, FALSE, 5);

	sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(page->vbox), sep, FALSE, FALSE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	for(i = 0; i < sizeof page->rcmd / sizeof page->rcmd[0]; i++)
	{
		if(i == 2 || i == 3)
			page->rcmd[i] = gtk_button_new_from_icon_name(i == 2 ? "go-up" : "go-down", GTK_ICON_SIZE_MENU);
		else
			page->rcmd[i] = gtk_button_new_with_label(_(rlab[i]));
		g_object_set_data(G_OBJECT(page->rcmd[i]), "index", GINT_TO_POINTER(i));
		g_signal_connect(G_OBJECT(page->rcmd[i]), "clicked", G_CALLBACK(evt_rcmd_clicked), page);
		expand = (i == 0) || (i == sizeof page->rcmd / sizeof page->rcmd[0] - 1);
		gtk_box_pack_start(GTK_BOX(hbox), page->rcmd[i], expand, TRUE, expand ? 5 : 1);
	}
	gtk_box_pack_start(GTK_BOX(page->vbox), hbox, FALSE, FALSE, 5);

	cfg_tree_level_begin(_("Buttons"));
	cfg_tree_level_append(_("Definitions"), page->vbox);

	gtk_combo_box_set_active(GTK_COMBO_BOX(optm), 0);	/* Do this last, so all handlers can run. */

	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

static void cbt_update(MainInfo *min)
{
	P_Buttons	*page = &the_page;

	page->min	= min;
	page->modified	= FALSE;
	btn_buttoninfo_copy(&page->buttons, &min->cfg.buttons);
	page->sheet = btn_buttonsheet_get(&page->buttons, page->cur_sheet);
	populate_scwin(page);
	set_mode_normal(page);
}

/* ----------------------------------------------------------------------------------------- */

static void cbt_accept(MainInfo *min)
{
	P_Buttons	*page = &the_page;

	if(page->modified)
	{
		btn_buttoninfo_clear(&min->cfg.buttons);
		min->cfg.buttons = page->buttons;
		page->buttons.sheets = NULL;
		cfg_set_flags(CFLG_REBUILD_BOTTOM | CFLG_RESET_KEYBOARD);
		page->modified = FALSE;
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-15 -	Save out the button information to a fun file, using our pseudo-XML. */
static gint cbt_save(MainInfo *min, FILE *out)
{
	btn_buttoninfo_save(min, &min->cfg.buttons, out, NODE);

	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-15 -	Load a bunch of button sheets in. */
static void cbt_load(MainInfo *min, const XmlNode *node)
{
	btn_buttoninfo_load(min, &min->cfg.buttons, node);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-16 -	Hide the button config page. Just unselects any selected button. */
static void cbt_hide(MainInfo *min)
{
	P_Buttons	*page = &the_page;

	if(page->button != NULL)
		set_mode_normal(page);
}

/* ----------------------------------------------------------------------------------------- */

const CfgModule * cbt_describe(MainInfo *min)
{
	static const CfgModule	desc = { NODE, cbt_init, cbt_update, cbt_accept, cbt_save, cbt_load, cbt_hide };

	return &desc;
}
