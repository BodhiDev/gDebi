/*
** 2001-09-30 -	A rename command that creates numbered sequences. I want it when arranging
**		images from a digital camera, but it might be handy for other stuff too, I'm
**		sure.
**
**		This is slightly more complicated than you might think at first, since
**		it actually does _two_ renames: first all affected files are renamed
**		to sequential temporary names, and then in a second pass the final names
**		are set. This allows the final sequence to overlap the initial, which I
**		predict will be useful.
*/

#include "gentoo.h"

#include <ctype.h>

#include "cmd_delete.h"
#include "dialog.h"
#include "dirpane.h"
#include "errors.h"
#include "fileutil.h"
#include "guiutil.h"
#include "overwrite.h"
#include "strutil.h"

#include "cmd_renameseq.h"

/* ----------------------------------------------------------------------------------------- */

typedef enum { BASE_8, BASE_10, BASE_16L, BASE_16U } RSBase;

typedef struct {
	MainInfo	*min;

	guint		start;		/* Index to start at. */
	RSBase		base;		/* The base we're using. */
	guint		precision;	/* Precision, i.e. number of digits. */
	GString		*head;		/* First part of name, before number. */
	GString		*tail;		/* End part, after number. */
} RenSeqInfo;

/* Widgets that are used to set the various parameters above, in dialog mode. */
typedef struct {
	RenSeqInfo	*rsi;
	const gchar	*name;		/* Name from first selected row, for Guess. */
	Dialog		*dlg;
	GtkWidget	*start;
	GtkWidget	*base;
	GtkWidget	*precision;
	GtkWidget	*head;
	GtkWidget	*tail;
	GtkWidget	*guess;
	GtkWidget	*preview;
} RenSeqDialog;

/* ----------------------------------------------------------------------------------------- */

static const gchar * compute_name(RenSeqInfo *rsi, guint index)
{
	static gchar	buf[PATH_MAX];
	const gchar	*fptr = NULL;

	switch(rsi->base)
	{
		case BASE_8:	fptr = "%s%0*o%s";	break;
		case BASE_10:	fptr = "%s%0*u%s";	break;
		case BASE_16L:	fptr = "%s%0*x%s";	break;
		case BASE_16U:	fptr = "%s%0*X%s";	break;
	}
	g_snprintf(buf, sizeof buf, fptr, rsi->head->str, rsi->precision, rsi->start + index, rsi->tail->str);

	return buf;
}

static void temp_file_undo(GSList *titer, GSList *siter, GtkTreeModel *model)
{
	g_file_set_display_name(titer->data, dp_row_get_name_display(model, siter->data), NULL, NULL);
}

/* 2009-10-04 -	Given a list of selected input files, and a list of temporary renamed files, undo the renames.
**		The length of tlist might be shorter than that of slist, so let it decide.
*/
static void temp_files_undo(GSList *tlist, GSList *slist, GtkTreeModel *model)
{
	GSList		*titer, *siter;

	for(titer = tlist, siter = slist; titer != NULL; titer = g_slist_next(titer), siter = g_slist_next(siter))
	{
		if(titer->data != NULL)
			temp_file_undo(titer, siter, model);
	}
}

/* 2009-10-04 -	Free a list of temporary files. */
static void temp_files_free(GSList *tlist)
{
	GSList	*titer;

	for(titer = tlist; titer != NULL; titer = g_slist_next(titer))
	{
		if(titer->data != NULL)
			g_object_unref(titer->data);
	}
	g_slist_free(tlist);
}

