/*
** 1998-05-19 -	A text viewer window. Is it simple? Yep, it worked in under well 100 lines.
** 1998-09-10 -	Sooner or later, all old code gets rewritten, it seems. This one did last
**		a long while, though.
** 1998-09-16 -	Added a command (cmd_viewtext). Sorry about the name, but it feels better.
** 1998-12-15 -	Added a GTK+ widget name to the text widget.
** 1998-12-18 -	Rewrote text reader to use mmap(), and implemented a hex viewer.
** 1999-01-05 -	Extended. Added more buttons (mainly for navigation), and a neat Goto dialog.
** 1999-01-07 -	Added search capabilities. Rather limited and perhaps not uber-smooth to use,
**		but better than nothing IMO.
** 1999-03-02 -	Revamped interface, now sort of semi-opaque. Better.
** 1999-03-13 -	Changed for the new dialog module.
** 1999-12-12 -	Removed manually created vertical scrollbar, put the GtkText widget in a scrolled
**		window instead. Might make it work better with wheelie mice?
*/

#include "gentoo.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gdk/gdkkeysyms.h>

#include "convstream.h"
#include "errors.h"
#include "fileutil.h"
#include "dirpane.h"
#include "dialog.h"
#include "guiutil.h"
#include "strutil.h"
#include "textview.h"

#define	HEX_ROW_SIZE	(32)		/* Must be multiple of four! */
#define	HEX_UNPRINTABLE	('.')

/* ----------------------------------------------------------------------------------------- */

typedef enum { MVE_UP = 0, MVE_DOWN, MVE_TOP, MVE_BOTTOM } MoveID;

typedef struct {			/* Used by the Goto-dialog. */
	Dialog		*dlg;			/* Must be first, for initialization below! */
	GtkWidget	*vbox;
	GtkWidget	*label;
	GtkWidget	*entry;
} DlgGoto;

typedef struct {
	Dialog		*dlg;			/* Must be first, see init of static instance below. */
	GtkWidget	*vbox;
	GtkWidget	*label;
	GtkWidget	*entry;
	GtkWidget	*hbox;
	GtkWidget	*fnocase;
	GtkWidget	*fnolf;
} DlgSrch;

/* As of gentoo 0.9.24, this structure is internal to this module. */
typedef struct {
	MainInfo	*min;			/* Incredibly handy. */
	GtkWidget	*win;
	GtkWidget	*scwin;
	GObject		*buffer;		/* GtkTextBuffer object holding the actual text. */
	GtkWidget	*text;
	GtkWidget	*bhbox;			/* Horizontal box with action buttons. */
	GtkWidget	*blabel;		/* A label that is in the action box. */
	GtkWidget	*bplabel;		/* This label shows file position. */
	guint		search_pos;		/* Offset into text to start next search at. */
	gulong		delete_handler;		/* Handler ID for the delete event. */
	gulong		keypress_handler;	/* Handler ID for the keypress event. */
	ConvStream	*convstream;		/* Used to abstract away encoding issues. */
} TxvInfo;

/* ----------------------------------------------------------------------------------------- */

static DlgGoto	goto_dialog = { NULL };		/* Make sure the 'dlg' member is NULL. */
static DlgSrch	srch_dialog = { NULL };		/* Same here. */

#define	SEARCH_RE_SIZE	(256)			/* Far more than *I* would need... ;^) */

/* Store search parameters between invocations. */
static struct {
	gchar		re[SEARCH_RE_SIZE];
	gboolean	no_case;
	gboolean	no_lf;
} last_search = { "", FALSE, FALSE };

/* ----------------------------------------------------------------------------------------- */

static gboolean	do_search(TxvInfo *txi, const gchar *re_src, gboolean nocase, gboolean nolf);
static void	do_search_repeat(TxvInfo *txi);

/* ----------------------------------------------------------------------------------------- */

static void really_destroy(GtkWidget *wid)
{
	TxvInfo	*txi;

	if((wid != NULL) && ((txi = g_object_get_data(G_OBJECT(wid), "user")) != NULL))
	{
		if(goto_dialog.dlg != NULL)		/* Goto dialog window open? Then close it. */
		{
			dlg_dialog_sync_close(goto_dialog.dlg, -1);
			goto_dialog.dlg = NULL;
		}
		if(srch_dialog.dlg != NULL)		/* Close down search window, if open. */
		{
			dlg_dialog_sync_close(srch_dialog.dlg, -1);
			srch_dialog.dlg = NULL;
		}
		g_object_unref(txi->buffer);
		g_free(txi);
		win_window_close(wid);
	}
}

