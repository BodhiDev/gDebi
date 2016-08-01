/*
** 1998-07-05 -	The Split command, handy for, er, splitting files into parts. Mucho GUI.
** 1999-03-07 -	Altered for the new selection/generic handler. Hum, I really should finish
**		this command off some day...
** 1999-06-19 -	Adapted for the new dialog module. Sheesh, I really should finish this command
**		some day!
*/

#include "gentoo.h"

#include <fcntl.h>

#include "cmd_delete.h"
#include "dialog.h"
#include "dirpane.h"
#include "errors.h"
#include "fileutil.h"
#include "guiutil.h"
#include "overwrite.h"
#include "sizeutil.h"
#include "strutil.h"

#include "cmd_generic.h"
#include "cmd_split.h"

#define	CMD_ID	"split"

#define	NFORMAT_LIMIT	(MAXNAMLEN + 16)
#define	SPLIT_CHUNK	(1 << 18)

/* ----------------------------------------------------------------------------------------- */

#define	VALUE_SIZE	16

typedef struct
{
	/* Basic parameters, extracted from UI. */
	guint64		file_size;
	guint64		part_size;
	guint64		base;
	guint64		step;
	guint64		last;
	guint64		num;

	/* Parameters made visible through a dictionary, for interpolation. */
	GHashTable	*parameters;
	gchar		v_format[16];
	gchar		v_index[VALUE_SIZE];
	gchar		v_base[VALUE_SIZE];
	gchar		v_step[VALUE_SIZE];
	gchar		v_last[VALUE_SIZE];
	gchar		v_num[VALUE_SIZE];
	gchar		v_offset[VALUE_SIZE];
} NameGenerator;

typedef struct {
	GtkWidget	*vbox;		/* This really is required. */
	GtkWidget	*label;		/* Tells the user what's up. */
	GtkWidget	*mode;		/* Option menu for selecting mode of split. */
	GtkWidget	*mvbox;		/* Sub-vbox for the mode-specific widgets. */
	GtkWidget	*nhbox;		/* Hbox for the naming issue. */
	GtkWidget	*nformat;	/* Entry widget for name formatter. */
	GtkWidget	*nfill;		/* Check box for zero-fill of number in format. */
	GtkAdjustment	*npadj;		/* Adjustment for precision. */
	GtkWidget	*nprec;		/* Scale widget for number precision. */
	GtkAdjustment	*nbadj;		/* Adjustment for index base. */
	GtkWidget	*nbase;		/* Spin widget for index base. */
	GtkAdjustment	*nsadj;		/* Adjustment for index step. */
	GtkWidget	*nstep;		/* Spin widget for index step. */

	GtkWidget	*nbook;		/* Notebook with mode-settings. */

	GtkWidget	*sssize;	/* Combo holding the wanted size. */
	GtkWidget	*snframe;	/* Frame for "split to fixed number". */

	GtkWidget	*fccount;	/* Spinner for the count, 2+. */

	GtkListStore	*preview_store;	/* Preview list model. */
	GtkWidget	*preview;	/* GtkTreeView for the preview. */

	MainInfo	*min;
	guint		curr_mode;	/* 0 for fixed size, 1 for fixed amount. */
	const gchar	*name;		/* Source file being split. */
	goffset		file_size;	/* Size of the current file. */

	NameGenerator	*namegen;
} SplitInfo;

/* ----------------------------------------------------------------------------------------- */

static const gchar * get_name_format(const SplitInfo *spi)
{
	return gtk_entry_get_text(GTK_ENTRY(spi->nformat));
}

static guint get_name_base(const SplitInfo *spi)
{
	return gtk_adjustment_get_value(GTK_ADJUSTMENT(spi->nbadj));
}

static guint get_name_step(const SplitInfo *spi)
{
	return gtk_adjustment_get_value(GTK_ADJUSTMENT(spi->nsadj));
}

/* ----------------------------------------------------------------------------------------- */

#define	PREVIEW_MAX	30	/* Must be at least 2, and must be even. */

typedef enum {
	DISPLAY_NORMAL,
	DISPLAY_ELLIPSIS,
	DISPLAY_NOTHING
} PreviewDisplay;

