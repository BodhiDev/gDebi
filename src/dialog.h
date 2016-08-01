/*
** 1999-06-19 -	Dialog module header. Lean & mean, hopefully.
*/

#if !defined DIALOG_H
#define	DIALOG_H

#include "config.h"

#include <glib.h>
#include <gtk/gtk.h>

/* ----------------------------------------------------------------------------------------- */

#define	DLG_POSITIVE	(0)

typedef struct Dialog	Dialog;

typedef void (*DlgAsyncFunc)(gint button, gpointer user);

/* ----------------------------------------------------------------------------------------- */

extern void	dlg_group_set(GdkWindow *group_window);
extern void	dlg_main_window_set(GtkWindow *win);
extern void	dlg_position_set(GtkWindowPosition pos);

extern Dialog *	dlg_dialog_sync_new(GtkWidget *body, const gchar *title, const gchar *buttons);
extern Dialog *	dlg_dialog_sync_new_simple(const gchar *body, const gchar *title, const gchar *buttons);
extern gint	dlg_dialog_sync_new_simple_wait(const gchar *body, const gchar *title, const gchar *buttons);
extern void	dlg_dialog_sync_stay_open(Dialog *dlg);
extern gint	dlg_dialog_sync_wait(Dialog *dlg);
extern void	dlg_dialog_sync_close(Dialog *dlg, gint button);
extern void	dlg_dialog_sync_destroy(Dialog *dlg);

extern Dialog *	dlg_dialog_async_new(GtkWidget *body, const gchar *title, const gchar *buttons, DlgAsyncFunc func, gpointer user);
extern Dialog *	dlg_dialog_async_new_simple(const gchar *body, const gchar *title, const gchar *buttons, DlgAsyncFunc func, gpointer user);
extern Dialog *	dlg_dialog_async_new_error(const gchar *body);
extern void	dlg_dialog_async_close(Dialog *dlg, gint button);
extern void	dlg_dialog_async_close_silent(Dialog *dlg);

extern GtkDialog*	dlg_dialog_get_dialog(const Dialog *dlg);
extern void	dlg_dialog_track_entry(Dialog *dlg, GtkWidget *entry);
extern void	dlg_dialog_set_positive_enabled(Dialog *dlg, gboolean enabled);

#endif		/* DIALOG_H */
