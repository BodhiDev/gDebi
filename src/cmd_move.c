/*
** 1998-05-30 -	Implementation of the built-in MOVE command, which is used to, er, move
**		files and directories. It might become incredibly involved. We'll see
**		about that.
** 1998-09-12 -	Did changes in command arguments to ease implementation of MOVEAS, also
**		exported all the move_XXX() functions, as the cmd_copy module does.
** 1999-03-06 -	Adapted for new selection/dirrow representations.
*/

#include "gentoo.h"

#include "cmd_copy.h"
#include "cmd_delete.h"
#include "dialog.h"
#include "dirpane.h"
#include "errors.h"
#include "fileutil.h"
#include "overwrite.h"
#include "progress.h"

#include "cmd_move.h"

#define	CMD_ID	"move"

/* ----------------------------------------------------------------------------------------- */

static void cb_progress(goffset pos, goffset total, gpointer user)
{
	pgs_progress_item_update(user, pos);
}

static gboolean move_gfile_directory(MainInfo *min, DirPane *src, DirPane *dst, const GFile *from, const GFile *to, gboolean progress, GError **err)
{
	GFileEnumerator	*fe;
	GFileInfo	*fi;
	gboolean	ok = TRUE;

	/* First create the destination directory. It must be possible without overwriting
	** anything, gentoo's copy semantics do not allow merge on move.
	*/
	if(!g_file_make_directory((GFile *) to, NULL, err))
		return FALSE;

	/* Now iterate over the children of the source, and move each file contained therein. */
	if((fe = g_file_enumerate_children((GFile *) from, "standard::*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err)) == NULL)
		return FALSE;
	while(ok && (fi = g_file_enumerator_next_file(fe, NULL, err)) != NULL)
	{
		GFile	*sfile, *dfile;

		ok = FALSE;
		if((sfile = g_file_get_child((GFile *) from, g_file_info_get_name(fi))) != NULL)
		{
			if((dfile = g_file_get_child((GFile *) to, g_file_info_get_name(fi))) != NULL)
			{
				ok = move_gfile(min, src, dst, sfile, dfile, progress, err);
				g_object_unref(dfile);
			}
			g_object_unref(sfile);
		}
		g_object_unref(fi);
	}
	g_object_unref(fe);

	return ok;
}

gboolean move_gfile(MainInfo *min, DirPane *src, DirPane *dst, const GFile *sfile, const GFile *dfile, gboolean progress, GError **err)
{
	GFileInfo	*fi;
	gboolean	ok;

	if(src == NULL || dst == NULL || sfile == NULL || dfile == NULL)
		return FALSE;

	switch(ovw_overwrite_unary_file(dst, dfile))
	{
	case OVW_SKIP:
		return TRUE;
	case OVW_CANCEL:
		return FALSE;
	case OVW_PROCEED:
		break;
	case OVW_PROCEED_FILE:
	case OVW_PROCEED_DIR:
		if(!del_delete_gfile(min, (GFile *) dfile, FALSE, err))
			return FALSE;
	}
	if((fi = g_file_query_info((GFile *) sfile, "standard::*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err)) == NULL)
		return FALSE;
	/* First try a native move. This is so fast, that we don't want to spend time recursing
	** to figure out the true size of directories. Instead, use the directory entry's size.
	*/
	pgs_progress_item_begin(min, g_file_info_get_display_name(fi), g_file_info_get_size(fi));
	g_object_unref(fi);

	ok = g_file_move((GFile *) sfile, (GFile *) dfile, G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_NO_FALLBACK_FOR_MOVE, pgs_progress_get_cancellable(), cb_progress, min, err);
	if(!ok && !(err != NULL && g_error_matches(*err, G_IO_ERROR, G_IO_ERROR_CANCELLED)))
	{
		guint64	size = 0;

		/* Initial native-only attempt failed, try again with the fallback enabled.
		** At this point we first need to figure out the size, for the progress reporting.
		*/
		if(fut_size_gfile(min, sfile, &size, NULL, err))
		{
			g_clear_error(err);
			pgs_progress_item_resize(min, size);
			ok = g_file_move((GFile *) sfile, (GFile *) dfile, G_FILE_COPY_NOFOLLOW_SYMLINKS, pgs_progress_get_cancellable(), cb_progress, min, err);
			/* If it failed, check if it was because it "would recurse". If so, then we need to
			** implement a fall-back, since GIO doesn't do that for us. Luckily, this whole
			** program is supposed to be a _file manager_, so it's right up our alley.
			*/
			if(!ok && err != NULL && g_error_matches(*err, G_IO_ERROR, G_IO_ERROR_WOULD_RECURSE))
			{
				/* Drop the GError, since we've handled it and it was semi-expected. */
				g_clear_error(err);
				ok = move_gfile_directory(min, src, dst, sfile, dfile, progress, err);
			}
		}
	}
	pgs_progress_item_end(min);

	return ok;
}

/* ----------------------------------------------------------------------------------------- */

/* 2003-11-24 -	Move selected files and/or directories to destination pane's directory. */
gint cmd_move(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	GSList		*slist, *iter;
	GError		*error = NULL;
	GFile		*sfile, *dfile;
	gboolean	ok = TRUE;
	guint		count = 0;

	if((min == NULL) || (src == NULL) || (dst == NULL))
		return 0;
	if((slist = dp_get_selection(src)) == NULL)
		return 1;

	err_clear(min);
	ovw_overwrite_begin(min, _("\"%s\" Already Exists - Continue With Move?"), 0UL);
	pgs_progress_begin(min, _("Moving..."), PFLG_ITEM_VISIBLE | PFLG_BYTE_VISIBLE);
	for(iter = slist; ok && (iter != NULL); iter = g_slist_next(iter))
	{
		sfile = dp_get_file_from_row(src, iter->data);
		dfile = dp_get_file_from_name(dst, dp_row_get_name(dp_get_tree_model(src), iter->data));
		ok = move_gfile(min, src, dst, sfile, dfile, TRUE, &error);
		if(!ok)
			err_set_gerror(min, &error, CMD_ID, sfile);
		count += (ok != FALSE);
		g_object_unref(dfile);
		g_object_unref(sfile);
	}
	if(count > 0)
	{
		dp_rescan_post_cmd(src);
		dp_rescan_post_cmd(dst);
	}
	pgs_progress_end(min);
	ovw_overwrite_end(min);
	dp_free_selection(slist);

	return ok;
}
