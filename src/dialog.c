/*
** 1999-03-13 -	A (new) module for dealing with user dialogs. The old one was written during
**		my first week of GTK+ programming (back in May 1998, if you really care), and
**		that had started to show a little too much. Hopefully, this is cleaner.
** 1999-06-19 -	Since the initial cleanliness quickly wore down due to (I guess) bad design,
**		I did a complete rewrite and redesign. This new shiny version features clear
**		separation of dialogs into synchronous and asynchronous versions. The former
**		are way (way) more useful.
** 2002-02-08 -	Added window grouping. Very nice, keeps the number of icons in Window Maker
**		down. :)
*/

#include "gentoo.h"

#include <stdio.h>

#include <gdk/gdkkeysyms.h>

#include "dialog.h"
#include "window.h"

struct Dialog {
	GtkWidget	*dlg;		/* The GtkDialog widget used to display the dialog. */
	gint		last_button;
	gint		button;
	DlgAsyncFunc	func;		/* Used in asynchronous dialogs only. */
	gpointer	user;		/* This, too. */
	gboolean	stay_open;	/* This, on the other hand, only for synchronous ones. */
};

static struct {
	GtkWindow	*main_window;
	GdkWindow	*group_window;
	GtkWindowPosition window_pos;
} the_state;

#define	DLG_BUTTON_SIZE		(32)	/* Max length of a single button label. */

/* ----------------------------------------------------------------------------------------- */

/* 2002-02-08 -	Set the module's internal group pointer. All dialogs have their windows
**		set as being part of the group defined by that window. Very handy.
*/
void dlg_group_set(GdkWindow *group_window)
{
	the_state.group_window = group_window;
}

void dlg_position_set(GtkWindowPosition pos)
{
	the_state.window_pos = pos;
}

/* 2009-03-25 -	Sets the main window, all dialogs are then made transient to this window.
**		I think this replaces the "group" concept used with GDK windows.
*/
void dlg_main_window_set(GtkWindow *win)
{
	the_state.main_window = win;
}

/* ----------------------------------------------------------------------------------------- */

/* 2004-11-22 -	Add buttons by splitting <buttons> on vertical bar (pipe) character. Far simpler
 *		than when using GTK+ 1.2, keyboard acceleration now done by GtkDialog.
*/
static gint add_buttons(GtkWidget *dlg, const char *buttons)
{
	gchar	label[DLG_BUTTON_SIZE], *ptr;
	gint	response = 0;

	while(*buttons)
	{
		for(ptr = label; (ptr - label) < (sizeof label - 1) && *buttons && *buttons != '|';)
			*ptr++ = *buttons++;
		*ptr = '\0';
		if(*buttons == '|')
			buttons++;
		gtk_dialog_add_button(GTK_DIALOG(dlg), label, response++);
	}
	return response - 1;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-06-19 -	Close dialog down in response to some event, and report it as if button <button> was clicked. */
static void sync_close(Dialog *dlg, gint button)
{
	dlg->button = button;
	if(!dlg->stay_open)
		gtk_widget_hide(dlg->dlg);
}

/* 2004-11-22 -	Response-handler for synchronous dialog. */
static void evt_sync_response(GtkWidget *wid, gint response, void *user)
{
	sync_close(user, response >= 0 ? response : -1);
}

/* 2004-11-22 -	Keyboard handler, so we can cancel through Escape. */
static gboolean evt_sync_key_pressed(GtkWidget *wid, GdkEventKey *evt, gpointer user)
{
	Dialog	*dlg = user;

	if(evt->keyval == GDK_KEY_Escape)
		gtk_dialog_response(GTK_DIALOG(dlg->dlg), dlg->last_button);
	else
		return FALSE;
	return TRUE;
}

/* 1999-06-19 -	Create a new, synchronous dialog box. Creates and initializes the auxilary data as well as the
**		actual dialog window, with action buttons and stuff. Note that this window, when displayed by
**		dlg_dialog_sync_wait(), will be modal, and hang around (at least in memory) until you call
**		dlg_dialog_sync_destroy().
*/
Dialog * dlg_dialog_sync_new(GtkWidget *body, const gchar *title, const gchar *buttons)
{
	Dialog		*dlg;

	if(buttons == NULL)
		buttons = _("_OK|_Cancel");

	dlg = g_malloc(sizeof *dlg);
	dlg->dlg = win_dialog_open(GTK_WIDGET(the_state.main_window));
	gtk_window_set_position(GTK_WINDOW(dlg->dlg), the_state.window_pos);
	g_signal_connect(G_OBJECT(dlg->dlg), "key_press_event", G_CALLBACK(evt_sync_key_pressed), dlg);
	g_signal_connect(G_OBJECT(dlg->dlg), "response", G_CALLBACK(evt_sync_response), dlg);
	dlg->stay_open = FALSE;
	if(title != NULL)
		win_window_set_title(GTK_WIDGET(dlg->dlg), title);

	if(body != NULL)
	{
		GtkWidget	*vbox;

		vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
		gtk_box_pack_start(GTK_BOX(vbox), body, TRUE, TRUE, 0);
		gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg->dlg))), vbox, TRUE, TRUE, 0);
	}
	dlg->last_button = add_buttons(dlg->dlg, buttons);
	gtk_dialog_set_default_response(GTK_DIALOG(dlg->dlg), 0);

	return dlg;
}