/* 2012-12-27 -	Helper function to move the cursor to the start of the buffer. This is handy after
**		loading, since all the append()s tend to leave the cursor at the end, while the
**		visible part is the first page. This causes weirdness when moving.
*/
static void cursor_to_start(TxvInfo *txi)
{
	GtkTextIter	iter;

	gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(txi->buffer), &iter);
	gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(txi->buffer), &iter);
}

/* 1998-05-19 -	This simplistic thing causes the textviewing window to close when needed. */
static gint evt_delete(GtkWidget *wid, GdkEvent *evt, gpointer user)
{
	really_destroy(wid);

	return FALSE;
}

/* 2010-07-19 -	Rewritten in a much simpler way. Now, input is expected to be UTF-8, so we don't need to check/convert. */
static void do_text_append(TxvInfo *txi, const gchar *text, gsize length)
{
	GtkTextIter	iter;

	gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(txi->buffer), &iter);
	gtk_text_buffer_insert(GTK_TEXT_BUFFER(txi->buffer), &iter, text, length);
}

/* 1999-03-02 -	Users of this module no longer have access to the GtkText widget, so
**		this function is needed for them to be able to actually display text.
*/
void txv_text_append(GtkWidget *wid, const gchar *text, gsize length)
{
	TxvInfo		*txi;

	if((wid == NULL) || ((txi = g_object_get_data(G_OBJECT(wid), "user")) == NULL) || length < 1)
		return;
	do_text_append(txi, text, length);
}

/* 2008-09-26 -	Append a block of text, and give it the given color too. Spiffy! */
void txv_text_append_with_color(GtkWidget *wid, const gchar *text, gsize length, const GdkColor *color)
{
	TxvInfo		*txi;
	GtkTextIter	start, end;
	gchar		tname[32];
	gint		pos;
	GtkTextTagTable	*ttab;
	GtkTextTag	*tag;

	if(color == NULL)
	{
		txv_text_append(wid, text, length);
		return;
	}
	if((wid == NULL) || ((txi = g_object_get_data(G_OBJECT(wid), "user")) == NULL) || length < 1)
		return;
	pos = gtk_text_buffer_get_char_count(GTK_TEXT_BUFFER(txi->buffer));
	do_text_append(txi, text, length);
	gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(txi->buffer), &start, pos);
	gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(txi->buffer), &end);

	/* Be clever. Build a tag name that depends on the color, to re-use tags. */
	g_snprintf(tname, sizeof tname, "txv-%04x%04x%04x", color->red, color->green, color->blue);
	/* If a tag by this name already exists, re-use it. */
	ttab = gtk_text_buffer_get_tag_table(GTK_TEXT_BUFFER(txi->buffer));
	if((tag = gtk_text_tag_table_lookup(ttab, tname)) == NULL)
		tag = gtk_text_buffer_create_tag(GTK_TEXT_BUFFER(txi->buffer), tname, "foreground-gdk", color, NULL);
	gtk_text_buffer_apply_tag(GTK_TEXT_BUFFER(txi->buffer), tag, &start, &end);
}

static void sink_text(const gchar *text, gsize length, gpointer user)
{
	TxvInfo	*txi = user;

	/* Is the conversion stream trying to signal us an error? */
	if(length == 0)
	{
		gchar	msg[256];
		gsize	len;
		GdkColor red;

		len = g_snprintf(msg, sizeof msg, _("\n** Text conversion from charset '%s' failed, aborting."), text);
		red.red = 0xffff;
		red.green = 0;
		red.blue = 0;
		red.pixel = 0;
		txv_text_append_with_color(txi->win, msg, len, &red);
		return;
	}
	do_text_append(txi, text, length);
}

