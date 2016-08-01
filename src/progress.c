/*
** 1998-09-29 -	This is a Russion space shuttle. It is also the module responsible for
**		reporting various commands' progress to the user, and providing a nice
**		way of aborting commands prematurely.
** 1999-01-30 -	Now cancels operation if ESCape is hit. Pretty convenient.
** 1999-03-05 -	Adapted for new selection management. Also fixes a couple of minor bugs.
** 2000-07-16 -	Operation can now be cancelled by closing the progress window.
*/

#include "gentoo.h"

#include <sys/stat.h>
#include <unistd.h>

#include <gdk/gdkkeysyms.h>

#include "dirpane.h"
#include "fileutil.h"
#include "guiutil.h"
#include "miscutil.h"
#include "sizeutil.h"

#include "progress.h"

/* ----------------------------------------------------------------------------------------- */

static struct {
	guint		level;			/* Current begin/end nesting level. Keep first!! */
	guint32		flags;			/* Flags from original progress_begin() call. */
	GCancellable	*cancel;		/* This is from the future. */
	gboolean	show_byte;		/* Show total bytes progress bar? */
	gboolean	show_item;		/* Show the item bar? */
	gboolean	delayed;		/* Set to 1 during initial display delay. */
	guint		tot_num;		/* Total number of items to process. */
	guint		tot_pos;		/* Index of current item. */
	guint64		byte_tot;		/* Total bytes in operation. */
	guint64		byte_pos;		/* Position, in bytes. */
	gchar		name[4 * FILENAME_MAX];	/* Current filename. */
	off_t		item_size;		/* Size of current item being processed. */
	off_t		item_pos;		/* Position in the item. */
	GTimeVal	time_begin;		/* The time when pgs_progress_begin() was called. */
	guint		delay_time;		/* Delay until display, in microseconds (!). */
	guint		last_secs;		/* Elapsed seconds last time we displayed ETA and stuff. */

	MainInfo	*min;
	GtkWidget	*dlg;
	GtkWidget	*body;
	GtkWidget	*tot_pgs;

	GtkWidget	*byte_pgs;
	GtkWidget	*byte_high;

	GtkWidget	*item;
	GtkWidget	*item_pgs;

	GtkWidget	*eta;		/* Label for elapsed time, speed, and ETA. */
} pgs_info = { 0U };

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-30 -	Count number of top-level selections.
** 1998-10-01 -	Added size support.
*/
static guint count_toplevel_selected(MainInfo *min, guint64 *size)
{
	DirPane	*dp = min->gui->cur_pane;
	GSList	*slist, *iter;
	guint	num = 0;
	guint64	dummy;

	if(size == NULL)
		size = &dummy;

	for(iter = slist = dp_get_selection(dp), *size = 0; iter != NULL; iter = g_slist_next(iter), num++)
		*size += dp_row_get_size(dp_get_tree_model(dp), iter->data);
	dp_free_selection(slist);

	return num;
}

/* 1998-09-30 -	Count number of items recursively from all selections.
** 1998-10-01 -	Added size support.
** 1998-12-20 -	Now uses the fut_dir_size() function for the recursive scanning.
*/
static guint count_recursive_selected(MainInfo *min, guint64 *size)
{
	DirPane	*dp = min->gui->cur_pane;
	GSList	*slist, *iter;
	guint	num = 0;

	if(size != NULL)
		*size = 0U;
	for(iter = slist = dp_get_selection(dp); iter != NULL; iter = g_slist_next(iter), num++)
	{
		if(size)
			*size += dp_row_get_size(dp_get_tree_model(dp), iter->data);
		if(dp_row_get_file_type(dp_get_tree_model(dp), iter->data, TRUE) == G_FILE_TYPE_DIRECTORY)
		{
			FUCount	fu;
			GFile	*dir;

			dir = dp_get_file_from_row(dp, iter->data);
			if(fut_size_gfile(min, dir, size, &fu, NULL))
				num += fu.num_total;
			g_object_unref(dir);
		}
	}
	dp_free_selection(slist);

	return num;
}