/* 2010-12-03 -	Returns one of the PreviewDisplay values depending on how the part_index:th row should be shown. */
static PreviewDisplay get_preview_display(guint64 index, guint64 num)
{
	if(num <= PREVIEW_MAX)
		return DISPLAY_NORMAL;
	if(index < PREVIEW_MAX / 2)
		return DISPLAY_NORMAL;
	if(index == PREVIEW_MAX / 2 + 1)
		return DISPLAY_ELLIPSIS;
	if(index >= num - PREVIEW_MAX / 2)
		return DISPLAY_NORMAL;
	return DISPLAY_NOTHING;
}

static void	namegenerator_configure(NameGenerator *sng, const SplitInfo *spi);

/* 2010-12-03 -	Create a "name generator", which is basically the parameters needed to (on-demand) generate name 'i' from a split sequence. */
static NameGenerator * namegenerator_new(const SplitInfo *spi)
{
	NameGenerator	*sng = g_malloc(sizeof *sng);

	namegenerator_configure(sng, spi);

	return sng;
}

/* 2010-12-11 -	Configure the name generator, based on state in spi's widgets. */
static void namegenerator_configure(NameGenerator *sng, const SplitInfo *spi)
{
	sng->file_size = spi->file_size;

	/* Inspect combo state, and compute a suitable part size. */
	if(gtk_combo_box_get_active(GTK_COMBO_BOX(spi->mode)) == 0)
		sng->part_size = sze_get_offset(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(spi->sssize)));
	else
	{
		const gchar	*num = gtk_entry_get_text(GTK_ENTRY(spi->fccount));
		const guint64	num_parts = g_ascii_strtoull(num, NULL, 0);

		/* This actually works. Imagine proof, here. */
		sng->part_size = (goffset) ((sng->file_size + num_parts - 1) / num_parts);
	}

	sng->base = get_name_base(spi);
	sng->step = get_name_step(spi);
	sng->num = (sng->file_size + sng->part_size - 1) / sng->part_size;
	sng->last = sng->base + (sng->num - 1) * sng->step;

	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(spi->nfill)))
		g_snprintf(sng->v_format, sizeof sng->v_format, "%%0%u" G_GUINT64_FORMAT, (guint) gtk_adjustment_get_value(GTK_ADJUSTMENT(spi->npadj)));
	else
		strcpy(sng->v_format, "%" G_GUINT64_FORMAT);

	sng->parameters = g_hash_table_new(g_str_hash, g_str_equal);
	sng->v_index[0] = '\0';
	g_hash_table_insert(sng->parameters, "index", sng->v_index);
	g_snprintf(sng->v_base, sizeof sng->v_base, sng->v_format, sng->base);
	g_hash_table_insert(sng->parameters, "base", sng->v_base);
	g_snprintf(sng->v_step, sizeof sng->v_step, sng->v_format, sng->step);
	g_hash_table_insert(sng->parameters, "step", sng->v_step);
	g_snprintf(sng->v_num, sizeof sng->v_num, sng->v_format, sng->num);
	g_hash_table_insert(sng->parameters, "num", sng->v_num);
	g_snprintf(sng->v_last, sizeof sng->v_last, sng->v_format, sng->last);
	g_hash_table_insert(sng->parameters, "last", sng->v_last);
	sng->v_offset[0] = '\0';
	g_hash_table_insert(sng->parameters, "offset", sng->v_offset);
}

static void namegenerator_update_offset(NameGenerator *sng, guint64 offset)
{
	g_snprintf(sng->v_offset, sizeof sng->v_offset, sng->v_format, offset);
}

static gboolean namegenerator_generate(NameGenerator *sng, gchar *buf, gsize buf_size, const gchar *format, guint64 index)
{
	g_snprintf(sng->v_index, sizeof sng->v_index, sng->v_format, sng->base + index * sng->step);

	return stu_interpolate_dictionary(buf, buf_size, format, sng->parameters);
}

static void namegenerator_destroy(NameGenerator *sng)
{
	g_hash_table_destroy(sng->parameters);
	g_free(sng);
}