static gboolean do_renameseq(RenSeqInfo *rsi, DirPane *src, GError **err)
{
	GSList		*slist, *iter, *tlist = NULL;
	GString		*tmppfx;
	gboolean	ok = TRUE;
	guint		index;
	GtkTreeModel	*m = dp_get_tree_model(src);

	/* A Rename in gentoo is never a move. Gotta keep'em separated. */
	if(strchr(rsi->head->str, G_DIR_SEPARATOR) != NULL ||
	   strchr(rsi->tail->str, G_DIR_SEPARATOR) != NULL)
		return FALSE;

	slist = dp_get_selection(src);
	tmppfx = g_string_sized_new(64);
	g_string_append_printf(tmppfx, ".gt!%u:%x", (guint) getpid(), (guint) time(NULL) % 1007);
	/* Give all files temporary names, so we can handle overlaps in sequencing. */
	for(iter = slist, index = 0; ok && (iter != NULL); iter = g_slist_next(iter), index++)
	{
		GFile	*f, *nf;

		if((f = dp_get_file_from_row(src, iter->data)) != NULL)
		{
			gchar	dname[PATH_MAX];

			g_snprintf(dname, sizeof dname, "%s,%u", tmppfx->str, index);
			nf = g_file_set_display_name(f, dname, NULL, err);
			if(nf != NULL)
			{
				tlist = g_slist_append(tlist, nf);
				dp_unselect(src, iter->data);
			}
			else
				ok = FALSE;
		}
		else
			ok = FALSE;
	}
	/* If temporary-naming went fine, continue with final renames. */
	if(ok)
	{
		GSList	*titer, *siter;
		GFile	*nf;

		/* Note: can't iterate over selection, the pane's GFile pointers are stale after the temporary rename. */
		ovw_overwrite_begin(rsi->min, _("\"%s\" Already Exists - Proceed With Rename?"), OVWF_NO_RECURSE_TEST);
		for(titer = tlist, siter = slist, index = 0; ok && (titer != NULL); titer = g_slist_next(titer), siter = g_slist_next(siter), index++)
		{
			const gchar	*nn = compute_name(rsi, index);
			OvwRes		ores;
			GFile		*dest;

			dest = dp_get_file_from_name(src, nn);
			ores = ovw_overwrite_unary_file(src, dest);
			if(ores == OVW_SKIP)
			{
				/* On skip, a single temporary name must be undo:ed. */
				temp_file_undo(titer, siter, m);
				/* Then this file must be removed from the temporary list. We do this by just blanking it out. */
				g_object_unref(titer->data);
				titer->data = NULL;
				/* Drop the destination file, so we can continue the loop. */
				g_object_unref(dest);
				continue;
			}
			else if(ores == OVW_CANCEL)
				ok = FALSE;
			else if(ores == OVW_PROCEED_DIR || ores == OVW_PROCEED_FILE)
				ok = del_delete_gfile(rsi->min, dest, FALSE, err);

			if(ok)
			{
				nf = g_file_set_display_name(titer->data, compute_name(rsi, index), NULL, err);
				if(nf != NULL)
					g_object_unref(nf);
				else
					ok = FALSE;
			}
			g_object_unref(dest);
		}
		ovw_overwrite_end(rsi->min);
	}
	else
		temp_files_undo(tlist, slist, m);
	temp_files_free(tlist);
	dp_free_selection(slist);

	return ok;
}

static void init_start(const RenSeqInfo *rsi, GtkWidget *entry)
{
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(entry), rsi->start);
}

/* 2001-10-02 -	Crap day. Anyway, extract state from various widgets, and dump it into the
**		'rsi' field of <dlg>.
*/
static void rsi_update(RenSeqDialog *dlg)
{
	/* First, extract current base so we know how to parse the start index. */
	dlg->rsi->base = gtk_combo_box_get_active(GTK_COMBO_BOX(dlg->base));
	gtk_widget_set_sensitive(dlg->guess, dlg->rsi->base == BASE_10);

	if(sscanf(gtk_entry_get_text(GTK_ENTRY(dlg->start)), "%u", &dlg->rsi->start) != 1)
		dlg->rsi->start = 1;

	dlg->rsi->precision = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(dlg->precision));

	g_string_assign(dlg->rsi->head, gtk_entry_get_text(GTK_ENTRY(dlg->head)));
	g_string_assign(dlg->rsi->tail, gtk_entry_get_text(GTK_ENTRY(dlg->tail)));
}

static void preview_update(RenSeqDialog *dlg)
{
	rsi_update(dlg);
	gtk_entry_set_text(GTK_ENTRY(dlg->preview), compute_name(dlg->rsi, 0U));
}

/* 2001-10-02 -	Joint widget-changed handler. Just call preview_update(). */
static void evt_something_changed(GtkWidget *wid, gpointer user)
{
	preview_update(user);
}

static gboolean find_digit(gunichar here, gpointer user)
{
	return g_unichar_isdigit(here);
}

/* 2010-03-07 -	Inspect first name in selection, and attempt to set parameters
**		accordingly. Glitzy, sure, but I do like sugar in my software. :)
**		Now works in UTF-8 (display names), and only in decimal mode.
*/
static void evt_guess_clicked(GtkWidget *wid, gpointer user)
{
	RenSeqDialog	*dlg = user;
	GString		*head;
	const gchar	*dptr;
	gunichar	here;

	/* String processing here is on dlg->name, which is a GIO "display name". */
	/* Extract characters up to the first digit. */
	head = g_string_new("");
	if((dptr = stu_utf8_find(dlg->name, find_digit, NULL)) != NULL && dptr > dlg->name)
	{
		guint	value = 0, prec = 0;

		g_string_append_len(head, dlg->name, dptr - dlg->name);
		/* Count the number of digits, while extracting actual (decimal) number. */
		while(*dptr && g_unichar_isdigit(here = g_utf8_get_char(dptr)))
		{
			value *= 10;
			value += here - '0';
			prec++;
			dptr = g_utf8_next_char(dptr);
		}
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(dlg->start), value);
		gtk_entry_set_text(GTK_ENTRY(dlg->head), head->str);
		gtk_entry_set_text(GTK_ENTRY(dlg->tail), dptr);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(dlg->precision), prec);
	}
	else if((dptr = g_utf8_strchr(dlg->name, -1, '.')) != NULL)
	{
		g_string_append_len(head, dlg->name, dptr - dlg->name);
		gtk_entry_set_text(GTK_ENTRY(dlg->head), head->str);
		gtk_entry_set_text(GTK_ENTRY(dlg->tail), dptr);
	}
}

