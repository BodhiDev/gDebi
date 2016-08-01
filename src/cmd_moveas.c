/*
** 1998-09-12 -	A Move As command. Very useful.
** 1999-03-06 -	Adapted for the new selection/generic/dirrow representations.
** 2010-02-28 -	Ported to GIO and GTK+ 2.0.
*/

#include "gentoo.h"
#include "dirpane.h"
#include "errors.h"
#include "fileutil.h"
#include "guiutil.h"
#include "overwrite.h"
#include "progress.h"
#include "strutil.h"

#include "cmd_move.h"
#include "cmd_generic.h"

#include "cmd_moveas.h"

#define	CMD_ID	"move as"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	GtkWidget	*vbox;
	GtkWidget	*label;
	GtkWidget	*entry;
	MainInfo	*min;
	gint		ovw_open;
	GQuark		quark;
} MvaInfo;

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-12 -	Update body of move as GUI. */
static void mva_body(MainInfo *min, DirPane *src, DirRow2 *row, gpointer gen, gpointer user)
{
	gchar	buf[256];
	MvaInfo	*mva = user;

	g_snprintf(buf, sizeof buf, _("Enter Name to Move \"%s\" As"), dp_row_get_name_display(dp_get_tree_model(src), row));
	gtk_label_set_text(GTK_LABEL(mva->label), buf);
	gtk_entry_set_text(GTK_ENTRY(mva->entry), dp_row_get_name_display(dp_get_tree_model(src), row));
	gtk_editable_select_region(GTK_EDITABLE(mva->entry), 0, -1);
	gtk_widget_grab_focus(mva->entry);

	cmd_generic_track_entry(gen, mva->entry);

	if(mva->ovw_open == FALSE)
	{
		ovw_overwrite_begin(mva->min, _("\"%s\" Already Exists - Continue With Move?"), 0U);
		mva->ovw_open = TRUE;
	}
}

static gint mva_action(MainInfo *min, DirPane *src, DirPane *dst, DirRow2 *row, GError **error, gpointer user)
{
	const gchar	*text;
	MvaInfo		*mva = user;
	GFile		*file_src, *file_dst;
	gboolean	ret = FALSE;

	text = gtk_entry_get_text(GTK_ENTRY(mva->entry));
	if(text == NULL || *text == '\0' || g_utf8_strchr(text, -1, G_DIR_SEPARATOR) != NULL)
	{
		*error = g_error_new(mva->quark, EINVAL, _("Invalid destination name for MoveAs"));
		return 0;
	}

	if((file_src = dp_get_file_from_row(src, row)) != NULL)
	{
		if((file_dst = dp_get_file_from_name_display(dst, text)) != NULL)
		{
			pgs_progress_begin(min, _("Moving As..."), PFLG_COUNT_RECURSIVE | PFLG_ITEM_VISIBLE | PFLG_BYTE_VISIBLE);
			ret = move_gfile(min, src, dst, file_src, file_dst, TRUE, error);
			pgs_progress_end(min);
			g_object_unref(file_dst);
		}
		g_object_unref(file_src);
	}

	if(ret)
		dp_unselect(src, row);

	return ret;
}

static void mva_free(gpointer user)
{
	MvaInfo	*mva = user;

	if(mva->ovw_open)
		ovw_overwrite_end(mva->min);
}

/* 1998-09-12 -	The move as generic command function. */
gint cmd_moveas(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	static MvaInfo	mva;

	mva.min	     = min;
	mva.ovw_open = FALSE;

	mva.vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	mva.label = gtk_label_new(_("Move As"));
	mva.entry = gui_dialog_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(mva.entry), FILENAME_MAX - 1);
	gtk_box_pack_start(GTK_BOX(mva.vbox), mva.label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mva.vbox), mva.entry, FALSE, FALSE, 0);

	mva.quark = g_quark_from_string(CMD_ID);

	return cmd_generic(min, _("Move As"), CGF_NOALL, mva_body, mva_action, mva_free, &mva);
}