/* 1998-12-18 -	Rewrote this a couple of times today. Note how this version really doesn't
**		care about the size of its input file; it just reads until reading fails.
**		This makes it possible to look at /proc files, which have size 0.
** 1998-12-20 -	Now uses mmap() if file has a non-zero size. Best of both worlds.
** 1999-02-03 -	Changed prototype slightly, since it got public. Added the <use_mmap> flag,
**		since we don't want to read config options at this level.
** 1999-02-20 -	Now checks the return value of mmap() correctly (I assumed it returned
**		NULL on failure, don't know how I got that idea). Also now falls back on
**		using normal file I/O if mmap() fails, which can happen on certain filesystems.
** 1999-03-02 -	New interface, renamed.
** 1999-04-06 -	Added the <buf_size> argument, which is used when mmap() isn't.
** 2010-02-28 -	Ported to GIO, mmap support removed (sadly).
*/
gint txv_text_load(GtkWidget *wid, GFile *file, gsize buf_size, const gchar *encoding, GError **err)
{
	TxvInfo			*txi;
	GFileInputStream	*is;

	if((wid == NULL) || ((txi = g_object_get_data(G_OBJECT(wid), "user")) == NULL) || (file == NULL))
		return 0;

	txv_set_label_from_file(wid, file);

	err_clear(txi->min);
	if((is = g_file_read(file, NULL, err)) != NULL)
	{
		gchar		*buf;
		gssize		got;
		ConvStream	*cs;

		buf = g_malloc(buf_size);
		cs = convstream_new(encoding, 32 << 10, sink_text, txi);
		while((got = g_input_stream_read(G_INPUT_STREAM(is), buf, buf_size, NULL, err)) > 0)
		{
			convstream_source(cs, buf, got);
		}
		convstream_destroy(cs);
		g_free(buf);
		g_object_unref(is);
		cursor_to_start(txi);
	}
	return *err == NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-12-18 -	Build "groups" of actual hex data, from <data> and <bytes> bytes on. */
static void build_hex_group(const guchar *data, gchar *group, gsize bytes)
{
	const static gchar	*hex = "0123456789ABCDEF";
	guint8			here;
	gsize			i;

	for(i = 0; i < bytes; i++)
	{
		if(i && !(i & 3))
			group++;
		here = (guint8) *data++;
		*group++ = hex[here >> 4];
		*group++ = hex[here & 15];
	}
}

/* 1998-12-18 -	Build text representation of stuff found at <data>, <bytes> bytes worth. */
static void build_hex_text(const guchar *data, gchar *text, gsize bytes)
{
	gsize	i;

	for(i = 0; i < bytes; i++, data++)
		*text++ = isprint((guchar) *data) ? *data : HEX_UNPRINTABLE;
}

/* 1998-12-18 -	Add contents of file <name> into GtkText widget <wid>, as a hex dump.
** 1999-02-03 -	New prototype, now public.
** 1999-02-20 -	Fixed use of mmap(), and added fall-back to ordinary read() I/O if
**		the wanted file couldn't be mapped. This might be suboptimal for huge
**		files...
** 1999-03-02 -	New interface, renamed.
** 1999-04-06 -	Cleaned up. Since the data-to-ASCII-hex conversion is only done on a small
**		number of bytes at a time, it was grossly inefficient to load the entire file.
**		Now we do plenty of small read()s instead, which I'm sure is slower, but at
**		least doesn't waste memory.
** 2010-02-28 -	Ported to use GIO.
*/
gint txv_text_load_hex(GtkWidget *win, GFile *file, GError **err)
{
	gchar		line[8 + 1 + (HEX_ROW_SIZE / 4) * 9 + 1 + HEX_ROW_SIZE + 1 + 1],
			group[9 * (HEX_ROW_SIZE / 4) + 1], text[HEX_ROW_SIZE + 1];
	TxvInfo		*txi;
	GFileInputStream *is;
	GtkTextTag	*tag;

	if((win == NULL) || ((txi = g_object_get_data(G_OBJECT(win), "user")) == NULL) || (file == NULL))
		return 0;

	txv_set_label_from_file(win, file);

	memset(group, ' ', sizeof group - 1);
	group[sizeof group - 1] = '\0';
	memset(text, ' ', sizeof text - 1);
	text[sizeof text - 1]   = '\0';

	err_clear(txi->min);
	if((is = g_file_read(file, NULL, err)) != NULL)
	{
		guchar		buf[HEX_ROW_SIZE];
		GtkTextIter	iter;
		gsize		offset = 0, chunk, len;

		gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(txi->buffer), &iter);
		while((chunk = g_input_stream_read(G_INPUT_STREAM(is), buf, sizeof buf, NULL, err)) > 0)
		{
			if(chunk < sizeof buf)
			{
				memset(group, ' ', sizeof group - 1);
				memset(text, ' ', sizeof text - 1);
			}
			build_hex_group(buf, group, chunk);
			build_hex_text(buf, text, chunk);
			len = g_snprintf(line, sizeof line, "%08lX %s %s\n", (unsigned long) offset, group, text);
			gtk_text_buffer_insert(GTK_TEXT_BUFFER(txi->buffer), &iter, line, len);
			offset += chunk;
		}
		g_object_unref(is);
		cursor_to_start(txi);
	}

	if((tag = gtk_text_tag_new("fixed")) != NULL)
	{
		GtkTextIter	start, end;
		GtkTextTagTable	*tt;

		g_object_set(G_OBJECT(tag), "family", "Monospace", NULL);
		tt = gtk_text_buffer_get_tag_table(GTK_TEXT_BUFFER(txi->buffer));
		gtk_text_tag_table_add(tt, tag);
		gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(txi->buffer), &start);
		gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(txi->buffer), &end);
		gtk_text_buffer_apply_tag(GTK_TEXT_BUFFER(txi->buffer), tag, &start, &end);
		g_object_unref(G_OBJECT(tag));
	}
	return *err == NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-01-07 -	Reset the search, so that the next one starts at the beginning. */
