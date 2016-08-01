/*
** 1999-05-29 -	A command analogous to Copy, but not quite. Instead of copying the selected source files to
**		the destination directory, this command creates a link at the destination, pointing to the
**		source. Pretty cool. This module also implements the SymLinkEdit command, which can be used
**		to create (!) and edit symbolic links.
*/

#include "gentoo.h"

#include "cmdarg.h"
#include "dialog.h"
#include "dirpane.h"
#include "errors.h"
#include "fileutil.h"
#include "guiutil.h"
#include "overwrite.h"
#include "strutil.h"

#include "cmd_delete.h"
#include "cmd_generic.h"
#include "cmd_symlink.h"

/* ----------------------------------------------------------------------------------------- */

typedef struct {		/* Symbolic link editing info. */
	GtkWidget	*vbox;
	GtkWidget	*name;		/* Entry for the name of the link (not editable unless creating new link). */
	GtkWidget	*contents;	/* Entry for the contents of the link (with pick button for coolness). */

	MainInfo	*min;
	DirPane		*src;
} SleInfo;

/* ----------------------------------------------------------------------------------------- */

/* 2009-09-19 -	Creates a symlink at <dfile>, pointing at <sfile>. */
static gboolean set_link(MainInfo *min, const GFile *dfile, const GFile *sfile, GError **error)
{
	gchar		*pn;
	gboolean	ok = FALSE;

	if((pn = g_file_get_parse_name((GFile *) sfile)) != NULL)
	{
		ok = g_file_make_symbolic_link((GFile *) dfile, pn, NULL, error);
		g_free(pn);
	}
	return ok;
}

/* 1999-05-29 -	Create (absolute) symbolic links for all selected entries in <src> at <dst>. */
gint cmd_symlink(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	GSList		*slist, *iter;
	OvwRes		res;
	guint		num = 0;
	GFile		*dfile = NULL;
	gboolean	ok = TRUE;
	GError		*error = NULL;

	err_clear(min);

	if((src == NULL) || (dst == NULL))
		return 1;

	if((slist = dp_get_selection(src)) == NULL)
		return 1;

	ovw_overwrite_begin(min, _("\"%s\" Already Exists - Continue With Link?"), OVWF_NO_RECURSE_TEST);
	for(iter = slist; ok && (iter != NULL); iter = g_slist_next(iter))
	{
		if(dfile != NULL)
			g_object_unref(dfile);
		dfile = dp_get_file_from_name(dst, dp_row_get_name(dp_get_tree_model(src), iter->data));
		res = ovw_overwrite_unary_file(dst, dfile);
		if(res == OVW_SKIP)
			continue;
		else if(res == OVW_CANCEL)
			break;
		else if(res == OVW_PROCEED_FILE || res == OVW_PROCEED_DIR)
		{
			if(!(ok = del_delete_gfile(min, dfile, FALSE, &error)))
				break;
		}
		ok = set_link(min, dfile, dp_get_file_from_row(src, iter->data), &error);
		if(ok)
		{
			dp_unselect(src, iter->data);
			num++;
		}
		else
			err_set_gerror(min, &error, "SymLink", dfile);
	}
	if(dfile != NULL)
		g_object_unref(dfile);
	ovw_overwrite_end(min);
	dp_free_selection(slist);

	if(num)
		dp_rescan_post_cmd(dst);
	return ok;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-30 -	Update generic command dialog for a new link. */
static void link_body(MainInfo *min, DirPane *src, DirRow2 *row, gpointer gen, gpointer user)
{
	SleInfo		*sle = user;
	const gchar	*link;

	gtk_entry_set_text(GTK_ENTRY(sle->name), dp_row_get_name_display(dp_get_tree_model(src), row));
	link = dp_row_get_link_target(dp_get_tree_model(src), row);
	gtk_entry_set_text(GTK_ENTRY(sle->contents), link);
	gtk_editable_select_region(GTK_EDITABLE(sle->contents), 0, -1);
	gtk_widget_grab_focus(sle->contents);

	cmd_generic_track_entry(gen, sle->contents);
}

/* 2009-09-19 -	Change a link. This is a GIO rewrite; it seems impossible to actually *change* an existing
**		symbolic link in-place; GIO gives me a "file exists" error when I try. So let's just delete
**		the existing link, and create a new one. Clumsy, but the semantics are right.
*/
static gint link_action(MainInfo *min, DirPane *src, DirPane *dst, DirRow2 *row, GError **error, gpointer user)
{
	SleInfo		*sle = user;
	GFile		*sfile;
	gboolean	ret;

	sfile = dp_get_file_from_row(src, row);
	if((ret = del_delete_gfile(min, sfile, FALSE, error)) != 0)
	{
		const gchar	*contents = gtk_entry_get_text(GTK_ENTRY(sle->contents));

		ret = g_file_make_symbolic_link(sfile, contents, NULL, error);
	}
	if(!ret)
		err_set_gerror(min, error, "SymLink", sfile);
	g_object_unref(sfile);

	return ret;
}

/* 1999-05-30 -	Free the link GUI when the generic module is done with it. */
static void link_free(gpointer user)
{
	SleInfo	*sle = user;

	gtk_widget_destroy(sle->vbox);
	sle->vbox = NULL;
}

/* 1999-05-30 -	User clicked the "details" button for setting link target from file selector. Pop it up. */
static void evt_pick_clicked(GtkWidget *wid, gpointer user)
{
	const gchar	*text;
	SleInfo		*sle = user;
	GtkWidget	*fsel;
	gint		resp;

	fsel = gtk_file_chooser_dialog_new(_("Select Link Target"), NULL, GTK_FILE_CHOOSER_ACTION_SAVE, _("OK"), GTK_RESPONSE_OK, _("Cancel"), GTK_RESPONSE_CANCEL, NULL);
	text = gtk_entry_get_text(GTK_ENTRY(sle->contents));
	if(text != NULL && *text != '\0')
	{
		/* Absolute-looking? */
		if(strchr(text, G_DIR_SEPARATOR) != NULL)
			gtk_file_chooser_set_uri(GTK_FILE_CHOOSER(fsel), text);
		else
		{
			GFile	*child = g_file_get_child_for_display_name(sle->src->dir.root, text, NULL);

			/* Try to find the actual target file in the same directory as the link, and move the chooser there. */
			if(child != NULL)
			{
				gchar	*uri = g_file_get_uri(child);

				gtk_file_chooser_set_uri(GTK_FILE_CHOOSER(fsel), uri);
				g_free(uri);
				g_object_unref(child);
			}
		}
	}
	resp = gtk_dialog_run(GTK_DIALOG(fsel));
	gtk_widget_hide(fsel);
	if(resp == GTK_RESPONSE_OK)
	{
		gchar	*sel = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fsel));

		if(sel != NULL)
		{
			gtk_entry_set_text(GTK_ENTRY(sle->contents), sel);
			g_free(sel);
		}
	}
	gtk_widget_destroy(fsel);
}

