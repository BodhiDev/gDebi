/*
** 1998-05-23 -	A big fat error handling module. There are *so* many possible errors,
**		I think we really must get a grip on that situation. This is it.
**		As far as grips go, I consider this to be pretty loose. :(
*/

#include "gentoo.h"

#include <ctype.h>
#include <stdarg.h>
#include <string.h>

#include "dirpane.h"
#include "dialog.h"
#include "guiutil.h"
#include "errors.h"

/* ----------------------------------------------------------------------------------------- */

/* Description of the error. This is in UTF-8. */
static gchar	error_desc[1024] = "";

/* ----------------------------------------------------------------------------------------- */

void err_clear(MainInfo *min)
{
	error_desc[0] = '\0';
}

/* 1999-01-21 -	Format an arbitrary string and print it on the status (error) line.
** UGLY   UGLY	Uses a sneaky little loop to ensure that the text is displayed *NOW*.
*/
void err_printf(MainInfo *min, const gchar *fmt, ...)
{
	va_list	al;

	va_start(al, fmt);
	vsnprintf(error_desc, sizeof error_desc, fmt, al);
	va_end(al);
	err_show(min);
	gui_events_flush();
}

/* 1998-05-30 -	Added special treatment of code == -1; this means that there is no
**		errno-compatible error information available, and the entire thing
**		is then suppressed.
** 2010-03-07 -	Assumes filenames are displayable, i.e. in UTF-8.
*/
gint err_set(MainInfo *min, gint code, const gchar *source, const gchar *obj)
{
	if(source != NULL)
	{
		if(code >= 0)
		{
			const gchar	*desc;

			desc = g_strerror(code);
			if(obj != NULL)
				g_snprintf(error_desc, sizeof error_desc, _("Couldn't %s \"%s\": %s (code %d)"), source, obj, desc, code);
			else
				g_snprintf(error_desc, sizeof error_desc, _("Couldn't %s \"%s\" (code %d)"), source, obj, code);
		}
		else
		{
			if(obj != NULL)
				g_snprintf(error_desc, sizeof error_desc, _("Couldn't %s \"%s\""), source, obj);
			else
				g_snprintf(error_desc, sizeof error_desc, _("Couldn't %s"), source);
		}
	}
	return 1;
}

/* 2009-08-06 -	Sets the error report from a GError, which is what most GIO calls will generate. This frees the GError, too. The file is not freed! */
void err_set_gerror(MainInfo *min, GError **err, const gchar *source, const GFile *file)
{
	if(err != NULL && *err != NULL)
	{
		/* If a file was supplied, extract the name and include in the displayed message. */
		if(file != NULL)
		{
			gchar	*pn = g_file_get_parse_name((GFile *) file);

			g_snprintf(error_desc, sizeof error_desc, _("%s (%s)"), (*err)->message, pn);
			g_free(pn);
		}
		else
			g_snprintf(error_desc, sizeof error_desc, _("%s"), (*err)->message);
		g_error_free(*err);
		*err = NULL;
	}
}

void err_show(MainInfo *min)
{
	if(error_desc[0] != '\0')
	{
		if(min->cfg.errors.display == ERR_DISPLAY_STATUSBAR)
		{
			gtk_label_set_text(GTK_LABEL(min->gui->top), error_desc);
			gui_events_flush();
		}
		else if(min->cfg.errors.display == ERR_DISPLAY_TITLEBAR)
			gui_set_main_title(min, error_desc);
		else if(min->cfg.errors.display == ERR_DISPLAY_DIALOG)
			dlg_dialog_async_new_simple(error_desc, "Error", "OK", NULL, NULL);
		if(min->cfg.errors.beep)
			gdk_beep();
	}
	else
		dp_show_stats(min->gui->cur_pane);
}