static void reset_search(TxvInfo *txi)
{
	if(txi != NULL)
	{
		GtkTextIter	iter;

		gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(txi->buffer), &iter);
		gtk_text_buffer_select_range(GTK_TEXT_BUFFER(txi->buffer), &iter, &iter);
		txi->search_pos = 0;
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-02-23 -	An attempt to support ESCape for closing down the text viewer. Shouldn't
**		be too harmful. I hope.
*/
static gint evt_keypress(GtkWidget *wid, GdkEventKey *evt, gpointer user)
{
	TxvInfo	*txi = g_object_get_data(G_OBJECT(wid), "user");

	if(evt->keyval == GDK_KEY_Escape || evt->keyval == GDK_KEY_Left)
		really_destroy(wid);
	else if(evt->keyval == GDK_KEY_End)
	{
		GtkAdjustment	*adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(txi->scwin));
		gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
	}
	else if(evt->keyval == GDK_KEY_F3)
		do_search_repeat(txi);
	else
		return FALSE;
	return TRUE;
}

/* 1999-01-07 -	This handler gets called when the vertical range (scrollbar) changes. This
**		should probably also include the number of lines available, but that is a
**		rather fuzzy quantity, so I'll just leave that for now.
*/
static gint evt_pos_changed(GtkAdjustment *adj, gpointer user)
{
	TxvInfo		*txi = user;
	gchar		buf[64];
	gfloat		p;
	GtkTextIter	iter;
	gint		bx, by;
	const float	value = gtk_adjustment_get_value(adj);
	const float	upper = gtk_adjustment_get_upper(adj);
	const float	page_size = gtk_adjustment_get_page_size(adj);

	if(upper - page_size > 0.0f)
	{
		p = 100 * (value / (upper - page_size));
		if(p > 100.0)
			p = 100.0;
	}
	else
		p = 0.0f;

	gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(txi->text), GTK_TEXT_WINDOW_WIDGET, 0, 0, &bx, &by);
	gtk_text_view_get_line_at_y(GTK_TEXT_VIEW(txi->text), &iter, by, NULL);
	by = gtk_text_iter_get_line(&iter) + 1;
	g_snprintf(buf, sizeof buf, _("Line %d (%.0f%%)"), by, p);
	gtk_label_set_text(GTK_LABEL(txi->bplabel), buf);

	return TRUE;
}

/* 1999-01-05 -	User clicked on one of the four basic movement buttons. Determine which, and
**		make the text move accordingly.
*/
static gint evt_move_clicked(GtkWidget *wid, gpointer user)
{
	TxvInfo		*txi = user;
	GtkTextIter	iter;
	gint		bx, by;

	gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(txi->text), GTK_TEXT_WINDOW_WIDGET, 0, 0, &bx, &by);
	gtk_text_view_get_line_at_y(GTK_TEXT_VIEW(txi->text), &iter, by, NULL);

	switch((MoveID) GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "user")))
	{
		case MVE_UP:
			gtk_text_view_backward_display_line(GTK_TEXT_VIEW(txi->text), &iter);
			gtk_text_view_backward_display_line_start(GTK_TEXT_VIEW(txi->text), &iter);
			gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(txi->text), &iter, 0.0, TRUE, 0.0, 0.0);
			break;
		case MVE_DOWN:
			gtk_text_view_forward_display_line(GTK_TEXT_VIEW(txi->text), &iter);
			gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(txi->text), &iter, 0.0, TRUE, 0.0, 0.0);
			break;
		case MVE_TOP:
			gtk_text_buffer_get_start_iter(GTK_TEXT_BUFFER(txi->buffer), &iter);
			gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(txi->text), &iter, 0.0, TRUE, 0.0, 0.0);
			reset_search(txi);
			break;
		case MVE_BOTTOM:
			gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(txi->buffer), &iter);
			gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(txi->text), &iter, 0.0, TRUE, 0.0, 1.0);
			reset_search(txi);
			break;
	}
	return TRUE;
}