static gint evt_delete(GtkWidget *wid, GdkEvent *evt, gpointer user)
{
	g_cancellable_cancel(user);
	return TRUE;
}

/* 1998-09-30 -	A callback for the big "Cancel" button. */
static gint evt_cancel_clicked(GtkWidget *wid, gpointer user)
{
	g_cancellable_cancel(user);
	return TRUE;
}

static gint evt_keypress(GtkWidget *wid, GdkEventKey *evt, gpointer user)
{
	if(evt->keyval == GDK_KEY_Escape)
		g_cancellable_cancel(user);
	return TRUE;
}

/* 1998-09-30 -	Begin a new "session" with progress reporting. */
void pgs_progress_begin(MainInfo *min, const gchar *op_name, guint32 flags)
{
	gchar		size_buf[32], buf[128];
	GtkWidget	*cancel, *hbox, *label;

	pgs_info.min = min;

	if(pgs_info.level == 0)
	{
		if(flags & PFLG_BUSY_MODE)
			flags &= PFLG_BUSY_MODE;	/* Clear any other flag. */

		pgs_info.flags	    = flags;
		pgs_info.show_byte  = (flags & PFLG_BYTE_VISIBLE) ? TRUE : FALSE;
		pgs_info.show_item  = (flags & PFLG_ITEM_VISIBLE) ? TRUE : FALSE;
		pgs_info.delayed    = TRUE;
		pgs_info.delay_time = 250000U;
		pgs_info.byte_pos   = 0;
		pgs_info.last_secs  = 0U;

		if(pgs_info.cancel == NULL)
			pgs_info.cancel = g_cancellable_new();
		else
			g_cancellable_reset(pgs_info.cancel);

		if(flags & PFLG_COUNT_RECURSIVE)
			pgs_info.tot_num = count_recursive_selected(min, &pgs_info.byte_tot);
		else
			pgs_info.tot_num = count_toplevel_selected(min, &pgs_info.byte_tot);

		pgs_info.tot_pos = 0;

		pgs_info.dlg = win_dialog_open(min->gui->window);
		gtk_widget_set_size_request(pgs_info.dlg, 384, -1);
		pgs_info.body = gtk_label_new(op_name);
		g_signal_connect(G_OBJECT(pgs_info.dlg), "delete_event", G_CALLBACK(evt_delete), pgs_info.cancel);
		g_signal_connect(G_OBJECT(pgs_info.dlg), "key_press_event", G_CALLBACK(evt_keypress), pgs_info.cancel);
		gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(pgs_info.dlg))), pgs_info.body, FALSE, FALSE, 0);

		if(pgs_info.show_item)
		{
			pgs_info.item = gtk_label_new("");
			gtk_label_set_xalign(GTK_LABEL(pgs_info.item), 0.f);
			gtk_label_set_yalign(GTK_LABEL(pgs_info.item), 0.5f);
			gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(pgs_info.dlg))), pgs_info.item, FALSE, FALSE, 5);
			hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			pgs_info.item_pgs = gtk_progress_bar_new();
			gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(pgs_info.item_pgs), TRUE);
			gtk_box_pack_start(GTK_BOX(hbox), pgs_info.item_pgs, TRUE, TRUE, 0);
			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pgs_info.item_pgs), 0.f);
			gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(pgs_info.dlg))), hbox, FALSE, FALSE, 0);
		}

		pgs_info.tot_pgs = gtk_progress_bar_new();
		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		if(flags & PFLG_BUSY_MODE)
		{
			gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(pgs_info.tot_pgs), 1.f / 10.f);
			gtk_box_pack_start(GTK_BOX(hbox), pgs_info.tot_pgs, TRUE, TRUE, 0);
		}
		else
		{
			label = gtk_label_new("0");
			gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);
			gtk_box_pack_start(GTK_BOX(hbox), pgs_info.tot_pgs, TRUE, TRUE, 0);
			g_snprintf(buf, sizeof buf, "%d", pgs_info.tot_num);
			label = gtk_label_new(buf);
			gtk_box_pack_end(GTK_BOX(hbox), label, FALSE, FALSE, 5);
			gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(pgs_info.tot_pgs), TRUE);
		}
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pgs_info.tot_pgs), 0.f);
		gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(pgs_info.dlg))), hbox, FALSE, FALSE, 0);

		if(pgs_info.show_byte)
		{
			sze_put_offset(size_buf, sizeof size_buf, pgs_info.byte_tot, SZE_AUTO, 3, ',');
			g_snprintf(buf, sizeof buf, _("Total (%s)"), size_buf);
			label = gtk_label_new(buf);
			gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(pgs_info.dlg))), label, FALSE, FALSE, 0);
			pgs_info.byte_pgs = gtk_progress_bar_new();
			gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(pgs_info.byte_pgs), TRUE);
			gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(pgs_info.dlg))), pgs_info.byte_pgs, TRUE, TRUE, 0);
		}
		if(!(flags & PFLG_BUSY_MODE))
		{
			pgs_info.eta = gtk_label_new("");
			gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(pgs_info.dlg))), pgs_info.eta, FALSE, FALSE, 0);
		}
		else
			pgs_info.eta = NULL;
		cancel = gtk_button_new_with_label(_("Cancel"));
		g_signal_connect(G_OBJECT(cancel), "clicked", G_CALLBACK(evt_cancel_clicked), pgs_info.cancel);
		gtk_dialog_add_action_widget(GTK_DIALOG(pgs_info.dlg), cancel, GTK_RESPONSE_CANCEL);

		g_get_current_time(&pgs_info.time_begin);
	}
	else
		g_warning("pgs_progress_begin() doesn't nest");
	pgs_info.level++;
}

