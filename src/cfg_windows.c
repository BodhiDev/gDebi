/*
** 1998-10-15 -	Config page for main window position and size. Neat.
** 2000-02-26 -	This file has been pretty much entierly rewritten, and renamed. It now
**		supports configuring position and size of not only the main window, but
**		also the config and textview windows. Pretty neat.
*/

#include "gentoo.h"

#include "dialog.h"
#include "guiutil.h"
#include "strutil.h"

#include "cfg_module.h"
#include "cfg_windows.h"

#define	NODE	"Windows"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	GtkWidget	*vbox;				/* Config page structures always begin like this. */
	GtkWidget	*content;			/* Content built for us by the window module. */
	GtkWidget	*border[2];			/* Window border spin buttons. */
	GtkWidget	*dpos[3];			/* Dialog position mode radio buttons. */

	MainInfo	*min;
	gboolean	modified;
	WinInfo		*edit;				/* Window info being edited. */
	GtkWindowPosition dialog_pos;
} P_Windows;

static P_Windows	the_page;

/* ----------------------------------------------------------------------------------------- */

static void populate_page(P_Windows *page)
{
	gint	border[2], i;

	if(page->content)
		gtk_widget_destroy(page->content);
	page->content = win_wininfo_build(page->edit, &page->modified);
	gtk_box_pack_start(GTK_BOX(page->vbox), page->content, FALSE, FALSE, 0);
	gtk_widget_show_all(page->content);

	win_borders_get(page->edit, &border[0], &border[1]);
	for(i = 0; i < sizeof border / sizeof *border; i++)
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(page->border[i]), border[i]);
}

/* ----------------------------------------------------------------------------------------- */

static void evt_border_changed(GtkWidget *wid, gpointer user)
{
	P_Windows	*page = user;
	gint		w, h;

	w = gtk_spin_button_get_value(GTK_SPIN_BUTTON(page->border[0]));
	h = gtk_spin_button_get_value(GTK_SPIN_BUTTON(page->border[1]));
	win_borders_set(page->edit, w, h);
	page->modified = TRUE;
}

static void evt_dialog_position_toggled(GtkWidget *wid, gpointer user)
{
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)))
	{
		P_Windows	*page = user;
		gint		pos;

		pos = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "pos"));
		if(pos != page->dialog_pos)
		{
			page->dialog_pos = pos;
			page->modified = TRUE;
		}
	}
}

static GtkWidget * cwn_init(MainInfo *min, gchar **name)
{
	P_Windows	*page = &the_page;
	GtkWidget	*hbox, *frame, *grid, *vbox;
	const gchar	*blab[] = { N_("Horizontal"), N_("Vertical") };
	const gchar	*dlab[] = { N_("Dialog Windows Positioned by Window Manager"), N_("Dialog Windows Follow Mouse"), N_("Dialog Windows Center On Screen") };
	GtkWindowPosition	dpos[] = { GTK_WIN_POS_NONE, GTK_WIN_POS_MOUSE, GTK_WIN_POS_CENTER };
	guint		i;
	GSList		*group = NULL;

	if(name == NULL)
		return NULL;

	*name = _("Windows");

	page->edit    = NULL;

	page->vbox    = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	page->content = NULL;

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	frame = gtk_frame_new(_("Window Borders"));
	grid = gtk_grid_new();
	for(i= 0; i < sizeof blab / sizeof *blab; i++)
	{
		GtkWidget	*label;

		label = gtk_label_new(_(blab[i]));
		gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
		page->border[i] = gtk_spin_button_new_with_range(-32768, 32768, 1);
		gtk_widget_set_hexpand(page->border[i], TRUE);
		gtk_widget_set_halign(page->border[i], GTK_ALIGN_FILL);
		gtk_spin_button_set_digits(GTK_SPIN_BUTTON(page->border[i]), 0);
		g_signal_connect(G_OBJECT(page->border[i]), "value_changed", G_CALLBACK(evt_border_changed), page);
		gtk_grid_attach(GTK_GRID(grid), page->border[i], 1, i, 1, 1);
	}
	gtk_container_add(GTK_CONTAINER(frame), grid);
	gtk_box_pack_start(GTK_BOX(hbox), frame, TRUE, TRUE, 0);

	frame = gtk_frame_new(_("Dialog Positioning"));
	vbox  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	for(i = 0; i < sizeof dlab / sizeof *dlab; i++)
	{
		page->dpos[i] = gtk_radio_button_new_with_label(group, _(dlab[i]));
		g_object_set_data(G_OBJECT(page->dpos[i]), "pos", GINT_TO_POINTER(dpos[i]));
		g_signal_connect(G_OBJECT(page->dpos[i]), "toggled", G_CALLBACK(evt_dialog_position_toggled), page);
		if(dpos[i] == min->cfg.dialogs.pos)
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->dpos[i]), TRUE);
		group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(page->dpos[i]));
		gtk_box_pack_start(GTK_BOX(vbox), page->dpos[i], FALSE, FALSE, 0);
	}
	gtk_container_add(GTK_CONTAINER(frame), vbox);
	gtk_box_pack_start(GTK_BOX(hbox), frame, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(page->vbox), hbox, TRUE, FALSE, 5);

	gtk_widget_show_all(page->vbox);
	return page->vbox;
}

/* ----------------------------------------------------------------------------------------- */

/* 2000-01-30 -	Update possize display. */
static void cwn_update(MainInfo *min)
{
	P_Windows	*page = &the_page;

	page->min	= min;
	page->modified	= FALSE;

	if(page->edit != NULL)
		win_wininfo_destroy(page->edit);
	page->edit = win_wininfo_copy(min->cfg.wininfo);
	page->dialog_pos = min->cfg.dialogs.pos;
	populate_page(page);
}

/* ----------------------------------------------------------------------------------------- */

static void cwn_accept(MainInfo *min)
{
	P_Windows	*page = &the_page;

	if(page->modified)
	{
		win_wininfo_destroy(min->cfg.wininfo);
		min->cfg.wininfo = page->edit;
		min->cfg.dialogs.pos = page->dialog_pos;
		dlg_position_set(min->cfg.dialogs.pos);
		page->edit = NULL;
		page->modified = FALSE;
	}
}

/* ----------------------------------------------------------------------------------------- */

static gboolean cwn_save(MainInfo *min, FILE *out)
{
	win_wininfo_save(min->cfg.wininfo, out);

	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

static void cwn_load(MainInfo *min, const XmlNode *node)
{
	win_wininfo_load(min->cfg.wininfo, node);
}

/* ----------------------------------------------------------------------------------------- */

/* 2000-02-26 -	When the config window hides, we can free our editing copy if we (still) have one. */
static void cwn_hide(MainInfo *min)
{
	P_Windows	*page = &the_page;

	if(page->edit != NULL)
	{
		win_wininfo_destroy(page->edit);
		page->edit = NULL;
	}
}

/* ----------------------------------------------------------------------------------------- */

const CfgModule * cwn_describe(MainInfo *min)
{
	static const CfgModule	desc = { NODE, cwn_init, cwn_update, cwn_accept, cwn_save, cwn_load, cwn_hide };

	return &desc;
}