/* 1999-05-30 -	Edit all selected symbolic links one at a time, or, if there is no selection,
**		create a new link letting the user type both source and destination names.
*/
gint cmd_symlinkedit(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	static SleInfo	sle;
	GtkWidget	*label, *grid, *pick;
	Dialog		*dlg;
	gboolean	ok = FALSE;

	sle.min = min;
	sle.src = src;

	sle.vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	grid = gtk_grid_new();
	label = gtk_label_new(_("Name"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
	sle.name = gui_dialog_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(sle.name), FILENAME_MAX - 1);
	gtk_widget_set_hexpand(sle.name, TRUE);
	gtk_widget_set_halign(sle.name, GTK_ALIGN_FILL);
	gtk_grid_attach(GTK_GRID(grid), sle.name, 1, 0, 2, 1);
	label = gtk_label_new(_("Contents"));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
	sle.contents = gui_dialog_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(sle.contents), PATH_MAX - 1);
	gtk_grid_attach(GTK_GRID(grid), sle.contents, 1, 1, 1, 1);
	pick = gui_details_button_new();
	g_signal_connect(G_OBJECT(pick), "clicked", G_CALLBACK(evt_pick_clicked), &sle);
	gtk_grid_attach(GTK_GRID(grid), pick, 2, 1, 1, 1);
	gtk_box_pack_start(GTK_BOX(sle.vbox), grid, TRUE, TRUE, 0);

	if(dp_has_selection(src))
	{
		gtk_editable_set_editable(GTK_EDITABLE(sle.name), FALSE);
		return cmd_generic(min, _("Edit Symbolic Link"), CGF_NOALL | CGF_LINKSONLY, link_body, link_action, link_free, &sle);
	}
	else
	{
		/* No selection to edit, so create new symlink in the source pane. */
		dlg = dlg_dialog_sync_new(sle.vbox, _("Create Symbolic Link"), NULL);
		gtk_widget_grab_focus(sle.name);
		if(dlg_dialog_sync_wait(dlg) == DLG_POSITIVE)
		{
			const gchar	*name, *contents;
			GFile		*sfile;
			OvwRes		ores;
			GError		*error = NULL;

			/* If name contains slashes, treat it as a full URI, otherwise it's local. */
			name = gtk_entry_get_text(GTK_ENTRY(sle.name));
			if(g_utf8_strchr(name, -1, G_DIR_SEPARATOR) != NULL)
				sfile = g_vfs_parse_name(min->vfs.vfs, name);
			else
				sfile = dp_get_file_from_name(src, name);
			contents = gtk_entry_get_text(GTK_ENTRY(sle.contents));
			ovw_overwrite_begin(min, _("\"%s\" Already Exists - Continue With Link?"), 0UL);
			ores = ovw_overwrite_unary_file(src, sfile);
			if(ores == OVW_PROCEED)
				ok = TRUE;
			else if(ores == OVW_PROCEED_FILE || ores == OVW_PROCEED_DIR)
				ok = del_delete_gfile(min, sfile, FALSE, &error);
			else
				ok = FALSE;
			if(ok)
			{
				if((ok = g_file_make_symbolic_link(sfile, contents, NULL, &error)) != FALSE)
					dp_rescan_post_cmd(src);
			}
			else
				err_set_gerror(min, &error, "SymLink", sfile);
			ovw_overwrite_end(min);
		}
		dlg_dialog_sync_destroy(dlg);
	}
	return ok;
}
