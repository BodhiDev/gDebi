/*
** 1998-05-29 -	A rename command might be useful. Since writing this also caused me
**		to implement the generic command interface in cmd_generic, it was
**		very useful indeed!
** 1998-09-18 -	Did some pretty massive changes/additions to provide overwrite protection.
** 1999-01-30 -	Bug fix: always called ovw_overwrite_end() in closing, regardless of
**		whether the _begin() function was ever called. This caused nesting errors.
** 1999-02-23 -	Um... That "bug fix" was buggy. I think I fixed it this time, though.
** 1999-03-05 -	Altered to comply with new selection handling (and its changes on the
**		generic command invocation parameters).
*/

#include "gentoo.h"
#include "cmd_delete.h"
#include "cmdseq_config.h"
#include "dirpane.h"
#include "errors.h"
#include "guiutil.h"
#include "overwrite.h"

#include "cmd_generic.h"
#include "cmd_rename.h"

#include <gdk/gdkkeysyms.h>

#define	CMD_ID	"rename"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	GtkWidget	*vbox;
	GtkWidget	*label;
	GtkWidget	*entry;
	MainInfo	*min;
	gboolean	ovw_open;

	/* These are only used when in-place rename is in progress. */
	DirPane		*inplace_pane;
	GError		*inplace_error;
	gboolean	inplace_success;
} RenInfo;

typedef enum {
	PRESELECT_NONE = 0,
	PRESELECT_FULL,
	PRESELECT_NAME_ONLY,
	PRESELECT_EXTENSION_ONLY
} RenamePreselect;

typedef struct {			/* Options used by the "Rename" command. */
	gboolean	modified;
	gboolean	single_in_place;
	RenamePreselect	preselect;
} OptRename;

static OptRename	rename_options;
static CmdCfg		*rename_cmc = NULL;

/* ----------------------------------------------------------------------------------------- */

/* Guess (!) which preselection mode is active in the given editable. */
static RenamePreselect guess_preselect(GtkEditable *editable)
{
	gint	start, end;

	if(gtk_editable_get_selection_bounds(editable, &start, &end))
	{
		/* We have a selection; grab the characters and inspect. */
		gchar	*chars = gtk_editable_get_chars(editable, 0, -1);

		if(chars != NULL)
		{
			const glong	len = g_utf8_strlen(chars, -1);
			RenamePreselect	ret = PRESELECT_NONE;
			
			/* Entire string selected? */
			if(start == 0 && end == len)
				ret = PRESELECT_FULL;
			/* Anchored to beginning, ends exactly with period? */
			else if(start == 0 && end < len && *g_utf8_offset_to_pointer(chars, end) == '.')
				ret = PRESELECT_NAME_ONLY;
			/* Anchored to end, starts right after period? */
			else if(end == len && start > 0 && *g_utf8_offset_to_pointer(chars, start - 1) == '.')
				ret = PRESELECT_EXTENSION_ONLY;
			g_free(chars);

			return ret;
		}
	}
	return PRESELECT_NONE;
}

/* Apply the indicated pre-selection. */
static void apply_preselect(GtkEditable *editable, const gchar *name, RenamePreselect mode)
{
	gpointer	dot_ptr;
	gulong		dot_pos;

	switch(mode)
	{
		case PRESELECT_NONE:
			/* Rather unexpectedly, we must do this or else GTK+ selects all. */
			gtk_editable_select_region(editable, 0, 0);
			break;
		case PRESELECT_FULL:
			gtk_editable_select_region(editable, 0, -1);
			break;
		case PRESELECT_NAME_ONLY:
		case PRESELECT_EXTENSION_ONLY:
			if((dot_ptr = g_utf8_strchr(name, -1, '.')) != NULL)
			{
				dot_pos = g_utf8_pointer_to_offset(name, dot_ptr);
				if(mode == PRESELECT_NAME_ONLY)
					gtk_editable_select_region(editable, 0, dot_pos);
				else
					gtk_editable_select_region(editable, dot_pos + 1, -1);
			}
			break;
	}
}

/* Keypress handler for whatever widget hosts the renaming. */
static gboolean evt_rename_keypress(GtkWidget *wid, GdkEvent *evt, gpointer user)
{
	const GdkEventKey	*ke = &evt->key;

	/* Hard-coded (control-period) shortcut to cycle selection. Pretty cool. */
	if(ke->type == GDK_KEY_PRESS && ke->state & GDK_CONTROL_MASK && ke->keyval == GDK_KEY_period)
	{
		const RenamePreselect	mode = guess_preselect(GTK_EDITABLE(wid));
		RenamePreselect		new_mode;
		gchar			*chars;

		if(mode == PRESELECT_EXTENSION_ONLY)
			new_mode = PRESELECT_NONE;
		else
			new_mode = mode + 1;

		/* Slightly iffy to re-allocate for the chars, but think of the frequency, here. */
		if((chars = gtk_editable_get_chars(GTK_EDITABLE(wid), 0, -1)) != NULL)
		{
			apply_preselect(GTK_EDITABLE(wid), chars, new_mode);
			g_free(chars);
		}
		return TRUE;
	}
	return FALSE;
}