static void update_preview_ss(const gchar *format, gboolean zero_fill, gint precision, guint64 base, guint64 step, SplitInfo *spi)
{
	goffset		size = spi->file_size, chunk;
	guint64		pos = 0, part_index;
	GtkTreeIter	iter;

	gtk_list_store_clear(spi->preview_store);
	namegenerator_configure(spi->namegen, spi);

	for(part_index = 0; size > 0; size -= chunk, pos += chunk, part_index++)
	{
		PreviewDisplay	dpl;

		if(size > spi->namegen->part_size)
			chunk = spi->namegen->part_size;
		else
			chunk = size;
		dpl = get_preview_display(part_index, spi->namegen->num);
		if(dpl == DISPLAY_NORMAL)
		{
			gchar	part[2 * MAXNAMLEN], part2[2 * MAXNAMLEN], part3[2 * MAXNAMLEN];
			sze_put_offset(part, sizeof part, pos, SZE_BYTES_NO_UNIT, 0, ',');
			namegenerator_update_offset(spi->namegen, pos);
			namegenerator_generate(spi->namegen, part2, sizeof part2, format, part_index);
			sze_put_offset(part3, sizeof part3, chunk, SZE_BYTES_NO_UNIT, 0, ',');
			gtk_list_store_insert_with_values(spi->preview_store, &iter, -1, 0, part, 1, part2, 2, part3, -1);
		}
		else if(dpl == DISPLAY_ELLIPSIS)
		{
			gtk_list_store_insert_with_values(spi->preview_store, &iter, -1, 1, "...", -1);
		}
	}
}

static void update_preview_fc(const gchar *format, gboolean zero_fill, gint precision, guint64 base, guint64 step, SplitInfo *spi)
{
	update_preview_ss(format, zero_fill, precision, base, step, spi);
}

static void update_preview(SplitInfo *spi)
{
	const gchar	*format = get_name_format(spi);
	const guint	base = get_name_base(spi);
	const guint	step = get_name_step(spi);
	const gboolean	zero_fill = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(spi->nfill));
	const gint	precision = gtk_adjustment_get_value(GTK_ADJUSTMENT(spi->npadj));

	if(spi->curr_mode == 0)
		update_preview_ss(format, zero_fill, precision, base, step, spi);
	else
		update_preview_fc(format, zero_fill, precision, base, step, spi);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-01 -	Pretty much rewrote this function, now that the sizeutil module exists. */
static void spt_body(MainInfo *min, DirPane *src, DirRow2 *row, gpointer gen, gpointer user)
{
	gchar		buf[2 * MAXNAMLEN], siz1[32], siz2[32];
	SplitInfo	*spi = user;
	GtkTreeModel	*m = dp_get_tree_model(src);
	gint		pos;

	spi->name = dp_row_get_name(m, row);
	spi->file_size = dp_row_get_size(m, row);

	if(spi->namegen == NULL)
		spi->namegen = namegenerator_new(spi);

	sze_put_offset(siz1, sizeof siz1, dp_row_get_size(m, row), SZE_BYTES, 3, ',');
	sze_put_offset(siz2, sizeof siz2, dp_row_get_size(m, row), SZE_AUTO, 3, ',');
	if(strcmp(siz1, siz2) != 0)
		g_snprintf(buf, sizeof buf, _("Split \"%s\".\nFile is %s (%s)."), dp_row_get_name_display(m, row), siz1, siz2);
	else
		g_snprintf(buf, sizeof buf, _("Split \"%s\".\nFile is %s."), dp_row_get_name_display(m, row), siz1);
	gtk_label_set_text(GTK_LABEL(spi->label), buf);
	gtk_entry_set_text(GTK_ENTRY(spi->nformat), dp_row_get_name_display(m, row));
	pos = gtk_entry_get_text_length(GTK_ENTRY(spi->nformat));
	gtk_editable_insert_text(GTK_EDITABLE(spi->nformat), ".{index}", 8, &pos);

	cmd_generic_track_entry(gen, spi->nformat);
	gtk_combo_box_set_active(GTK_COMBO_BOX(spi->mode), spi->curr_mode);

	update_preview(spi);
}

