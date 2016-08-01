/*
** 1999-05-02 -	A little color dialog module. Really just a convenience wrapper around
**		the GTK+ GtkColorSelection widget. Saves a few uppercase keystrokes.
** 1999-06-19 -	Adapted for the new dialog module.
*/

#include "gentoo.h"

#include "dialog.h"
#include "color_dialog.h"

typedef struct {
	GtkWidget	*chooser;		/* The core widget. */
	ColChangedFunc	func;
	gpointer	user;
} ColDlg;

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-02 -	This gets called as the user changes color controls in dialog. Call callback. */
static void evt_color_activated(GtkWidget *wid, GdkRGBA *color, gpointer user)
{
	ColDlg	*dlg = user;

	if(dlg->func != NULL)
		dlg->func(color, dlg->user);
}

gint cdl_dialog_sync_new_wait(const gchar *label, ColChangedFunc func, const GdkRGBA *initial, gpointer user)
{
	static ColDlg	dlg;
	Dialog		*d;
	gint		ret = -1;

	dlg.func = func;
	dlg.user = user;

	dlg.chooser = gtk_color_chooser_widget_new();
	g_signal_connect(G_OBJECT(dlg.chooser), "color_activated", G_CALLBACK(evt_color_activated), &dlg);
	if(initial != NULL)
		gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dlg.chooser), initial);
	d = dlg_dialog_sync_new(dlg.chooser, label ? label : _("Edit Color"), NULL);
	ret = dlg_dialog_sync_wait(d);
	if(ret == DLG_POSITIVE && dlg.func != NULL)
	{
		GdkRGBA 	color;

		gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dlg.chooser), &color);
		dlg.func(&color, dlg.user);
	}
	dlg_dialog_sync_destroy(d);

	return ret;
}