/* 1999-01-05 -	User hit the "Goto..." button. Pop up a dialog asking for destination. */
static void evt_goto_clicked(GtkWidget *wid, gpointer user)
{
	TxvInfo		*txi = user;
	GtkAdjustment	*adj;

	goto_dialog.vbox  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	goto_dialog.label = gtk_label_new(_("Enter Line Number or Percentage:"));
	gtk_box_pack_start(GTK_BOX(goto_dialog.vbox), goto_dialog.label, FALSE, FALSE, 0);
	goto_dialog.entry = gui_dialog_entry_new();
	gtk_box_pack_start(GTK_BOX(goto_dialog.vbox), goto_dialog.entry, FALSE, FALSE, 0);

	gtk_widget_show_all(goto_dialog.vbox);
	goto_dialog.dlg = dlg_dialog_sync_new(goto_dialog.vbox, _("Goto"), NULL);
	gtk_widget_grab_focus(goto_dialog.entry);
	if((dlg_dialog_sync_wait(goto_dialog.dlg) == DLG_POSITIVE) &&
	   (adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(txi->scwin))) != NULL)
	{
		const gchar	*txt;
		gchar		unit;
		gint		value, line;
		GtkTextIter	iter;

		txt = gtk_entry_get_text(GTK_ENTRY(goto_dialog.entry));
		if(sscanf(txt, "0x%x", &value) == 1)			/* Hexadecimal offset? */
			gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(txi->buffer), &iter, value);
		else if(sscanf(txt, "%d%c", &value, &unit) == 2)
		{
			/* Kind of overly clever this part, perhaps. */
			switch(unit)
			{
			case '%':
				line = (gtk_text_buffer_get_line_count(GTK_TEXT_BUFFER(txi->buffer)) * value) / 100;
				gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(txi->buffer), &iter, line);
				break;
			case 'M':
			case 'm':
				value <<= 10;	/* Yes, only 10, 10 more below. */
				/* Fall-through, re-use get_iter call. */
			case 'K':
			case 'k':
				value <<= 10;
				gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(txi->buffer), &iter, value);
				break;
			}
		}
		else if(sscanf(txt, "%d", &value) == 1)			/* Line number? */
			gtk_text_buffer_get_iter_at_line(GTK_TEXT_BUFFER(txi->buffer), &iter, value);
		gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(txi->text), &iter, 0.0, TRUE, 0.0, 0.0);
	}
	if(goto_dialog.dlg != NULL)
	{
		dlg_dialog_sync_destroy(goto_dialog.dlg);
		goto_dialog.dlg = NULL;		/* Indicate that the dialog is no longer open. */
	}
}

static gint evt_close_clicked(GtkWidget *wid, gpointer user)
{
	TxvInfo		*txi = user;
	GtkWidget	*win;

	if(txi != NULL)
	{
		win = txi->win;
		really_destroy(win);
	}
	return FALSE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-01-07 -	Scroll the text so that the character at offset <pos> from the beginning
**		becomes visible. This search disregards wrapped lines, for reasons of
**		programmer sanity preservation. This obviously sucks, but I really don't
**		want to dig *that* deep into the GtkText widget. There is an easy way to
**		accomplish this that eliminates this problem: use a insert/delete pair.
**		However, that causes the widget to *scroll* to the required position. Yawn.
** 2008-03-09 -	Then came GTK+, TextIters, and life became simpler in some ways.
*/
static void show_text(TxvInfo *txi, gint pos)
{
	GtkTextIter	iter;

	gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(txi->buffer), &iter, pos);
	gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(txi->text), &iter, 0.0, TRUE, 0.0, 0.0);
}

/* 1999-01-07 -	Do the actual search, using <re_src> as the regular expression source. This
**		should really begin with '(', end with ')', and don't contain any non-escaped
**		parentheses within!
** 2008-03-15 -	Ported to use glib's GRegex data structures, for fun.
*/
static gboolean do_search(TxvInfo *txi, const gchar *re_src, gboolean nocase, gboolean nolf)
{
	GRegex		*re;
	GError		*err = NULL;
	gchar		*text;
	gint		found = FALSE, cflags = G_REGEX_EXTENDED | G_REGEX_MULTILINE;

	if(nocase)
		cflags |= G_REGEX_CASELESS;
	if(nolf)
		cflags |= G_REGEX_DOTALL;

	if((re = g_regex_new(re_src, cflags, G_REGEX_MATCH_NOTBOL, &err)) != NULL)
	{
		GtkTextIter	start, end;

		gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(txi->buffer), &start, txi->search_pos);
		gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(txi->buffer), &end);
		if((text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(txi->buffer), &start, &end, TRUE)) != NULL)
		{
			GMatchInfo	*match = NULL;

			if(g_regex_match(re, text, 0, &match))
			{
				gint	startpos, endpos;

				if(g_match_info_fetch_pos(match, 0, &startpos, &endpos))
				{
					show_text(txi, txi->search_pos + startpos);
					gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(txi->buffer), &start, txi->search_pos + startpos);
					gtk_text_buffer_get_iter_at_offset(GTK_TEXT_BUFFER(txi->buffer), &end, txi->search_pos + endpos);
					gtk_text_buffer_select_range(GTK_TEXT_BUFFER(txi->buffer), &start, &end);
					txi->search_pos += endpos;
				}
				found = TRUE;
			}
			g_match_info_free(match);
			g_free(text);
		}
		g_regex_unref(re);
	}
	if(err != NULL)
	{
		dlg_dialog_async_new_error(err->message);
		g_error_free(err);
	}
	return found;
}