GCancellable * pgs_progress_get_cancellable(void)
{
	return pgs_info.cancel;
}

/* 1998-09-30 -	End a progress-reporting session. Closes down the dialog box. */
void pgs_progress_end(MainInfo *min)
{
	if(pgs_info.level == 1)
	{
		gtk_widget_destroy(pgs_info.dlg);
		pgs_info.dlg = NULL;
	}
	pgs_info.level--;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-30 -	Begin on a new "item"; typically a file, being <size> bytes. */
void pgs_progress_item_begin(MainInfo *min, const gchar *name, off_t size)
{
	if(pgs_info.flags & PFLG_BUSY_MODE)
		gtk_progress_bar_pulse(GTK_PROGRESS_BAR(pgs_info.tot_pgs));
	else
	{
		gchar buf[32];
		g_snprintf(buf, sizeof buf, "%u", pgs_info.tot_pos);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(pgs_info.tot_pgs), buf);
		pgs_info.tot_pos++;
	}
	g_snprintf(pgs_info.name, sizeof pgs_info.name, "%s", name);
	pgs_progress_item_resize(min, size);
}

/* 2012-01-09 -	Set a new size for the current item. This makes sense when moving. */
void pgs_progress_item_resize(MainInfo *min, off_t new_size)
{
	pgs_info.item_size = new_size;

	if(pgs_info.show_item)
	{
		gchar	size_buf[32], buf[FILENAME_MAX + 32];

		sze_put_offset(size_buf, sizeof size_buf, pgs_info.item_size, SZE_AUTO, 3, ',');
		g_snprintf(buf, sizeof buf, "%s (%s)", pgs_info.name, size_buf);
		gtk_label_set_text(GTK_LABEL(pgs_info.item), buf);
	}
	gui_events_flush();
}