/* 1999-06-19 -	Create a synchronous dialog whose body is a plain text label. Extremely sugary. */
Dialog * dlg_dialog_sync_new_simple(const gchar *body, const gchar *title, const gchar *buttons)
{
	return dlg_dialog_sync_new(gtk_label_new(body), title, buttons);
}

/* 1999-06-19 -	This (bulkily-named) function is very handy for simple confirm dialogs. */
gint dlg_dialog_sync_new_simple_wait(const gchar *body, const gchar *title, const gchar *buttons)
{
	Dialog	*dlg;
	gint	ret;

	dlg = dlg_dialog_sync_new_simple(body, title, buttons);
	ret = dlg_dialog_sync_wait(dlg);
	dlg_dialog_sync_destroy(dlg);

	return ret;
}

/* 2002-08-20 -	Make the dialog stay open until destroyed. Used only by the generic command module,
**		but on the other hand that module is kind of important.
*/
void dlg_dialog_sync_stay_open(Dialog *dlg)
{
	dlg->stay_open = TRUE;
}

/* 1999-06-19 -	Display a previously created dialog, and wait for the user to click one of its buttons. Then
**		return the index (counting from zero which is the leftmost button) of the button that was
**		clicked.
*/
gint dlg_dialog_sync_wait(Dialog *dlg)
{
	if(dlg != NULL)
	{
		gtk_widget_show_all(dlg->dlg);
		gtk_dialog_run(GTK_DIALOG(dlg->dlg));
		return dlg->button;
	}
	return -1;
}

/* 1999-06-19 -	Close a synchronous dialog as if <button> was clicked. */
void dlg_dialog_sync_close(Dialog *dlg, gint button)
{
	if(dlg != NULL)
		sync_close(dlg, button);
}

