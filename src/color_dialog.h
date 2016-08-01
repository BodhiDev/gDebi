/*
** 1999-05-02 -	Header for the little color dialog module.
*/

#if !defined COLOR_DIALOG_H
#define	COLOR_DIALOG_H

#include <gdk/gdk.h>

#include "dialog.h"

typedef void	(*ColChangedFunc)(const GdkRGBA *color, gpointer user);

extern gint	cdl_dialog_sync_new_wait(const gchar *label, ColChangedFunc func, const GdkRGBA *initial, gpointer user);

#endif		/* COLOR_DIALOG_H */