/* 2009-10-07 -	Do a partial "splice", i.e. a copying of data from an input stream into an output stream.
**		Here, 'chunk' is the number of bytes to copy, and 'buf' & 'buf_size' describe the buffer
**		used to hold the data.
*/
static gboolean streams_splice_partial(GOutputStream *out, GInputStream *in, gsize chunk, gpointer buf, gsize buf_size, GError **err)
{
	gsize	to_go = chunk, bite;
	gssize	did;

	while(to_go > 0)
	{
		bite = to_go > buf_size ? buf_size : to_go;
		did = g_input_stream_read(in, buf, bite, NULL, err);
		if(did == bite)
		{
			did = g_output_stream_write(out, buf, bite, NULL, err);
			if(did != bite)
				break;
		}
		else
			break;
		to_go -= bite;
	}
	return to_go == 0;
}

/* 1998-09-01 -	Split file on row <row> of <src>, producing a bunch of segment files in <dst>.
**		Each segment (except possibly the last) will have the same size.
** 1998-09-12 -	Didn't have any closing of the output files. I really should be shot. Also
**		fixed the protection bits for the output; now same as the source file.
** 1999-11-13 -	Adapted to use new fut_copy_partial() function, fixed bug with closing output
**		file on failed write.
** 2010-07-22 -	Now supports both split modes, in an attack of epic closure.
** 2010-12-11 -	Now uses the NameGenerator API to generate actual names, to sync with preview.
*/
static gint do_split(SplitInfo *spi, DirPane *src, DirPane *dst, DirRow2 *row, GError **error)
{
	const gchar		*format = gtk_entry_get_text(GTK_ENTRY(spi->nformat));
	gchar			outname[PATH_MAX];
	gsize			to_go = 0, chunk;
	gint			piece = 0;
	GFile			*fin, *fout;
	GFileInputStream	*sin;
	gpointer		tmp;
	gboolean		ok = TRUE;

	/* Make sure the name generator is updated and valid-seeming. */
	namegenerator_configure(spi->namegen, spi);
	if(spi->namegen->part_size == 0)
		return 0;

	if((fin = dp_get_file_from_row(src, row)) == NULL)
		return 0;
	if((sin = g_file_read(fin, NULL, error)) == NULL)
	{
		g_object_unref(fin);
		return 0;
	}
	if((tmp = g_malloc(SPLIT_CHUNK)) == NULL)
	{
		g_object_unref(fin);
		g_object_unref(sin);
		return 0;
	}

	ovw_overwrite_begin(spi->min, _("\"%s\" Already Exists - Continue With Split?"), 0UL);

	to_go = dp_row_get_size(dp_get_tree_model(src), row);
	for(piece = 0; ok && to_go > 0; to_go -= chunk, piece++)
	{
		if(to_go > spi->namegen->part_size)
			chunk = spi->namegen->part_size;
		else
			chunk = to_go;
		namegenerator_generate(spi->namegen, outname, sizeof outname, format, piece);
		if((fout = g_file_get_child_for_display_name(dst->dir.root, outname, error)) != NULL)
		{
			GFileOutputStream	*sout;
			OvwRes			ores;

			ores  = ovw_overwrite_unary_file(dst, fout);
			if(ores == OVW_CANCEL)
				ok = FALSE;
			else if(ores == OVW_SKIP)
				ok = g_seekable_seek(G_SEEKABLE(sin), chunk, G_SEEK_CUR, NULL, error);
			else if(ores == OVW_PROCEED_FILE || ores == OVW_PROCEED_DIR)
				ok = del_delete_gfile(spi->min, fout, FALSE, error);
			if(ok && (sout = g_file_create(fout, G_FILE_CREATE_NONE, NULL, error)) != NULL)
			{
				ok = streams_splice_partial(G_OUTPUT_STREAM(sout), G_INPUT_STREAM(sin), chunk, tmp, SPLIT_CHUNK, error);
				g_object_unref(sout);
			}
			else
				ok = FALSE;
			g_object_unref(fout);
		}
		else
			ok = FALSE;
	}

	ovw_overwrite_end(spi->min);
	g_free(tmp);
	g_object_unref(sin);
	g_object_unref(fin);

	return ok;
}

