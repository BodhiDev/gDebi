/*
** 1998-07-12 -	Various little helper routines for building/managing GUIs.
** 1999-03-13 -	Adapted for new dialog module.
*/

#include "gentoo.h"

#include <string.h>

#include "dialog.h"
#include "guiutil.h"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	Dialog		*dlg;
	GtkWidget	*vbox;
	GtkWidget	*label;
	GtkWidget	*obj;
	GtkWidget	*bar;
	gboolean	terminate;
} PBar;

typedef struct
{
	GObject	*object;
	gulong	handler;
} SigHandler;

struct GuiHandlerGroup
{
	GList	*handlers;
};

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-26 -	Create a button with a "details" icon on it. This is typically a magnifying
**		class. This button is a graphical replacement for a button labeled "...".
** 2009-04-23 -	Happy ten year anniversary, gui_details_button_new()! W00t! Rewritten to use
**		GTK+ 2.0 stock image, GTK_STOCK_FIND is what we use. It used to be a magnifying
**		glass, but in current GTK+ it's a pair of binoculars. Oh well.
*/
GtkWidget * gui_details_button_new(void)
{
	return gtk_button_new_from_icon_name("edit-find", GTK_ICON_SIZE_MENU);
}

/* ----------------------------------------------------------------------------------------- */

/* 2011-07-24 -	Build a regular entry widget, for use in a dialog (so with default-handling). */
GtkWidget * gui_dialog_entry_new(void)
{
	GtkWidget	*ent;

	ent = gtk_entry_new();
	gtk_entry_set_activates_default(GTK_ENTRY(ent), TRUE);

	return ent;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-07-30 -	Build a group of radio buttons with the given labels.
** 1999-05-15 -	Now also sets the GTK+ object user data to the index of each button.
*/
GSList * gui_radio_group_new(guint num, const gchar **label, GtkWidget *but[])
{
	GSList	*list = NULL;
	guint	i;

	for(i = 0; i < num; i++)
	{
		but[i] = gtk_radio_button_new_with_label(list, _(label[i]));
		list = gtk_radio_button_get_group(GTK_RADIO_BUTTON(but[i]));
		g_object_set_data(G_OBJECT(but[i]), "user", GUINT_TO_POINTER(i));
	}
	return list;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-05 -	This gets called by the dialog module if the user hits the button. */
static void callback_done(gint button, gpointer user)
{
	PBar	*pbar = user;

	pbar->terminate = TRUE;
}

/* 1998-09-05 -	Initialize a user-abortable progress "session". Opens up a dialog window
**		with the given <body> text and <button>, as well as a progress bar widget
**		and a "current object name" indicator (a bare label).
**		Returns an "anchor"; a pointer to some private data. You use this pointer
**		in later calls to identify the session. Progress sessions don't nest!
**		Use calls to gui_progress_update() to make the bar move, and also to learn
**		about any button actions.
*/
gpointer gui_progress_begin(const gchar *body, const gchar *button)
{
	static PBar	pbar;

	pbar.vbox  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	pbar.label = gtk_label_new(body);
	gtk_box_pack_start(GTK_BOX(pbar.vbox), pbar.label, TRUE, TRUE, 0);
	pbar.obj = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(pbar.vbox), pbar.obj, TRUE, TRUE, 0);
	pbar.bar = gtk_progress_bar_new();
	gtk_box_pack_start(GTK_BOX(pbar.vbox), pbar.bar, TRUE, TRUE, 5);
	pbar.terminate = FALSE;

	pbar.dlg = dlg_dialog_async_new(pbar.vbox, _("Progress"), button, callback_done, &pbar);

	return &pbar;
}

/* 1998-09-05 -	Update the progress bar to a new value (which should be between 0.0 and 1.0).
**		Also allows you to change the indication of the current object.
**		Returns TRUE if the user has hit the button in the window, typically indicating
**		that the entire operation should be aborted ASAP.
*/
gboolean gui_progress_update(gpointer anchor, gfloat value, const gchar *obj)
{
	PBar	*pbar = anchor;

	if(!pbar->terminate)
	{
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pbar->bar), value);
		if((obj != NULL) && (pbar->obj != NULL))
			gtk_label_set_text(GTK_LABEL(pbar->obj), obj);
		gui_events_flush();
	}
	return pbar->terminate;
}

/* 1998-09-05 -	Close down the progress session, since the time-consuming operation has been
**		finished.
*/
void gui_progress_end(gpointer anchor)
{
	PBar	*pbar = anchor;

	if(!pbar->terminate)
		dlg_dialog_async_close(pbar->dlg, -1);
	memset(pbar, 0, sizeof *pbar);		/* Makes sure we notice any foul play. */
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-06-22 -	Just a utility function to help the config modules to their thing. This one
**		builds a menu from a NULL-terminated vector of item texts. Very simple, but
**		also very useful. Like so many other good things. :) Each menu item will
**		have the given <func> connected to it, which will be called with <data> as
**		the user data (as usual). Also, to make it possible for the callback to
**		determine which item was selected, each item will have its index (from 0)
**		set as object's user data. Not pretty, but still simple and useful.
*/
GtkWidget * gui_build_menu(const gchar *text[], GCallback func, gpointer user)
{
	GtkWidget	*menu, *item;
	guint		i;

	menu = gtk_menu_new();
	for(i = 0; text[i] != NULL; i++)
	{
		item = gtk_menu_item_new_with_label(_(text[i]));
		if(func != NULL)
		{
			g_object_set_data(G_OBJECT(item), "user", GUINT_TO_POINTER(i));
			g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(func), user);
		}
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		gtk_widget_show(item);
	}
	return menu;
}