/* 1999-01-07 -	User hit the "Search" button, so let's pop up something to request parameters. */
static void evt_search_clicked(GtkWidget *wid, gpointer user)
{
	TxvInfo	*txi = user;

	srch_dialog.vbox  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	srch_dialog.label = gtk_label_new(_("Enter Text to Search For (RE)"));
	gtk_box_pack_start(GTK_BOX(srch_dialog.vbox), srch_dialog.label, FALSE, FALSE, 0);
	srch_dialog.entry = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(srch_dialog.entry), SEARCH_RE_SIZE - 1);
	gtk_entry_set_text(GTK_ENTRY(srch_dialog.entry), last_search.re);
	gtk_editable_select_region(GTK_EDITABLE(srch_dialog.entry), 0, -1);
	gtk_box_pack_start(GTK_BOX(srch_dialog.vbox), srch_dialog.entry, FALSE, FALSE, 0);
	srch_dialog.hbox  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	srch_dialog.fnocase = gtk_check_button_new_with_label(_("Ignore Case?"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(srch_dialog.fnocase), last_search.no_case);
	gtk_box_pack_start(GTK_BOX(srch_dialog.hbox), srch_dialog.fnocase, TRUE, TRUE, 0);
	srch_dialog.fnolf = gtk_check_button_new_with_label(_("Don't Span Newlines?"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(srch_dialog.fnolf), last_search.no_lf);
	gtk_box_pack_start(GTK_BOX(srch_dialog.hbox), srch_dialog.fnolf, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(srch_dialog.vbox), srch_dialog.hbox, FALSE, FALSE, 0);

	gtk_widget_show_all(srch_dialog.vbox);
	srch_dialog.dlg = dlg_dialog_sync_new(srch_dialog.vbox, _("Search"), NULL);
	gtk_widget_grab_focus(srch_dialog.entry);
	if(dlg_dialog_sync_wait(srch_dialog.dlg) == DLG_POSITIVE)
	{
		const gchar	*txt;
		GString	*tmp;

		txt = gtk_entry_get_text(GTK_ENTRY(srch_dialog.entry));
		g_strlcpy(last_search.re, txt, sizeof last_search.re);
		last_search.no_case = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(srch_dialog.fnocase));
		last_search.no_lf   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(srch_dialog.fnolf));
		if(txt != NULL && (tmp = g_string_new("(")) != NULL)
		{
			for(; *txt; txt++)		/* Quote any parentheses in user's string. */
			{
				if(*txt == '(')
					g_string_append(tmp, "\\(");
				else if(*txt == ')')
					g_string_append(tmp, "\\)");
				else
					g_string_append_c(tmp, *txt);
			}
			g_string_append_c(tmp, ')');
			do_search(txi, tmp->str, last_search.no_case, last_search.no_lf);
			g_string_free(tmp, TRUE);
		}
	}
	if(srch_dialog.dlg != NULL)
	{
		dlg_dialog_sync_destroy(srch_dialog.dlg);
		srch_dialog.dlg = NULL;		/* Indicate that the dialog is no longer open. */
	}
}