/* Apply the configured preselection-mode. */
static void init_preselect(GtkEditable *editable, const gchar *name, RenamePreselect mode)
{
	g_signal_connect(G_OBJECT(editable), "key_press_event", G_CALLBACK(evt_rename_keypress), NULL);
	apply_preselect(editable, name, mode);
}

static void ren_body(MainInfo *min, DirPane *src, DirRow2 *row, gpointer gen, gpointer user)
{
	gchar		temp[PATH_MAX + 256];
	const gchar	*name;
	RenInfo		*ren = user;

	name = dp_row_get_name_display(dp_get_tree_model(src), row);
	g_snprintf(temp, sizeof temp, _("Enter New Name For \"%s\""), name);
	gtk_label_set_text(GTK_LABEL(ren->label), temp);
	gtk_entry_set_text(GTK_ENTRY(ren->entry), name);
	gtk_widget_grab_focus(ren->entry);
	init_preselect(GTK_EDITABLE(ren->entry), name, rename_options.preselect);

	cmd_generic_track_entry(gen, ren->entry);

	if(!ren->ovw_open)
	{
		ovw_overwrite_begin(ren->min, _("\"%s\" Already Exists - Proceed With Rename?"), 0U);
		ren->ovw_open = TRUE;
	}
}

static gboolean do_rename(DirPane *src, const DirRow2 *row, const gchar *new_name, GError **error)
{
	GFile	*file;

	if((file = dp_get_file_from_row(src, row)) != NULL)
	{
		GFile	*nf;

		nf = g_file_set_display_name(file, new_name, NULL, error);
		if(nf != NULL)
			g_object_unref(nf);
		g_object_unref(file);

		return nf != NULL;
	}
	return FALSE;
}

static gint ren_action(MainInfo *min, DirPane *src, DirPane *dst, DirRow2 *row, GError **error, gpointer user)
{
	RenInfo		*ren = user;
	const gchar	*old_name, *new_name;
	GFile		*df;
	gboolean	ok;

	old_name = dp_row_get_name(dp_get_tree_model(src), row);
	new_name = gtk_entry_get_text(GTK_ENTRY(ren->entry));

	/* Ignore renames to self. */
	if(strcmp(old_name, new_name) == 0)
	{
		dp_unselect(src, row);
		return 1;
	}

	err_clear(min);

	/* Check for overwrite, and attempt to clear the way. */
	switch(ovw_overwrite_unary_name(src, new_name))
	{
	case OVW_SKIP:
		return 1;
	case OVW_CANCEL:
		return 0;
	case OVW_PROCEED:
		break;
	case OVW_PROCEED_FILE:
	case OVW_PROCEED_DIR:
		df = dp_get_file_from_name(src, new_name);
		if(df == NULL)
			return 0;
		ok = del_delete_gfile(min, df, FALSE, error);
		g_object_unref(df);
		if(!ok)
			return 0;
		break;
	}

	/* All is well, open the file and do the rename. */
	return do_rename(src, row, new_name, error);
}

static void ren_free(gpointer user)
{
	if(((RenInfo *) user)->ovw_open)
		ovw_overwrite_end(((RenInfo *) user)->min);
}

/* ----------------------------------------------------------------------------------------- */

/* 2011-06-29 -	Cell editing started. We would like to just call do_preselect() here, but GTK+ seems to
**		also have ideas about the selection of the newly editing-enabled cell renderer. So we just
**		sneakily buffer the pointer, and hack it later. This is not pretty, but it seems to work.
*/
static void evt_cell_editing_started(GtkCellRenderer *cr, GtkCellEditable *editable, gchar *path, gpointer user)
{
	if(GTK_IS_EDITABLE(editable))
		*(GtkEditable **) user = GTK_EDITABLE(editable);
}

/* 2011-01-03 -	When editing ends, try to do the rename. */
static void evt_cell_edited(GtkCellRendererText *renderer, gchar *path, gchar *new_text, gpointer user)
{
	RenInfo		*ri = user;
	GtkTreeIter	row;

	if(new_text != NULL && *new_text != '\0')
	{
		if(gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(ri->inplace_pane->dir.store), &row, path))
		{
			ri->inplace_success = do_rename(ri->inplace_pane, &row, new_text, &ri->inplace_error);
			if(!ri->inplace_success)
			{
				/* No generic framework, we need to do this ourselves. */
				err_set_gerror(ri->min, &ri->inplace_error, NULL, NULL);	/* Doesn't include file information, but whatever. */
			}
		}
	}
	gtk_main_quit();
}

/* 2011-01-03 -	Pick up editing cancellation, and exit the recursive GTK+ main loop. */
static void evt_cell_editing_canceled(GtkCellRenderer *cr, gpointer user)
{
	gtk_main_quit();
}

