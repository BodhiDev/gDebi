/*
** 1998-09-26 -	This module deals with grabbing (i.e. redirecting) a sub-process' output.
**		Very useful when running external commands. This code was extracted from the
**		remains of the old (bloated) command module. If the interface seems rather
**		un-intuitive, that is probably because it's a mess. :^)
** 1999-03-02 -	Cleaned up interface with textviewing module, geting rid (I hope) of some
**		nasty races having to do with closing it down in mid-capture.
** 1999-03-08 -	Did some changes due to seemingly different semantics in GTK+ 1.2.0. This
**		requires handling of GDK_INPUT_EXCEPTION input condition (um, perhaps it
**		did in 1.0.6 too, but it worked even if I didn't care).
** 2008-05-16 -	Took a leap into the future, and ported to GIOChannels.
** 2010-03-11 -	Sliced away lots of code, parent uses glib to get pipes set up, we just grab.
*/

#include "gentoo.h"

#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include <gdk/gdkkeysyms.h>

#include "guiutil.h"
#include "textview.h"

#include "cmdgrab.h"

/* ----------------------------------------------------------------------------------------- */

#define	GRAB_CHUNK	(512)		/* We read this many bytes on each "input available" callback. */

typedef struct {
	MainInfo	*min;
	GPid		child;
	GIOChannel	*c_stdout, *c_stderr;	/* Channels for stdout and stderr from child. */
	gint		fd_out, fd_err;		/* File descriptors for output & error channels. */
	gint		tag_out, tag_err;	/* GTK+ input tags for those channels. */
	gulong		evt_del;		/* Delete event handler. */
	GdkColor	stderr_color;		/* Color used for stderr. */
	GtkWidget	*txv;			/* Text viewing window. */
} GrabInfo;

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-26 -	This gets called when the user tries to delete (i.e., close) the output
**		window. We now need to disconnect the input grabbers, kill the child,
**		free the grabinfo, and close the window. We don't do need to do much of
**		that, though; killing the children generates a last callback-call, and
**		so we can reuse the close-down code already there!
*/
static gint evt_grab_deleted(GtkWidget *wid, GdkEvent *evt, gpointer user)
{
	GrabInfo	*gri = user;

	txv_close(gri->txv);
	kill(gri->child, SIGTERM);
	gri->txv = NULL;		/* This signals the I/O handler that the viewer is closed. */

	return FALSE;
}

/* 1999-02-23 -	User just pressed a key in the grab window. If Escape, close down. */
static gint evt_grab_keypress(GtkWidget *wid, GdkEventKey *event, gpointer user)
{
	GrabInfo	*gri;

	if(event->keyval == GDK_KEY_Escape)
	{
		if((gri = g_object_get_data(G_OBJECT(wid), "gri")) != NULL)
		{
			kill(gri->child, SIGTERM);
			gri->txv = NULL;
		}
		txv_close(wid);
	}
	return FALSE;
}

/* 1998-09-26 -	Here's a callback for the gtk_input_add() stuff. This gets called when there's
**		something to read from our child process, on either its stdout or stderr pipes.
**		We read it out (in small chunks) and use a GtkText widget to display the stuff.
**		This feels a lot more stable than the previous implementation, by the way.
** 1998-12-15 -	Now shows the window as the first output appears. Makes the window invisible
**		for commands that don't cause output. Note the manual destruction for that case.
** 1999-04-22 -	Added quick fix for weird condition: we get called with GDK_INPUT_READ although
**		there is nothing more to read. Added explicit grab termination via recursion.
** 2008-05-16 -	Ported to GLib 2.0 and GIOChannels.
*/
static gboolean grab_callback(GIOChannel *channel, GIOCondition cond, gpointer data)
{
	GrabInfo	*gri = data;

	if(cond & G_IO_IN)
	{
		gchar	*line;
		gsize	linelen = 0u;

		while(g_io_channel_read_line(channel, &line, &linelen, NULL, NULL) == G_IO_STATUS_NORMAL)
		{
			if(gri->txv != NULL)
			{
				txv_show(gri->txv);
				if(channel == gri->c_stdout)
					txv_text_append(gri->txv, line, linelen);
				else
					txv_text_append_with_color(gri->txv, line, linelen, &gri->stderr_color);
				gui_events_flush();
			}
			g_free(line);
		}
	}
	if((cond & G_IO_ERR) || (cond & G_IO_HUP))
	{
		if(channel == gri->c_stdout)
		{
			g_source_remove(gri->tag_out);
			g_io_channel_shutdown(gri->c_stdout, FALSE, NULL);
			g_io_channel_unref(gri->c_stdout);
			gri->c_stdout = NULL;
			close(gri->fd_out);
			gri->fd_out = 0;
		}
		else if(channel == gri->c_stderr)
		{
			g_source_remove(gri->tag_err);
			g_io_channel_shutdown(gri->c_stderr, FALSE, NULL);
			g_io_channel_unref(gri->c_stderr);
			gri->c_stderr = NULL;
			close(gri->fd_err);
			gri->fd_err = 0;
		}
		if(gri->c_stdout == NULL && gri->c_stderr == NULL)
		{
			if(gri->txv != NULL)			/* Window still open? */
			{
				txv_connect_delete(gri->txv, NULL, NULL);
				txv_connect_keypress(gri->txv, NULL, NULL);
				txv_enable(gri->txv);
				if(!gtk_widget_get_realized(gri->txv))
					gtk_widget_destroy(gri->txv);
			}
			g_free(gri);
		}
	}
	return TRUE;
}

/* 1998-09-26 -	Set up two GTK+ input listeners, one on <fd_out> and one on <fd_err>. Is
**		cool (?) enough to share a single output window between these two channels.
*/
gboolean cgr_grab_output(MainInfo *min, const gchar *prog, GPid child, gint fd_out, gint fd_err)
{
	gchar		buf[MAXNAMLEN + 32];
	GrabInfo	*gri;

	gri = g_malloc(sizeof *gri);
	gri->min = min;
	gri->child = child;
	gri->fd_out = fd_out;
	/* TODO: This color should be GUI-settable, of course. Later ... */
	gri->stderr_color.red = 0xfefeu;
	gri->stderr_color.green = 0x1e1eu;
	gri->stderr_color.blue = 0x1e1eu;
	if((gri->c_stdout = g_io_channel_unix_new(fd_out)) != NULL)
	{
		/*g_io_channel_set_buffered(gri->stdout, FALSE);*/
		g_io_channel_set_buffer_size(gri->c_stdout, GRAB_CHUNK);
		gri->fd_err = fd_err;
		if((gri->c_stderr = g_io_channel_unix_new(fd_err)) != NULL)
		{
			if((gri->txv = txv_open(min, NULL)) != NULL)
			{
				g_snprintf(buf, sizeof buf, _("Output of %s (pid %d)"), prog, (gint) child);
				txv_set_label(gri->txv, buf);
				g_object_set_data(G_OBJECT(gri->txv), "gri", gri);
				txv_connect_delete(gri->txv, G_CALLBACK(evt_grab_deleted), gri);
				txv_connect_keypress(gri->txv, G_CALLBACK(evt_grab_keypress), gri);
				gri->tag_out = g_io_add_watch(gri->c_stdout, G_IO_IN | G_IO_PRI | G_IO_HUP, grab_callback, gri);
				gri->tag_err = g_io_add_watch(gri->c_stderr, G_IO_IN | G_IO_PRI | G_IO_HUP, grab_callback, gri);

				return TRUE;
			}
		}
	}
	g_free(gri);

	return FALSE;
}