/* 1999-06-19 -	Destroy a synchronous dialog. Don't expect to be able to access body widgets after calling this. */
void dlg_dialog_sync_destroy(Dialog *dlg)
{
	gtk_widget_destroy(dlg->dlg);

	g_free(dlg);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-06-19 -	Execute the close-down policy of the asynchronous dialog. */
static void async_close(Dialog *dlg, gint button)
{
	dlg->button = button;
	if((dlg->button > -2 ) && (dlg->func != NULL))
		dlg->func(dlg->button, dlg->user);
	gtk_widget_destroy(dlg->dlg);
	g_free(dlg);
}

/* 1999-06-19 -	User pressed a key in an asynchronous dialog. */
static gboolean evt_async_key_pressed(GtkWidget *wid, GdkEventKey *evt, gpointer user)
{
	Dialog	*dlg = user;

	if(evt->keyval == GDK_KEY_Escape)
	{
		async_close(dlg, dlg->last_button);
		return TRUE;
	}
	return FALSE;
}

/* 1999-06-19 -	User clicked one of the action buttons in an asynchronous dialog. */
static void evt_async_response(GtkWidget *wid, gint response, gpointer user)
{
	async_close(user, response > 0 ? response : -1);
}

/* 1999-06-19 -	Create, and immediately display, an asynchronous dialog. The supplied callback will be called
**		as the user clicks a button (or closes the window). At that point, the dialog (and any user-
**		supplied body widgets) are still around.
*/
Dialog * dlg_dialog_async_new(GtkWidget *body, const gchar *title, const gchar *buttons, DlgAsyncFunc func, gpointer user)
{
	Dialog	*dlg;
	GtkBox	*carea;

	if(buttons == NULL)
		buttons = _("_OK|_Cancel");

	dlg = g_malloc(sizeof *dlg);
	dlg->func = func;
	dlg->user = user;
	dlg->dlg = win_dialog_open(GTK_WIDGET(the_state.main_window));
	gtk_window_set_position(&GTK_DIALOG(dlg->dlg)->window, the_state.window_pos);
	g_signal_connect(G_OBJECT(dlg->dlg), "key_press_event", G_CALLBACK(evt_async_key_pressed), dlg);
	g_signal_connect(G_OBJECT(dlg->dlg), "response", G_CALLBACK(evt_async_response), dlg);
	if(title != NULL)
		win_window_set_title(GTK_WIDGET(&GTK_DIALOG(dlg->dlg)->window), title);
	carea = GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg->dlg)));
	gtk_container_set_border_width(GTK_CONTAINER(carea), 5);
	gtk_box_pack_start(carea, body, TRUE, TRUE, 0);
	dlg->last_button = add_buttons(dlg->dlg, buttons);
	gtk_widget_show_all(dlg->dlg);

	return dlg;
}

/* 1999-06-19 -	A simple sugary wrapper for text-only dialogs. */
Dialog * dlg_dialog_async_new_simple(const gchar *body, const gchar *title, const gchar *buttons, DlgAsyncFunc func, gpointer user)
{
	return dlg_dialog_async_new(gtk_label_new(body), title, buttons, func, user);
}

/* 1999-06-19 -	Provide a simple error reporting dialog. */
Dialog * dlg_dialog_async_new_error(const gchar *body)
{
	return dlg_dialog_async_new_simple(body, _("Error"), _("OK"), NULL, NULL);
}

/* 1999-06-19 -	Close an asynchronous dialog, as if button <button> was clicked. */
void dlg_dialog_async_close(Dialog *dlg, gint button)
{
	async_close(dlg, button);
}

/* 1999-06-19 -	Close an asynchronous dialog, but do not call the user's callback. */
void dlg_dialog_async_close_silent(Dialog *dlg)
{
	async_close(dlg, -2);
}

/* ----------------------------------------------------------------------------------------- */

/* 2011-09-28 -	Return the GtkDialog, which can be useful sometimes. */
GtkDialog * dlg_dialog_get_dialog(const Dialog *dlg)
{
	return dlg != NULL ? GTK_DIALOG(dlg->dlg) : NULL;
}

static void evt_entry_changed(GtkWidget *entry, gpointer user)
{
	const gchar	*text = gtk_entry_get_text(GTK_ENTRY(entry));

	dlg_dialog_set_positive_enabled(user, *text != '\0');
}

/* 2010-06-13 -	Make the dialog track the state of a GtkEntry, and only enable the "OK" button if entry is non-empty. */
void dlg_dialog_track_entry(Dialog *dlg, GtkWidget *entry)
{
	if(dlg == NULL)
		return;
	g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(evt_entry_changed), dlg);
	/* Make sure it's synchronized initially, too. */
	evt_entry_changed(entry, dlg);
}

/* 2010-09-05 -	Happy birthday, me! Enable/disable the "positive" (=OK) button on the fly. */
void dlg_dialog_set_positive_enabled(Dialog *dlg, gboolean enabled)
{
	if(dlg == NULL)
		return;
	gtk_dialog_set_response_sensitive(GTK_DIALOG(dlg->dlg), DLG_POSITIVE, enabled);
}