static gint spt_action(MainInfo *min, DirPane *src, DirPane *dst, DirRow2 *row, GError **error, gpointer user)
{
	SplitInfo	*spi = user;
	gint		ret = 0;

	if((ret = do_split(spi, src, dst, row, error)))
		dp_unselect(src, row);

	return ret;
}

static void spt_free(gpointer user)
{
	SplitInfo	*spi = user;

	namegenerator_destroy(spi->namegen);
	spi->namegen = NULL;
}

/* ----------------------------------------------------------------------------------------- */

static void evt_ss_combo_changed(GtkWidget *combo, gpointer user)
{
	update_preview(user);
}

/* 1998-07-09 -	Build widgetry needed to support splitting to fixed part size. */
static GtkWidget * build_ss(SplitInfo *spi)
{
	GtkWidget	*frame, *hbox, *label;

	frame = gtk_frame_new(_("Fixed Size Split"));
	hbox  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new(_("Segment Size"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	spi->sssize = gtk_combo_box_text_new_with_entry();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(spi->sssize), _("1457000 bytes (3.5\" floppy)"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(spi->sssize), _("10485760 bytes (10 MB)"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(spi->sssize), _("26214400 bytes (25 MB)"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(spi->sssize), _("52428800 bytes (50 MB)"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(spi->sssize), _("78643200 bytes (75 MB)"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(spi->sssize), _("100431360 bytes (95 MB, Zip disk)"));
	gtk_combo_box_set_active(GTK_COMBO_BOX(spi->sssize), 0);
	g_signal_connect(G_OBJECT(spi->sssize), "changed", G_CALLBACK(evt_ss_combo_changed), spi);
	gtk_box_pack_start(GTK_BOX(hbox), spi->sssize, TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(frame), hbox);

	return frame;
}

static void evt_fc_spin_changed(GtkWidget *spin, gpointer user)
{
	update_preview(user);
}

/* 2010-07-22 -	Quite a while later, I actually got around to implementing the other split
**		mode: fixed count (=number of parts).
*/
static GtkWidget * build_fc(SplitInfo *spi)
{
	GtkWidget	*frame, *hbox, *label;

	frame = gtk_frame_new(_("Fixed Count Split"));

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new(_("Segment Count"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	spi->fccount = gtk_spin_button_new_with_range(2, 1000, 1);	/* Incredibly arbitrary, yes. */
	g_signal_connect(G_OBJECT(spi->fccount), "value_changed", G_CALLBACK(evt_fc_spin_changed), spi);
	gtk_box_pack_start(GTK_BOX(hbox), spi->fccount, TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(frame), hbox);

	return frame;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-01 -	Callback for mode option menu. */
static void evt_mode_select(GtkWidget *wid, guint index, gpointer user)
{
	SplitInfo	*spi = user;

	spi->curr_mode = index;
	gtk_notebook_set_current_page(GTK_NOTEBOOK(spi->nbook), spi->curr_mode);
	update_preview(spi);
}

/* ----------------------------------------------------------------------------------------- */

static void evt_nformat_insert(GtkEntryBuffer *buffer, guint position, gchar *chars, guint nchars, gpointer user)
{
	update_preview(user);
}

static void evt_nformat_delete(GtkEntryBuffer *buffer, guint position, guint nchars, gpointer user)
{
	update_preview(user);
}

static void evt_nformat_details_clicked(GtkWidget *button, gpointer user)
{
	/* FIXME: This data should, of course, be part of the dictionary-based interpolation
	 * system, this is very un-DRY design. Something for a future refactoring, there's
	 * also an obvious need (eh) to make the Split command and the regular Command config
	 * use the same subsystem for managing their interpolation needs.
	 */
	static const struct {
		const gchar	*symbol;
		const gchar	*desc;
	} sym_doc[] = {
	{ "index", N_("Current part number, unique for every created file") },
	{ "base", N_("The value from the Base box") },
	{ "step", N_("The amount that index will change for each file") },
	{ "num", N_("The total number of files that will be created") },
	{ "last", N_("The value of index for the last file to be created") },
	{ "offset", N_("The part's offset into the original file") },
	};
	Dialog			*dlg;
	GtkListStore		*store;
	GtkTreeIter		iter;
	GtkCellRenderer		*cr;
	GtkTreeViewColumn	*vc;
	GtkWidget		*scwin, *view;
	gsize			i;

	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	for(i = 0; i < sizeof sym_doc / sizeof *sym_doc; ++i)
	{
		gtk_list_store_insert_with_values(store, &iter, -1, 0, sym_doc[i].symbol, 1, _(sym_doc[i].desc), -1);
	}
	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), GTK_SELECTION_BROWSE);
	cr = gtk_cell_renderer_text_new();
	vc = gtk_tree_view_column_new_with_attributes("(code)", cr, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), vc);
	vc = gtk_tree_view_column_new_with_attributes("(description)", cr, "text", 1, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), vc);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
	gtk_container_add(GTK_CONTAINER(scwin), view);

	dlg = dlg_dialog_sync_new(scwin, _("Pick Code"), NULL);
	gtk_widget_set_size_request(GTK_WIDGET(dlg_dialog_get_dialog(dlg)), 380, 240);
	if(dlg_dialog_sync_wait(dlg) == DLG_POSITIVE)
	{
		SplitInfo	*spi = user;

		if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), NULL, &iter))
		{
			gchar	*code = NULL, buf[1024];
			gint	pos;

			gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 0, &code, -1);
			/* FIXME: We could be more intelligent here, and not add the braces if the cursor is after an opening brace. */
			g_snprintf(buf, sizeof buf, "{%s}", code);
			gtk_editable_insert_text(GTK_EDITABLE(spi->nformat), buf, -1, &pos);
		}
	}
}

static void evt_check_changed(GtkWidget *button, gpointer user)
{
	SplitInfo	*spi = user;

	gtk_widget_set_sensitive(spi->nprec, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)));
	update_preview(user);
}