static void do_search_repeat(TxvInfo *txi)
{
	if(last_search.re[0] != '\0')
	{
		if(!do_search(txi, last_search.re, last_search.no_case, last_search.no_lf) && txi->search_pos > 0)
		{
			reset_search(txi);
			do_search(txi, last_search.re, last_search.no_case, last_search.no_lf);
		}
	}
	else
		evt_search_clicked(NULL, txi);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-10 -	A complete rewrite of the textview widget creation stuff. Now supports just creating
**		the necessary widgetry, and then returning it to the caller for custom manipulation
**		elsewhere. Very useful when capturing output of commands. Consistency is nice.
** 1998-09-11 -	Added the <with_save> boolean, which when TRUE enables a "Save" button.
** 1999-01-05 -	Removed the <with_save> boolean, added loads of other fun buttons that are always
**		there instead. :)
** 1999-02-03 -	Added the <label> argument, which will be set if non-NULL.
** 1999-03-02 -	Redid interface.
*/
GtkWidget * txv_open(MainInfo *min, const gchar *label)
{
	GtkWidget	*vbox, *btn, *bhbox;
	TxvInfo		*txi;
	GtkAccelGroup	*accel;

	txi = g_malloc(sizeof *txi);

	txi->min = min;
	txi->search_pos = 0;		/* Start searching from the beginning. */
	txi->delete_handler = 0u;
	txi->keypress_handler = 0u;

	txi->win = win_window_open(min->cfg.wininfo, WIN_TEXTVIEW);
	g_object_set_data(G_OBJECT(txi->win), "user", txi);
	txv_connect_delete(txi->win, NULL, NULL);
	txv_connect_keypress(txi->win, NULL, NULL);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	txi->buffer = G_OBJECT(gtk_text_buffer_new(NULL));
	txi->text = gtk_text_view_new_with_buffer(GTK_TEXT_BUFFER(txi->buffer));
	gtk_text_view_set_editable(GTK_TEXT_VIEW(txi->text), FALSE);
	gtk_widget_set_name(txi->text, "txvText");

	txi->scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(txi->scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	g_signal_connect_after(G_OBJECT(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(txi->scwin))),
				"value_changed", G_CALLBACK(evt_pos_changed), txi);
	gtk_container_add(GTK_CONTAINER(txi->scwin), txi->text);

	gtk_box_pack_start(GTK_BOX(vbox), txi->scwin, TRUE, TRUE, 0);

	txi->bhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if(label != NULL)
		txi->blabel = gtk_label_new(label);
	else
		txi->blabel = gtk_label_new("");
	gtk_label_set_ellipsize(GTK_LABEL(txi->blabel), PANGO_ELLIPSIZE_MIDDLE);
	gtk_label_set_xalign(GTK_LABEL(txi->blabel), 0.f);
	gtk_box_pack_start(GTK_BOX(txi->bhbox), txi->blabel, TRUE, TRUE, 10);

	txi->bplabel = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(txi->bplabel), 1.0f);
	gtk_box_pack_start(GTK_BOX(txi->bhbox), txi->bplabel, FALSE, FALSE, 0);

	accel = gtk_accel_group_new();

	btn = gtk_button_new_from_icon_name("go-up", GTK_ICON_SIZE_MENU);
	g_object_set_data(G_OBJECT(btn), "user", GINT_TO_POINTER(MVE_UP));
	g_signal_connect(G_OBJECT(btn), "clicked", G_CALLBACK(evt_move_clicked), txi);
	gtk_box_pack_start(GTK_BOX(txi->bhbox), btn, FALSE, FALSE, 0);
	btn = gtk_button_new_from_icon_name("go-down", GTK_ICON_SIZE_MENU);
	g_object_set_data(G_OBJECT(btn), "user", GINT_TO_POINTER(MVE_DOWN));
	g_signal_connect(G_OBJECT(btn), "clicked", G_CALLBACK(evt_move_clicked), txi);
	gtk_box_pack_start(GTK_BOX(txi->bhbox), btn, FALSE, FALSE, 0);
	btn = gtk_button_new_from_icon_name("go-top", GTK_ICON_SIZE_MENU);
	g_object_set_data(G_OBJECT(btn), "user", GINT_TO_POINTER(MVE_TOP));
	g_signal_connect(G_OBJECT(btn), "clicked", G_CALLBACK(evt_move_clicked), txi);
	gtk_widget_add_accelerator(btn, "clicked", accel, GDK_KEY_Home, 0, 0);
	gtk_box_pack_start(GTK_BOX(txi->bhbox), btn, FALSE, FALSE, 0);
	btn = gtk_button_new_from_icon_name("go-bottom", GTK_ICON_SIZE_MENU);
	g_object_set_data(G_OBJECT(btn), "user", GINT_TO_POINTER(MVE_BOTTOM));
	g_signal_connect(G_OBJECT(btn), "clicked", G_CALLBACK(evt_move_clicked), txi);
	gtk_widget_add_accelerator(btn, "clicked", accel, GDK_KEY_End, 0, 0);
	gtk_box_pack_start(GTK_BOX(txi->bhbox), btn, FALSE, FALSE, 0);

	bhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	btn = gtk_button_new_from_icon_name("go-jump", GTK_ICON_SIZE_MENU);
	g_signal_connect(G_OBJECT(btn), "clicked", G_CALLBACK(evt_goto_clicked), txi);
	gtk_box_pack_start(GTK_BOX(bhbox), btn, FALSE, FALSE, 0);

	btn = gui_details_button_new();
	gtk_widget_add_accelerator(btn, "clicked", accel, GDK_KEY_F, GDK_CONTROL_MASK, 0);
	g_signal_connect(G_OBJECT(btn), "clicked", G_CALLBACK(evt_search_clicked), txi);
	gtk_box_pack_start(GTK_BOX(bhbox), btn, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(txi->bhbox), bhbox, FALSE, FALSE, 5);

	btn = gtk_button_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);
	g_signal_connect(G_OBJECT(btn), "clicked", G_CALLBACK(evt_close_clicked), txi);
	gtk_box_pack_start(GTK_BOX(txi->bhbox), btn, FALSE, TRUE, 0);
	gtk_widget_set_sensitive(txi->bhbox, FALSE);
	gtk_box_pack_start(GTK_BOX(vbox), txi->bhbox, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(txi->win), vbox);
	gtk_window_add_accel_group(GTK_WINDOW(txi->win), accel);

	/* Now that all is built, initialize position-displaying label. */
	evt_pos_changed(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(txi->scwin)), txi);

	gtk_widget_show_all(vbox);
	gtk_widget_grab_focus(txi->text);

	return txi->win;
}

