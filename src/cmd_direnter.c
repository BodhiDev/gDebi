/*
** 1998-09-02 -	This command is a bit special, actually. It is designed to be used as the
**		double-click action for directories. If run by a double click, it will attempt
**		to enter the directory clicked. Otherwise, it will enter the first selected
**		directory, if any.
** 1999-01-20 -	Added calls to the automount module at the relevant points.
** 1999-03-06 -	Rewritten to comply with new selection architecture. Way shorter.
** 1999-05-07 -	Added support for command argument specifying wanted directory.
** 1999-05-29 -	Removed one character, magically bringing support for symlinks back online. :)
** 1999-06-06 -	Moved parts of dp_enter_dir() here, since its now customary to call DirEnter
**		rather than the dp code directly. Makes history handling easier, too. Implemented
**		a couple of new history commands (DirBackward & DirForward). Real neat.
*/

#include "gentoo.h"
#include "errors.h"
#include "dirhistory.h"
#include "dirpane.h"
#include "fileutil.h"
#include "gfam.h"
#include "guiutil.h"
#include "strutil.h"
#include "userinfo.h"

#include "cmd_parent.h"
#include "cmd_direnter.h"

#define	CMD_ID	"direnter"

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-07 -	Get the name of the first selected row of <dp>. If that is not a directory,
**		report error to user and return NULL.
** 2010-03-04 -	Now returns the URI of the first selected directory, which is a bit more GIO.
*/
static gboolean get_selected_dir(DirPane *dp, gchar *buf, gsize buf_max)
{
	GSList		*slist;
	gboolean	ok = FALSE;

	/* FIXME: This is kind of wasteful. Maybe invent some dp_get_selection_first() function? */
	if((slist = dp_get_selection(dp)) != NULL)
	{
		if(dp_row_get_file_type(dp_get_tree_model(dp), slist->data, TRUE) == G_FILE_TYPE_DIRECTORY)
		{
			GFile	*file;

			if((file = dp_get_file_from_row(dp, slist->data)) != NULL)
			{
				gchar	*uri = g_file_get_uri(file);

				if(uri != NULL)
				{
					ok = (g_strlcpy(buf, uri, buf_max) < buf_max);
					if(ok)
						dp_unselect(dp, slist->data);
					g_free(uri);
				}
				g_object_unref(file);
			}
		}
		dp_free_selection(slist);
	}
	return ok;
}

/* 1999-05-07 -	Rewrote this, now supports a "dir" command argument.
** 1999-06-06 -	Now does all work through the dirhistory module.
*/
gint cmd_direnter(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	const gchar	*dir;
	gchar		uri[URI_MAX] = "";
	guint		index;
	gboolean	ok = FALSE;

	if((dir = car_bareword_get(ca, 0)) == NULL)
		dir = car_keyword_get_value(ca, "dir", NULL);
	if(dir != NULL)
	{
		if(strcmp(dir, "@cwd") == 0)				/* Actual current directory? */
		{
			dir = getcwd(uri, sizeof uri);
			if(dir == NULL)
				dir = usr_get_home();
		}
		else if(sscanf(dir, "@history[%u]", &index) == 1)	/* Access history buffer? */
		{
			dir = dph_dirhistory_get_entry(src->hist, index);
			if(dir == NULL)
				dir = usr_get_home();
		}
		if(dir != NULL)
			ok = g_strlcpy(uri, dir, sizeof uri) < sizeof uri;
	}
	if(!ok)
		ok = get_selected_dir(src, uri, sizeof uri);
	if(!ok)
	{
		g_warning("** cmd_direnter() exhausted known ways to get a directory--aborting");
		return 0;
	}

	fut_interpolate(uri);
	dph_state_save(src);
	if(dp_enter_dir(src, uri))
	{
		/* Annoying hacky code to get GTK+ to resize the pane before we restore
		** the history state; if not resized, vpos setting will likely fail.
		*/
		gtk_widget_queue_resize(GTK_WIDGET(src->view));
		gui_events_flush();
		dph_state_restore(src);
		dp_show_stats(src);
		fam_monitor(src);
		return 1;
	}
	g_warning("** Pane #%d failed to enter '%s'", src->index, uri);

	return 0;
}
