/*
** 2000-02-12 -	A Join command, to counter the long-existing (um, but still incomplete)
**		Split command. Features neat drag-and-drop support for ordering the files
**		to be joined.
*/

#include "gentoo.h"

#include <ctype.h>
#include <fcntl.h>

#include "cmd_copy.h"
#include "cmd_delete.h"
#include "dialog.h"
#include "dirpane.h"
#include "errors.h"
#include "fileutil.h"
#include "guiutil.h"
#include "overwrite.h"
#include "progress.h"
#include "sizeutil.h"

#include "cmd_join.h"

#define	CMD_ID	"join"

/* ----------------------------------------------------------------------------------------- */

/* 2011-10-04 -	Happy Cinnamon Bun day. Splices data from <in> into <out>, in discrete chunks so we can report
**		progress. glib's built-in g_output_stream_splice() does it all at once, which freezes gentoo
**		for large files. Not acceptable. This would be a good candidate for multi-threading, in the
**		future. Considering the size of this, it's kind of annoying that glib doesn't have it. :|
*/
static gssize chunked_splice(MainInfo *min, GInputStream *ins, GOutputStream *outs, gsize size, gchar *buf, gsize buf_size, GError **error)
{
	gssize		to_go = size, out_size = 0;

	while(to_go > 0)
	{
		const gssize	chunk = to_go > buf_size ? buf_size : to_go;
		gssize		got, wrote;

		got = g_input_stream_read(ins, buf, chunk, NULL, error);
		if(got != chunk)
			break;
		wrote = g_output_stream_write(outs, buf, chunk, NULL, error);
		if(wrote != chunk)
			break;
		out_size += chunk;
		if(pgs_progress_item_update(min, out_size) != PGS_PROCEED)
			break;
	}
	return out_size;
}

/* 2000-02-12 -	Do the actual joining of the selected files. Returns TRUE on success. */
static gboolean do_join(MainInfo *min, DirPane *src, DirPane *dst, GtkListStore *store, GtkEntry *entry, gsize total_size)
{
	const gchar		*str = gtk_entry_get_text(entry);
	GFile			*out;
	OvwRes			owres;
	gboolean		doit = TRUE;
	GError			*err = NULL;
	GFileOutputStream	*outs;
	gsize			done_size = 0;

	/* Protect against slashes in destination name. No rename-trickery! */
	if(g_utf8_strchr(str, -1, G_DIR_SEPARATOR) != NULL)
	{
		err_set(min, EINVAL, CMD_ID, NULL);
		return FALSE;
	}

	out = dp_get_file_from_name_display(dst, str);
	ovw_overwrite_begin(min, _("\"%s\" Already Exists - Continue With Join?"), 0U);
	owres = ovw_overwrite_unary_file(dst, out);
	switch(owres)
	{
	case OVW_SKIP:
	case OVW_CANCEL:
		doit = FALSE;
		break;
	case OVW_PROCEED:
		break;
	case OVW_PROCEED_FILE:
	case OVW_PROCEED_DIR:
		doit = del_delete_gfile(min, out, FALSE, &err);
		break;
	}
	if(!doit)
	{
		err_set_gerror(min, &err, CMD_ID, out);
		g_object_unref(out);
		return FALSE;
	}

	if((outs = g_file_create(out, G_FILE_CREATE_NONE, NULL, &err)) != NULL)
	{
		GtkTreeIter		iter;
		gssize			put;
		gchar			*buf;
		const gsize		buf_size = cpy_get_buf_size();

		if((buf = g_malloc(buf_size)) != NULL)
		{
			pgs_progress_begin(min, _("Joining..."), PFLG_BYTE_VISIBLE);
			gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
			while(gtk_list_store_iter_is_valid(store, &iter))
			{
				GFile	*in;
				DirRow2	*dr;

				gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 2, &dr, -1);
				if((in = dp_get_file_from_row(src, dr)) != NULL)
				{
					GFileInputStream	*ins;

					if((ins = g_file_read(in, NULL, &err)) != NULL)
					{
						const gsize	size = dp_row_get_size(dp_get_tree_model(src), dr);

						pgs_progress_item_begin(min, dp_row_get_name_display(dp_get_tree_model(src), dr), size);
						put = chunked_splice(min, G_INPUT_STREAM(ins), G_OUTPUT_STREAM(outs), size, buf, buf_size, &err);
						if(put < 0 || !g_input_stream_close(G_INPUT_STREAM(ins), NULL, &err))
							break;
						pgs_progress_item_end(min);
						dp_unselect(src, dr);
						done_size += put;
					}
					g_object_unref(in);
				}
				else
					break;
				if(!gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter))
					break;
			}
			pgs_progress_end(min);
			g_free(buf);
		}
		if(done_size == total_size)
			dp_rescan_post_cmd(dst);
	}
	if(done_size != total_size)
		err_set_gerror(min, &err, CMD_ID, out);

	g_object_unref(out);
	ovw_overwrite_end(min);

	return done_size == total_size;
}

static void evt_dest_changed(GtkEditable *w, gpointer user)
{
	Dialog		*dlg = user;
	const gchar	*str = gtk_entry_get_text(GTK_ENTRY(w));
	gsize		len;

	/* Protect against empty destination name, nicely. */
	len = strlen(str);
	dlg_dialog_set_positive_enabled(dlg, len > 0);
}