/* 2014-03-02 -	Format a bunch of seconds into a more friendly "time" format. */
static void format_eta(gchar *buf, gsize buf_max, guint seconds)
{
	const guint	base[] = { 60, 60, 24, 0 };
	guint		field[sizeof base / sizeof *base], i, conv = seconds;

	for(i = 0; i < sizeof field / sizeof *field; ++i)
	{
		if(base[i])
		{
			field[i] = conv % base[i];
			conv /= base[i];
		}
		else
			field[i] = conv;
	}

	if(seconds < 60 * 60)	/* Enough with MM:SS? */
		g_snprintf(buf, buf_max, "%02u:%02u", field[1], field[0]);
	else if(seconds < 24 * 60 * 60)	/* Enough with HH:MM:SS? */
		g_snprintf(buf, buf_max, "%02u:%02u:%02u", field[2], field[1], field[0]);
	else if(field[3] == 1)		/* FIXME: Perhaps there's better I18N magic to use here for day(s). */
		g_snprintf(buf, buf_max, _("%u day, %02u:%02u:%02u"), field[3], field[2], field[1], field[0]);
	else
		g_snprintf(buf, buf_max, _("%u days, %02u:%02u:%02u"), field[3], field[2], field[1], field[0]);
}

/* 1998-09-30 -	Indicate progress operating on the most recently registered item. Our
**		current position in the item is <pos> bytes.
*/
PgsRes pgs_progress_item_update(MainInfo *min, off_t pos)
{
	GTimeVal	time_now;

	if(pgs_info.dlg != NULL)
	{
		g_get_current_time(&time_now);
		if(pgs_info.delayed)
		{
			const guint	micro = 1E6 * (time_now.tv_sec  - pgs_info.time_begin.tv_sec) +
						      (time_now.tv_usec - pgs_info.time_begin.tv_usec);

			if(micro >= pgs_info.delay_time)
				pgs_info.delayed = FALSE;
		}
		if(!pgs_info.delayed)
		{
			if(pgs_info.eta != NULL)
			{
				const gfloat	secs = msu_diff_timeval(&pgs_info.time_begin, &time_now);
				if((guint) secs != pgs_info.last_secs)
				{
					gchar	buf[64], spdbuf[16], etabuf[32];
					gfloat	spd;
					guint	eta;

					pgs_info.last_secs = secs;
					spd = (pgs_info.byte_pos + pos) / secs;
					eta = (pgs_info.byte_tot - (pgs_info.byte_pos + pos)) / spd;
					format_eta(etabuf, sizeof etabuf, eta);
					sze_put_offset(spdbuf, sizeof spdbuf, spd, SZE_AUTO, 3, ',');
					g_snprintf(buf, sizeof buf, _("Elapsed %02d:%02d  Speed %s/s  ETA %s"),
						pgs_info.last_secs / 60, pgs_info.last_secs % 60, spdbuf,
						etabuf);
					gtk_label_set_text(GTK_LABEL(pgs_info.eta), buf);
				}
			}
			gtk_widget_realize(pgs_info.dlg);
			gdk_window_set_group(gtk_widget_get_window(pgs_info.dlg), gtk_widget_get_window(pgs_info.min->gui->window));
			gtk_widget_show_all(pgs_info.dlg);
		}
		if(pgs_info.show_byte && pgs_info.byte_tot)
		{
			gchar tmp[32];
			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pgs_info.byte_pgs), (gfloat) (pgs_info.byte_pos + pos) / pgs_info.byte_tot);
			sze_put_offset(tmp, sizeof tmp, pgs_info.byte_pos + pos, SZE_AUTO, 3, ',');
			gtk_progress_bar_set_text(GTK_PROGRESS_BAR(pgs_info.byte_pgs), tmp);
		}
		if(pgs_info.show_item && pgs_info.item_size)
		{
			gchar tmp[32];
			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pgs_info.item_pgs), (gfloat) pos / pgs_info.item_size);
			sze_put_offset(tmp, sizeof tmp, pos, SZE_AUTO, 3, ',');
			gtk_progress_bar_set_text(GTK_PROGRESS_BAR(pgs_info.item_pgs), tmp);
		}
		gui_events_flush();
		if(g_cancellable_is_cancelled(pgs_info.cancel))
			return PGS_CANCEL;
	}
	return PGS_PROCEED;
}

/* 1998-09-30 -	Done with an item. */
void pgs_progress_item_end(MainInfo *min)
{
	pgs_info.byte_pos += pgs_info.item_size;

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pgs_info.tot_pgs), (gfloat) pgs_info.tot_pos / pgs_info.tot_num);
	gui_events_flush();
}