static void evt_spin_changed(GtkWidget *button, gpointer user)
{
	update_preview(user);
}

gint cmd_split(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	static SplitInfo	spi = { 0 };
	const gchar		*mode[] = { N_("Fixed size, variable number of parts"),
					    N_("Fixed number of parts, variable sizes"),
					    NULL };
	GtkWidget		*hbox, *label;
	GtkWidget		*hsep, *exp, *scwin, *fdet;
	GtkCellRenderer		*cr;
	GtkTreeViewColumn	*vc;

	spi.min = min;
	spi.vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	spi.label = gtk_label_new(_("Split"));

	/* Build this early, since the signal handler references it. */
	spi.nbook = gtk_notebook_new();
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(spi.nbook), FALSE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(spi.nbook), FALSE);
	gtk_notebook_append_page(GTK_NOTEBOOK(spi.nbook), build_ss(&spi), NULL);
	gtk_notebook_append_page(GTK_NOTEBOOK(spi.nbook), build_fc(&spi), NULL);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new(_("Mode"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(spi.vbox), spi.label, FALSE, FALSE, 0);
	hsep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(spi.vbox), hsep, FALSE, FALSE, 4);
	spi.mode = gui_build_combo_box(mode, G_CALLBACK(evt_mode_select), &spi);
	gtk_box_pack_start(GTK_BOX(hbox), spi.mode, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(spi.vbox), hbox, FALSE, FALSE, 0);
	spi.nhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new(_("Name Format"));
	gtk_box_pack_start(GTK_BOX(spi.nhbox), label, FALSE, FALSE, 2);
	spi.nformat = gui_dialog_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(spi.nformat), NFORMAT_LIMIT);
	g_signal_connect(G_OBJECT(gtk_entry_get_buffer(GTK_ENTRY(spi.nformat))), "inserted_text", G_CALLBACK(evt_nformat_insert), &spi);
	g_signal_connect(G_OBJECT(gtk_entry_get_buffer(GTK_ENTRY(spi.nformat))), "deleted_text", G_CALLBACK(evt_nformat_delete), &spi);
	gtk_box_pack_start(GTK_BOX(spi.nhbox), spi.nformat, TRUE, TRUE, 0);
	fdet = gui_details_button_new();
	g_signal_connect(G_OBJECT(fdet), "clicked", G_CALLBACK(evt_nformat_details_clicked), &spi);
	gtk_box_pack_start(GTK_BOX(spi.nhbox), fdet, FALSE, FALSE, 0);
	label = gtk_label_new(_("Base"));
	gtk_box_pack_start(GTK_BOX(spi.nhbox), label, FALSE, FALSE, 0);
	spi.nbadj = gtk_adjustment_new(0, 0, 999, 1, 16, 0.);
	spi.nbase = gtk_spin_button_new(GTK_ADJUSTMENT(spi.nbadj), 0, 0);
	g_signal_connect(G_OBJECT(spi.nbase), "value_changed", G_CALLBACK(evt_spin_changed), &spi);
	gtk_box_pack_start(GTK_BOX(spi.nhbox), spi.nbase, FALSE, FALSE, 0);
	label = gtk_label_new(_("Step"));
	gtk_box_pack_start(GTK_BOX(spi.nhbox), label, FALSE, FALSE, 0);
	spi.nsadj = gtk_adjustment_new(1, 1, 63, 1, 16, 0.);
	spi.nstep = gtk_spin_button_new(GTK_ADJUSTMENT(spi.nsadj), 0, 0);
	g_signal_connect(G_OBJECT(spi.nstep), "value_changed", G_CALLBACK(evt_spin_changed), &spi);
	gtk_box_pack_start(GTK_BOX(spi.nhbox), spi.nstep, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(spi.vbox), spi.nhbox, FALSE, FALSE, 2);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	spi.nfill = gtk_check_button_new_with_label(_("Zero-Fill Numbers?"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(spi.nfill), TRUE);
	g_signal_connect(G_OBJECT(spi.nfill), "toggled", G_CALLBACK(evt_check_changed), &spi);
	gtk_box_pack_start(GTK_BOX(hbox), spi.nfill, TRUE, TRUE, 0);
	label = gtk_label_new("Precision");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	spi.npadj = gtk_adjustment_new(3, 2, 9, 1, 3, 0.);
	spi.nprec = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, GTK_ADJUSTMENT(spi.npadj));
	gtk_scale_set_digits(GTK_SCALE(spi.nprec), 0);
	gtk_scale_set_value_pos(GTK_SCALE(spi.nprec), GTK_POS_RIGHT);
	g_signal_connect(G_OBJECT(spi.nprec), "value_changed", G_CALLBACK(evt_spin_changed), &spi);
	gtk_box_pack_start(GTK_BOX(hbox), spi.nprec, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(spi.vbox), hbox, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(spi.vbox), spi.nbook, FALSE, FALSE, 0);

	spi.preview_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
	spi.preview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(spi.preview_store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(spi.preview), TRUE);
	cr = gtk_cell_renderer_text_new();
	g_object_set(G_OBJECT(cr), "xalign", 1.0f, NULL);
	vc = gtk_tree_view_column_new_with_attributes(_("Offset"), cr, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(spi.preview), vc);
	cr = gtk_cell_renderer_text_new();
	vc = gtk_tree_view_column_new_with_attributes(_("Name"), cr, "text", 1, NULL);
	gtk_tree_view_column_set_sizing(vc, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(spi.preview), vc);
	cr = gtk_cell_renderer_text_new();
	g_object_set(G_OBJECT(cr), "xalign", 1.0f, NULL);
	vc = gtk_tree_view_column_new_with_attributes(_("Size"), cr, "text", 2, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(spi.preview), vc);

	exp = gtk_expander_new(_("Preview"));
	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scwin), spi.preview);
	gtk_container_add(GTK_CONTAINER(exp), scwin);
	gtk_box_pack_start(GTK_BOX(spi.vbox), exp, TRUE, TRUE, 5);

	return cmd_generic(min, _("Split"), CGF_NOALL | CGF_NODIRS, spt_body, spt_action, spt_free, &spi);
}
