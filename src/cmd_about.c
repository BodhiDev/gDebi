/*
** 1998-09-23 -	No program is complete without an "About" command.
** 1999-03-13 -	Now uses a multi dialog, which allows setting a sensible title. :)
** 1999-03-21 -	Brought the copyright into 1999, and added fun page flipping. :)
** 1999-06-20 -	Adapted for the new dialog module, and made asynchronous for fun.
** 2002-06-07 -	Redid it with a notebook. Cleaner, and required anyway. Heh.
*/

#include "gentoo.h"
#include "dialog.h"
#include "gfam.h"

#include "cmd_about.h"

/* ----------------------------------------------------------------------------------------- */

#include "graphics/icon_gentoo.c"

/* ----------------------------------------------------------------------------------------- */

struct body {
	GtkWidget	*notebook;
	GtkWidget	*page[2];
	guint		index;
	guint		handle;
};

/* ----------------------------------------------------------------------------------------- */

static gint timeout_trigger(gpointer user)
{
	struct body	*body = user;

	gtk_notebook_set_current_page(GTK_NOTEBOOK(body->notebook), body->index ^= 1);
	return TRUE;
}

/* 1999-06-19 -	This gets called as the dialog finally closes. */
static void timeout_remove(gint button, gpointer user)
{
	struct body	*body = user;

	g_source_remove(GPOINTER_TO_UINT(body->handle));
	g_free(body);
}

/* 2008-03-16 -	Returns a label describing the current CHARSET value (I think). */
static GtkWidget * get_charset_label(void)
{
	const gchar	*charset = NULL;
	gchar		buf[1024];

	g_get_charset(&charset);
	g_snprintf(buf, sizeof buf, _("Native CHARSET: \"%s\"."), charset);
	return gtk_label_new(buf);
}

/* 2008-08-09 -	Returns a label describing the current filename encoding. */
static GtkWidget * get_filename_label(void)
{
	const gchar	**enc = NULL;

	g_get_filename_charsets(&enc);
	if(enc != NULL && enc[0] != NULL)
	{
		gchar	buf[1024];

		g_snprintf(buf, sizeof buf, _("Filename encoding: \"%s\"."), enc[0]);
		return gtk_label_new(buf);
	}
	return NULL;
}

#if defined CAIRO_HAS_PNG_FUNCTIONS

struct image_reader_state {
	const unsigned char	*data;
	size_t			to_go;
	size_t			pos;
};

/* 2012-05-13 -	A stream reader for (compiled-in) PNG graphics data. */
static cairo_status_t image_reader(void *closure, unsigned char *data, unsigned int length)
{
	struct image_reader_state	*state = closure;

	if(length <= state->to_go)
	{
		memcpy(data, state->data + state->pos, length);
		state->pos += length;
		state->to_go -= length;
		return CAIRO_STATUS_SUCCESS;
	}
	return CAIRO_STATUS_READ_ERROR;
}

static GtkWidget * load_logo(void)
{
	cairo_surface_t			*surf;
	struct image_reader_state	reader_state = { icon_gentoo_png, sizeof icon_gentoo_png, 0 };
	GtkWidget			*img = NULL;

	if((surf = cairo_image_surface_create_from_png_stream(image_reader, &reader_state)) != NULL)
	{
		GdkPixbuf	*pb = gdk_pixbuf_get_from_surface(surf, 0, 0, cairo_image_surface_get_width(surf), cairo_image_surface_get_height(surf));

		img = gtk_image_new_from_pixbuf(pb);
		g_object_unref(G_OBJECT(pb));
	}
	return img;
}

#endif

gint cmd_about(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	gchar		buf[64], page2[768];
	GtkWidget	*w;
	struct body	*body;

	body = g_malloc(sizeof *body);
	body->index = 0U;

	body->notebook = gtk_notebook_new();
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(body->notebook), FALSE);
	body->page[0] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

#if defined CAIRO_HAS_PNG_FUNCTIONS
	if((w = load_logo()) != NULL)
		gtk_box_pack_start(GTK_BOX(body->page[0]), w, FALSE, FALSE, 20);
#endif

	g_snprintf(buf, sizeof buf, _("Version %s (GTK+ version %d.%d.%d)."), VERSION, gtk_major_version, gtk_minor_version, gtk_micro_version);
	w = gtk_label_new(buf);
	gtk_box_pack_start(GTK_BOX(body->page[0]), w, FALSE, FALSE, 0);
	w = gtk_label_new(_("(c) 1998-2016 by Emil Brink, Obsession Development.\n"));
	gtk_box_pack_start(GTK_BOX(body->page[0]), w, FALSE, FALSE, 0);
	w = gtk_label_new(_("This is free software, and there is ABSOLUTELY NO\n"
			"WARRANTY. Read the file COPYING for more details.\n"));
	gtk_box_pack_start(GTK_BOX(body->page[0]), w, FALSE, FALSE, 0);

	/* Inform user which interface strings are being used. */
#if defined ENABLE_NLS
	w = gtk_label_new(_("NLS: Supported, using built-in English strings."));
#else
	w = gtk_label_new("NLS: Not supported, using built-in English strings.");
#endif
	gtk_box_pack_start(GTK_BOX(body->page[0]), w, FALSE, FALSE, 0);

	w = get_charset_label();
	gtk_box_pack_start(GTK_BOX(body->page[0]), w, FALSE, FALSE, 0);
	w = get_filename_label();
	if(w != NULL)
		gtk_box_pack_start(GTK_BOX(body->page[0]), w, FALSE, FALSE, 0);

	gtk_notebook_append_page(GTK_NOTEBOOK(body->notebook), body->page[0], NULL);
	g_snprintf(page2, sizeof page2, _("The author of gentoo can be reached via Internet\n"
					"e-mail at <a href=\"mailto:%s\">%s</a>; feel free to let\n"
					"me know what you think of this software, give\n"
					"suggestions/bug reports, and so on.\n\n"

					"The latest release of gentoo can always be down-\n"
					"loaded from the official gentoo project homepage at\n"
					"<a href=\"%s\">%s</a>."),
					PACKAGE_BUGREPORT, PACKAGE_BUGREPORT,
					PACKAGE_URL, PACKAGE_URL);
	body->page[1] = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(body->page[1]), page2);
	gtk_notebook_append_page(GTK_NOTEBOOK(body->notebook), body->page[1], NULL);

	body->handle = g_timeout_add(10000, timeout_trigger, body);
	dlg_dialog_async_new(body->notebook, _("About gentoo"), _("_OK (Wait for More)"), timeout_remove, body);

	return 1;
}