/* 2010-05-10 -	Trampoline for the gui_build_combo_box()-constructed widgets. Computes the
**		index that was selected, and hands that to the user-supplied callback.
*/
static void evt_combo_changed(GtkWidget *wid, gpointer user)
{
	const gint	item = gtk_combo_box_get_active(GTK_COMBO_BOX(wid));

	if(item >= 0)
	{
		GCallback	func = g_object_get_data(G_OBJECT(wid), "callback");

		if(func != NULL)
			((void(*)(GtkWidget*, guint, gpointer))func)(wid, item, user);
	}
}

/* 2010-05-09 -	Merry birthday, me! :D This is a replacement for the old gui_build_menu().
**		It builds a text-based GtkComboBox, and calls the given function like so:
**			void callback(GtkWidget *combo, guint index, gpointer user);
**		Simple, and friendly.
*/
GtkWidget * gui_build_combo_box(const gchar *text[], GCallback func, gpointer user)
{
	GtkWidget	*cbox;
	gulong		handler;

	cbox = gtk_combo_box_text_new();
	g_object_set_data(G_OBJECT(cbox), "callback", func);
	for(; *text != NULL; text++)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cbox), _(*text));
	handler = g_signal_connect(G_OBJECT(cbox), "changed", G_CALLBACK(evt_combo_changed), user);
	g_object_set_data(G_OBJECT(cbox), "handler", GINT_TO_POINTER(handler));

	return cbox;
}

/* 2010-11-02 -	Block the signal handler. Since it's a bit encapsulated, a function here is needed. */
void gui_combo_box_set_blocked(GtkWidget *widget, gboolean blocked)
{
	const gulong	handler = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "handler"));

	if(blocked)
		g_signal_handler_block(G_OBJECT(widget), handler);
	else
		g_signal_handler_unblock(G_OBJECT(widget), handler);
}

/* ----------------------------------------------------------------------------------------- */

GuiHandlerGroup * gui_handler_group_new(void)
{
	GuiHandlerGroup	*g;

	g = g_slice_new(GuiHandlerGroup);
	if(g != NULL)
	{
		g->handlers = NULL;
	}
	return g;
}

gulong gui_handler_group_connect(GuiHandlerGroup *g, GObject *obj, const gchar *signal, GCallback cb, gpointer user)
{
	SigHandler	*sh;

	if(g == NULL || obj == NULL || signal == NULL || cb == NULL)
		return 0ul;

	if((sh = g_slice_new(SigHandler)) == NULL)
		return 0ul;
	sh->object = obj;
	sh->handler = g_signal_connect(obj, signal, cb, user);
	/* Prepend, for O(1) performance; order doesn't really matter. */
	g->handlers = g_list_prepend(g->handlers, sh);

	/* Return handler ID, if user code wants to play with it. */
	return sh->handler;
}

void gui_handler_group_block(const GuiHandlerGroup *g)
{
	const GList	*iter;

	if(g == NULL)
		return;
	for(iter = g->handlers; iter != NULL; iter = g_list_next(iter))
	{
		const SigHandler *sh = iter->data;
		g_signal_handler_block(sh->object, sh->handler);
	}
}

void gui_handler_group_unblock(const GuiHandlerGroup *g)
{
	const GList	*iter;

	if(g == NULL)
		return;
	for(iter = g->handlers; iter != NULL; iter = g_list_next(iter))
	{
		const SigHandler *sh = iter->data;
		g_signal_handler_unblock(sh->object, sh->handler);
	}
}

/* ----------------------------------------------------------------------------------------- */

void gui_set_main_title(MainInfo *min, const gchar *title)
{
	gchar	buf[256];

	if(title != NULL && *title != '\0')
	{
		g_snprintf(buf, sizeof buf, "gentoo - %s", title);
		win_window_set_title(min->gui->window, buf);
	}
	else
		win_window_set_title(min->gui->window, "gentoo");
}

/* 2013-06-15 -	Converts a modern RGBA color to an old-school one. */
void gui_color_from_rgba(GdkColor *color, const GdkRGBA *rgba)
{
	color->red = 65535.0f * rgba->red;
	color->green = 65535.0f * rgba->green;
	color->blue = 65535.0f * rgba->blue;
	color->pixel = 0;
}

void gui_rgba_from_color(GdkRGBA *rgba, const GdkColor *color)
{
	rgba->red   = color->red / 65535.;
	rgba->green = color->green / 65535.;
	rgba->blue  = color->blue / 65535.;
	rgba->alpha = 1.0;
}

/* ----------------------------------------------------------------------------------------- */

/* 2008-07-04 -	Flush pending GTK+ events.
** 2010-03-04 -	Comments trimmed, legacy code removed. Just the events, thanks.
*/
void gui_events_flush(void)
{
	while(gtk_events_pending())
		gtk_main_iteration();
}