/* 2001-10-03 -	Well, here's the actual command entrypoint, like usual. */
gint cmd_renameseq(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	static RenSeqInfo	rsi = { NULL };
	const gchar		*bname[] = { N_("8, Octal"), N_("10, Decimal"), N_("16, Hex (a-f)"), N_("16, Hex (A-F)") };
	GtkAdjustment		*adj;
	GtkWidget		*vbox, *grid, *label;
	RenSeqDialog		dlg;
	gint			i, ret;
	GSList			*sel;
	gboolean		ok = FALSE;

	if((sel = dp_get_selection(src)) == NULL)
		return 0;
	dlg.name = dp_row_get_name_display(dp_get_tree_model(src), sel->data);
	dp_free_selection(sel);

	if(rsi.min == NULL)
	{
		rsi.min   = min;
		rsi.start = 1;
		rsi.base  = BASE_10;
		rsi.precision = 5;
		rsi.head = g_string_new("");
		rsi.tail = g_string_new("");
	}
	dlg.rsi = &rsi;

	vbox  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	label = gtk_label_new(_("This command renames all selected files\n"
				"into a numbered sequence. The controls\n"
				"below let you define how the names are\n"
				"formed."));
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	grid = gtk_grid_new();
	label = gtk_label_new(_("Start At"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
	adj = gtk_adjustment_new(1.0, 0.0, 4924967295.0, 1.0, 10.0, 0.0);
	dlg.start = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1.0, 0);
	init_start(&rsi, dlg.start);
	g_signal_connect(G_OBJECT(dlg.start), "changed", G_CALLBACK(evt_something_changed), &dlg);
	gtk_widget_set_hexpand(dlg.start, TRUE);
	gtk_widget_set_halign(dlg.start, GTK_ALIGN_FILL);
	gtk_grid_attach(GTK_GRID(grid), dlg.start, 1, 0, 2, 1);
	label = gtk_label_new("(Base 10)");
	gtk_grid_attach(GTK_GRID(grid), label, 3, 0, 1, 1);

	label = gtk_label_new(_("Base"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
	dlg.base = gtk_combo_box_text_new();
	for(i = 0; i < sizeof bname / sizeof bname[0]; i++)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dlg.base), _(bname[i]));
	g_signal_connect(G_OBJECT(dlg.base), "changed", G_CALLBACK(evt_something_changed), &dlg);
	gtk_grid_attach(GTK_GRID(grid), dlg.base, 1, 1, 1, 1);

	label = gtk_label_new(_("Precision"));
	gtk_grid_attach(GTK_GRID(grid), label, 2, 1, 1, 1);
	adj   = gtk_adjustment_new(rsi.precision, 0.0, 10.0, 1.0, 1.0, 0.0);
	dlg.precision = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 0, 0);
	g_signal_connect(G_OBJECT(adj), "value_changed", G_CALLBACK(evt_something_changed), &dlg);
	gtk_grid_attach(GTK_GRID(grid), dlg.precision, 3, 1, 1, 1);

	label = gtk_label_new(_("Head"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 2, 1, 1);
	dlg.head = gui_dialog_entry_new();
	gtk_entry_set_text(GTK_ENTRY(dlg.head), rsi.head->str);
	g_signal_connect(G_OBJECT(dlg.head), "changed", G_CALLBACK(evt_something_changed), &dlg);
	gtk_grid_attach(GTK_GRID(grid), dlg.head, 1, 2, 2, 1);

	label = gtk_label_new(_("Tail"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 3, 1, 1);
	dlg.tail = gui_dialog_entry_new();
	gtk_entry_set_text(GTK_ENTRY(dlg.tail), rsi.tail->str);
	g_signal_connect(G_OBJECT(dlg.tail), "changed", G_CALLBACK(evt_something_changed), &dlg);
	gtk_grid_attach(GTK_GRID(grid), dlg.tail, 1, 3, 2, 1);

	dlg.guess = gtk_button_new_with_label(_("Guess"));
	g_signal_connect(G_OBJECT(dlg.guess), "clicked", G_CALLBACK(evt_guess_clicked), &dlg);
	gtk_grid_attach(GTK_GRID(grid), dlg.guess, 3, 2, 1, 2);

	label = gtk_label_new(_("Preview"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 4, 1, 1);
	dlg.preview = gui_dialog_entry_new();
	gtk_editable_set_editable(GTK_EDITABLE(dlg.preview), FALSE);
	gtk_grid_attach(GTK_GRID(grid), dlg.preview, 1, 4, 3, 1);

	gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);

	gtk_combo_box_set_active(GTK_COMBO_BOX(dlg.base), rsi.base);
	preview_update(&dlg);

	dlg.dlg = dlg_dialog_sync_new(vbox, _("Sequential Rename"), NULL);
	ret = dlg_dialog_sync_wait(dlg.dlg);

	if(ret == DLG_POSITIVE)
	{
		GError	*err = NULL;

		rsi_update(&dlg);
		err_clear(min);
		ok = do_renameseq(&rsi, src, &err);
		dp_rescan_post_cmd(src);
		if(!ok)
			err_set_gerror(min, &err, "RenameSeq", NULL);
	}
	dlg_dialog_sync_destroy(dlg.dlg);

	return ok;
}