/* 1999-12-23 -	Show the textviewing widget. Just a wrapper, isolating the use of the window
**		utility module from the point of view of users of the textview. OK?
*/
void txv_show(GtkWidget *textviewer)
{
	win_window_show(textviewer);
}

/* 2010-03-13 -	Factored this one out of txv_connect_delete() and txv_connect_keypress, below. */
static gulong connect_handler(TxvInfo *txi, gulong *handler, const gchar *signame, GCallback func, GCallback default_func, gpointer user)
{
	if(*handler != 0u)
	{
		g_signal_handler_disconnect(G_OBJECT(txi->win), *handler);
		*handler = 0;
	}
	if(func == NULL)		/* Reset internal handler? */
	{
		func = default_func;
		user = txi->win;
	}
	*handler = g_signal_connect(G_OBJECT(txi->win), signame, func, user);
	return *handler;
}

/* 2010-03-13 -	Replace the module's own default delete handler with a custom handler.
**		Note that in order to not leak memory, any custom handler must call
**		txv_close() itself to clean up after the view.
*/
gulong txv_connect_delete(GtkWidget *wid, GCallback func, gpointer user)
{
	TxvInfo	*txi;

	if((wid == NULL) || ((txi = g_object_get_data(G_OBJECT(wid), "user")) == NULL))
		return 0;
	return connect_handler(txi, &txi->delete_handler, "delete_event", func, G_CALLBACK(evt_delete), user);
}

/* 1999-03-02 -	Install a keypress handler on the textviewing window. This cannot be
**		done directly by the module user, since we have an internal handler
**		and they don't seem to chain properly... Odd.
*/
gulong txv_connect_keypress(GtkWidget *wid, GCallback func, gpointer user)
{
	TxvInfo	*txi;

	if((wid == NULL) || ((txi = g_object_get_data(G_OBJECT(wid), "user")) == NULL))
		return 0;
	return connect_handler(txi, &txi->keypress_handler, "key_press_event", func, G_CALLBACK(evt_keypress), user);
}

/* 1999-01-05 -	Set the label. This is kind of redundant, but true to the original Opus look. :)
** 1999-03-02 -	New interface.
*/
void txv_set_label(GtkWidget *wid, const gchar *text)
{
	TxvInfo	*txi;

	if(wid != NULL && (txi = g_object_get_data(G_OBJECT(wid), "user")) != NULL && text != NULL)
	{
		win_window_set_title(wid, text);
		gtk_label_set_text(GTK_LABEL(txi->blabel), text);
	}
}

/* 2009-05-27 -	Set the label of the view, based on a filename input. This needs encoding-conversion.
** 2010-02-28 -	Converted to GIO.
*/
void txv_set_label_from_file(GtkWidget *wid, GFile *file)
{
	gchar	*dn;

	if((dn = g_file_get_parse_name(file)) != NULL)
	{
		txv_set_label(wid, dn);
		g_free(dn);
	}
}

/* 1999-03-02 -	Enable the text widget for viewing. Called once all insertions are done. */
void txv_enable(GtkWidget *wid)
{
	TxvInfo	*txi;

	if((wid != NULL) && ((txi = g_object_get_data(G_OBJECT(wid), "user")) != NULL))
	{
		gtk_widget_show(wid);	/* Just in case. */
/*		gtk_adjustment_set_value(GTK_ADJUSTMENT(GTK_TEXT(txi->text)->vadj), 0.0f);*/
		gtk_widget_set_sensitive(txi->bhbox, TRUE);
	}
	else
		fprintf(stderr, "TEXTVIEW: Couldn't enable\n");
}

/* 1999-02-23 -	Close down a text viewer. Handy for use from cmdgrab.
** 1999-03-02 -	New interface.
*/
void txv_close(GtkWidget *wid)
{
	TxvInfo	*txi;

	if(wid != NULL && (txi = g_object_get_data(G_OBJECT(wid), "user")) != NULL)
		really_destroy(txi->win);		/* This makes the TxvInfo go away. */
}