gint cmd_rename(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	static RenInfo	ri;

	if(rename_options.single_in_place && dp_has_single_selection(src))	/* In-place? */
	{
		gsize			i;
		GtkTreeViewColumn	*col = NULL;
		GSList			*sel;
		GtkTreePath		*path;
		GList			*cells;
		GtkCellRenderer		*cr;
		GtkEditable		*editable = NULL;

		ri.inplace_pane = src;
		ri.inplace_error = NULL;
		ri.inplace_success = FALSE;

		/* Find the (first) column that displays the name, if any. If there are several, you lose. */
		for(i = 0; i < min->cfg.dp_format[src->index].num_columns; i++)
		{
			if(min->cfg.dp_format[src->index].format[i].content == DPC_NAME)
			{
				col = gtk_tree_view_get_column(GTK_TREE_VIEW(src->view), i);
				break;
			}
		}
		if(col == NULL)
			return 0;

		/* Get the cell renderer, so we can connect a signal handler. */
		cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(col));
		if(cells == NULL)
			return 0;
		cr = cells->data;
		g_list_free(cells);

		/* Make name's renderer editable, this *must* be done last-minute or it breaks double-clicking. */
		g_object_set(G_OBJECT(cr), "mode", GTK_CELL_RENDERER_MODE_EDITABLE, "editable", TRUE, "editable-set", TRUE, NULL);

		/* Make sure the signal handler is connected, but only once. */
		g_signal_handlers_disconnect_by_func(G_OBJECT(cr), G_CALLBACK(evt_cell_editing_started), &ri);
		g_signal_connect(G_OBJECT(cr), "editing-started", G_CALLBACK(evt_cell_editing_started), &editable);
		g_signal_handlers_disconnect_by_func(G_OBJECT(cr), G_CALLBACK(evt_cell_edited), &ri);
		g_signal_connect(G_OBJECT(cr), "edited", G_CALLBACK(evt_cell_edited), &ri);
		g_signal_handlers_disconnect_by_func(G_OBJECT(cr), G_CALLBACK(evt_cell_editing_canceled), &ri);
		g_signal_connect(G_OBJECT(cr), "editing-canceled", G_CALLBACK(evt_cell_editing_canceled), &ri);

		/* Get the path, and activate cell editing. */
		sel = dp_get_selection(src);
		if((path = gtk_tree_model_get_path(GTK_TREE_MODEL(src->dir.store), sel->data)) != NULL)
		{
			gtk_tree_view_set_cursor_on_cell(GTK_TREE_VIEW(src->view), path, col, NULL, TRUE);
			gtk_tree_path_free(path);

			/* Detach the keyboard acceleration context to prevent clashes. */
			kbd_context_detach(min->gui->kbd_ctx, GTK_WINDOW(min->gui->window));

			/* This got set by the the evt_cell_editing_started() callback, now apply preselection. */
			if(editable != NULL)
			{
				gchar	*text;

				if((text = gtk_editable_get_chars(GTK_EDITABLE(editable), 0, -1)) != NULL)
				{
					init_preselect(GTK_EDITABLE(editable), text, rename_options.preselect);
					g_free(text);
				}
			}
			/* Recurse, so we keep Rename fully synchronous. */
			gtk_main();

			/* Editing is done, remove the editability. */
			g_object_set(G_OBJECT(cr), "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE, "editable", FALSE, "editable-set", FALSE, NULL);

			/* Re-attach keyboard modifiers. */
			kbd_context_attach(min->gui->kbd_ctx, GTK_WINDOW(min->gui->window));
		}
		dp_free_selection(sel);
		return ri.inplace_success;
	}
	else	/* Do the classical generic-based dialog rename. */
	{
		ri.ovw_open = FALSE;
		ri.vbox	    = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		ri.label    = gtk_label_new("");
		ri.entry    = gui_dialog_entry_new();
		gtk_entry_set_max_length(GTK_ENTRY(ri.entry), MAXNAMLEN - 1);
		gtk_box_pack_start(GTK_BOX(ri.vbox), ri.label, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(ri.vbox), ri.entry, FALSE, FALSE, 0);

		return cmd_generic(min, _("Rename"), CGF_NOALL | CGF_NODST, ren_body, ren_action, ren_free, &ri);
	}
}

/* ----------------------------------------------------------------------------------------- */

void cfg_rename(MainInfo *min)
{
	if(rename_cmc == NULL)
	{
		/* Set the default values for module's options. */
		rename_options.modified = FALSE;
		rename_options.single_in_place = FALSE;
		rename_options.preselect = PRESELECT_FULL;

		rename_cmc = cmc_config_new("Rename", &rename_options);
		cmc_field_add_boolean(rename_cmc, "modified", NULL, offsetof(OptRename, modified));
		cmc_field_add_boolean(rename_cmc, "inplace", _("Rename Single File In-Place (Without Dialog)?"), offsetof(OptRename, single_in_place));
		cmc_field_add_enum(rename_cmc, "preselect", _("Automatically Pre-Select"), offsetof(OptRename, preselect), _("None|Entire Name|Filename Only|Extension Only"));
		cmc_config_register(rename_cmc);
	}
}