gint cmd_join(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	GSList	*sel;
	gint	ret = 1;

	err_clear(min);

	if((sel = dp_get_selection(src)) != NULL)
	{
		GtkTreeModel	*m = dp_get_tree_model(src);
		GSList		*iter;
		gsize		tot = 0, num = 0;

		/* Compute size of all regular files in the selection; ignore 0-sized files. */
		for(iter = sel; iter != NULL; iter = g_slist_next(iter))
		{
			guint64	size = dp_row_get_size(m, iter->data);
			/* Follow symlinks, since we want to allow the joining of a bunch of link targets. */
			if(size > 0 && dp_row_get_file_type(m, iter->data, TRUE) == G_FILE_TYPE_REGULAR)
			{
				tot += size;
				num++;
			}
		}
		
		/* Only open the dialog and proceed with command if there were, in
		** fact, more than one regular file selected.
		*/
		if(num > 1)
		{
			Dialog			*dlg;
			GtkWidget		*scwin, *vbox, *label, *entry, *view;
			GtkListStore		*store;
			GtkCellRenderer		*cr, *sr;
			GtkTreeViewColumn	*vc;
			GtkTreeSelection	*treesel;
			gchar			buf[FILENAME_MAX + 64];
			const gchar		*first = NULL, *tail;
			gunichar		ch;

			vbox  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
			label = gtk_label_new(_("Click and Drag Files to Reorder, Then Click Join."));
			gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

			if(tot > (1 << 10))
			{
				gchar	sbuf[16], bbuf[32];

				sze_put_offset(sbuf, sizeof sbuf, tot, SZE_AUTO, 3, ',');
				sze_put_offset(bbuf, sizeof bbuf, tot, SZE_BYTES, 3, ',');
				g_snprintf(buf, sizeof buf, _("The total size is %s (%s)."), sbuf, bbuf);
			}
			else
				g_snprintf(buf, sizeof buf, _("The total size is %lu bytes."), (unsigned long) tot);
			label = gtk_label_new(buf);
			gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

			scwin = gtk_scrolled_window_new(NULL, NULL);
			gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

			/* Create and initialize list store with selected files and their sizes. */
			store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
			for(iter = sel; iter != NULL; iter = g_slist_next(iter))
			{
				if(dp_row_get_file_type(m, iter->data, TRUE) == G_FILE_TYPE_REGULAR && dp_row_get_size(m, iter->data) > 0)
				{
					GtkTreeIter	ti;
					gchar		bbuf[32];

					sze_put_offset(bbuf, sizeof bbuf, dp_row_get_size(m, iter->data), SZE_BYTES_NO_UNIT, 3, ',');
					gtk_list_store_insert_with_values(store, &ti, -1,
										0, dp_row_get_name_display(m, iter->data),
										1, bbuf,
								       		2, iter->data,
								       		-1);
					if(first == NULL)
						first = dp_row_get_name_display(m, iter->data);
				}
			}

			view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
			cr = gtk_cell_renderer_text_new();
			vc = gtk_tree_view_column_new_with_attributes("(title)", cr, "text", 0, NULL);
			gtk_tree_view_append_column(GTK_TREE_VIEW(view), vc);
			sr = gtk_cell_renderer_text_new();
			g_object_set(G_OBJECT(sr), "xalign", 1.0f, NULL);
			vc = gtk_tree_view_column_new_with_attributes("(title)", sr, "text", 1, NULL);
			gtk_tree_view_append_column(GTK_TREE_VIEW(view), vc);
			gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

			treesel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
			gtk_tree_selection_set_mode(treesel, GTK_SELECTION_NONE);
			gtk_tree_view_set_reorderable(GTK_TREE_VIEW(view), TRUE);
			gtk_widget_set_size_request(view, 0, 256);	/* Get a sensible minmum height. */
			gtk_container_add(GTK_CONTAINER(scwin), view);
			gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);

			label = gtk_label_new(_("Enter Destination File Name"));
			gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 5);
			entry = gui_dialog_entry_new();
			gtk_entry_set_max_length(GTK_ENTRY(entry), FILENAME_MAX - 1);
			/* Get a pointer to the terminating '\0' character. */
			tail = g_utf8_offset_to_pointer(first, g_utf8_strlen(first, -1));
			do
			{
				tail = g_utf8_find_prev_char(first, tail);	/* Step backward. */
				ch = g_utf8_get_char(tail);
			} while(g_unichar_isxdigit(ch) || ch == '.');
			gtk_entry_buffer_set_text(gtk_entry_get_buffer(GTK_ENTRY(entry)), first, g_utf8_pointer_to_offset(first, tail) + 1);
			gtk_editable_set_position(GTK_EDITABLE(entry), -1);
			gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 0);

			dlg = dlg_dialog_sync_new(vbox, _("Join"), _("_Join|_Cancel"));
			g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(evt_dest_changed), dlg);
			gtk_widget_show_all(vbox);
			gtk_widget_grab_focus(entry);
			if(dlg_dialog_sync_wait(dlg) == DLG_POSITIVE)
				ret = do_join(min, src, dst, store, GTK_ENTRY(entry), tot);
			dlg_dialog_sync_destroy(dlg);
		}
		dp_free_selection(sel);
	}
	return ret;
}
