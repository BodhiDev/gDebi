/*
** 1998-09-17 -	A module to deal with the (possible) overwriting of files, and warn the user
**		when that is about to happen. Deleting a file is considered to be an overwrite.
**		Changing protection bits and owner info is not.
** 1999-03-13 -	Changes for the new dialog module.
** 1999-06-12 -	Added "Skip All" button.
** 2001-01-01 -	Added simplistic (and most likely insufficient) recursion-detection.
** 2003-11-23 -	Now tells caller if collision was with directory or file.
*/

#include "gentoo.h"
#include "dialog.h"
#include "dirpane.h"
#include "errors.h"
#include "strutil.h"
#include "fileutil.h"
#include "overwrite.h"

/* ----------------------------------------------------------------------------------------- */

static struct {
	guint		level;
	gboolean	do_all;		/* Gets set when user clicks "All". */
	gboolean	skip_all;	/* Gets set when user clicks "Skip All". */
	MainInfo	*min;
	const gchar	*fmt;
	guint32		flags;
	GtkWidget	*label;
	Dialog		*dlg;
} ovw_info = { 0U };		/* Makes sure <level> is 0 on first call. */

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-17 -	Begin a "session" with overwrite protection. The <fmt> string is used in a
**		call to sprintf() with the destination file as argument if we need to inform
**		the user. Very handy. Calls to this function REALLY should nest with calls to
**		ovw_overwrite_end()! Or else.
*/
void ovw_overwrite_begin(MainInfo *min, const gchar *fmt, guint32 flags)
{
	if(ovw_info.level == 0)
	{
		ovw_info.do_all   = FALSE;
		ovw_info.skip_all = FALSE;
		ovw_info.min = min;
		ovw_info.fmt = fmt;
		ovw_info.flags = flags;
		ovw_info.label = gtk_label_new("");
		ovw_info.dlg = dlg_dialog_sync_new(ovw_info.label, _("Please Confirm"), _("_OK|A_ll|_Skip|Skip _All|_Cancel"));
	}
	else
		fprintf(stderr, "OVERWRITE: Mismatched call (level=%d)!\n", ovw_info.level);
	ovw_info.level++;
}

/* ----------------------------------------------------------------------------------------- */

static OvwRes proceed_file(GFileInfo *fi)
{
	return g_file_info_get_file_type(fi) == G_FILE_TYPE_DIRECTORY ? OVW_PROCEED_DIR : OVW_PROCEED_FILE;
}

OvwRes ovw_overwrite_unary_file(DirPane *dst, const GFile *dst_file)
{
	GFileInfo	*fi;
	OvwRes		res = OVW_PROCEED;

	/* If overwrite-checking has not even begun, abort abort. */
	if(ovw_info.level == 0)
		return OVW_CANCEL;

	if(dst_file == NULL)
		return OVW_PROCEED;

	if((fi = g_file_query_info((GFile *) dst_file, "standard::*", 0, NULL, NULL)) != NULL)
	{
		gchar		buf[PATH_MAX + 2048], *dn;
		gint		dlg_res;

		if(ovw_info.do_all)			/* Already answered "All"? */
			res = proceed_file(fi);
		else if(ovw_info.skip_all)		/* Already answered "Skip All"? */
			res = OVW_SKIP;
		else
		{
			dn = g_file_get_parse_name((GFile *) dst_file);
			g_snprintf(buf, sizeof buf, ovw_info.fmt, dn);
			g_free(dn);
			gtk_label_set_text(GTK_LABEL(ovw_info.label), buf);

			dlg_res = dlg_dialog_sync_wait(ovw_info.dlg);
			switch(dlg_res)
			{
			case DLG_POSITIVE:	/* OK ? */
				res = proceed_file(fi);
				break;
			case 1:			/* All? */
				ovw_info.do_all = TRUE;
				res = proceed_file(fi);
				break;
			case 2:			/* Skip? */
				res = OVW_SKIP;
				break;
			case 3:
				ovw_info.skip_all = TRUE;
				res = OVW_SKIP;
				break;
			default:
				res = OVW_CANCEL;
			}
		}
		g_object_unref(fi);

		return res;
	}
	return OVW_PROCEED;
}

OvwRes ovw_overwrite_unary_name(DirPane *dst, const gchar *dst_name)
{
	OvwRes	res = OVW_PROCEED;
	GFile	*file;

	if((file = dp_get_file_from_name(dst, dst_name)) != NULL)
	{
		res = ovw_overwrite_unary_file(dst, file);
		g_object_unref(file);
	}
	return res;
}

/* ----------------------------------------------------------------------------------------- */

void ovw_overwrite_end(MainInfo *min)
{
	if(--ovw_info.level == 0)
	{
		ovw_info.min = NULL;
		ovw_info.fmt = NULL;
		dlg_dialog_sync_destroy(ovw_info.dlg);
		ovw_info.dlg = NULL;
	}
}
