/*
** 1998-05-21 -	This module implements the built-in command "Copy". Very handy stuff!
** 1998-05-23 -	Added some error handling/reporting. Very far from bullet-proof, though. :(
** 1998-05-31 -	Added capability to copy device files (recreates them at destination using
**		mknod(), of course). Nice.
** 1998-06-04 -	Copied directories now try to have the same protection flags, rather than
**		trying to turn on as many bits as possible (as the old code did!).
** 1998-06-07 -	Added support for copying soft links. Originally wrote the code in cmd_move,
**		then moved it here and made the cmd_move code call it here.
** 1998-09-12 -	Changed all destination arguments to be full (with path and filename). This
**		makes the implementation of cmd_copys SO much easier.
** 1998-09-19 -	Now uses the new overwrite protection/confirmation module. Kinda cool.
** 1998-12-23 -	Now supports copying the source's access and modification dates, too.
** 1999-01-03 -	After complaints, I altered the copying command slightly; it no longer attempts
**		to avoid doing a new stat() call on the file being copied. This assures that
**		the size used for the copy is the most recent one known to the file system (if
**		you pretend there's no other programs running, that is).
** 1999-03-05 -	Moved over to new selection method, and it's semi-abstracted view of dir rows.
** 1999-04-06 -	Modified to use the new command configuration system.
** 2000-07-02 -	Initialized translation by marking up strings.
*/

#include "gentoo.h"

#include <fcntl.h>
#include <utime.h>

#include "cmd_delete.h"
#include "cmdseq_config.h"
#include "dialog.h"
#include "dirpane.h"
#include "errors.h"
#include "fileutil.h"
#include "overwrite.h"
#include "progress.h"
#include "strutil.h"

#include "cmd_copy.h"

#define	CMD_ID	"copy"

/* ----------------------------------------------------------------------------------------- */

typedef struct {			/* Options used by the "Copy" command (and relatives). */
	gboolean	modified;
	gboolean	copy_dates;		/* Copy access and modification dates? */
	gboolean	ignore_attrib_err;	/* Allow attribute-copying to fail silently. */
	gboolean	leave_fullsize;		/* If destination has same size as source, leave it even if error occured. */
	gsize		buf_size;		/* Buffer size. */
} OptCopy;

static OptCopy	copy_options;
static CmdCfg	*copy_cmc = NULL;

/* ----------------------------------------------------------------------------------------- */

/* 2011-10-04 -	Return the buffer size for copying, so others can use it too. */
gsize cpy_get_buf_size(void)
{
	return copy_options.buf_size;
}

/* ----------------------------------------------------------------------------------------- */

static void cb_progress(goffset pos, goffset total, gpointer user)
{
	pgs_progress_item_update(user, pos);
}

static gboolean copy_gfile_regular(MainInfo *min, DirPane *src, DirPane *dst, GFile *from, GFile *to, GError **err)
{
	/* Yeah, time for some seriously heavy lifting. Luckily, not by me. :) */
	return g_file_copy(from, to, G_FILE_COPY_NONE, pgs_progress_get_cancellable(), cb_progress, min, err);
}

static gboolean copy_gfile_symlink(MainInfo *min, DirPane *src, DirPane *dst, GFile *from, GFile *to, GError **err)
{
	/* This is just a copy, so it might be merged with copy_gfile_regular(), above. */
	return g_file_copy(from, to, G_FILE_COPY_NOFOLLOW_SYMLINKS, pgs_progress_get_cancellable(), cb_progress, min, err);
}

static gboolean copy_gfile_special(MainInfo *min, DirPane *src, DirPane *dst, GFile *from, GFile *to, GError **err)
{
	GFileInfo	*fi;
	gchar		*path;
	gboolean	ok = FALSE;

	if((fi = g_file_query_info(from, "standard::*,unix::*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err)) == NULL)
		return FALSE;
	/* It seems current versions of GIO can't ever set the G_FILE_ATTRIBUTE_UNIX_RDEV attribute
	 * so we can't just use that. Might be because of the below code required to do this.
	*/
	if((path = g_file_get_path(to)) != NULL)
	{
		const guint32	mode = g_file_info_get_attribute_uint32(fi, G_FILE_ATTRIBUTE_UNIX_MODE);
		const guint32	rdev = g_file_info_get_attribute_uint32(fi, G_FILE_ATTRIBUTE_UNIX_RDEV);

		/* Manual page for mknod() says never to use it to create FIFOs, so let's not. */
		if(mode & S_IFIFO)
			ok = mkfifo(path, mode) == 0;
		else
			ok = mknod(path, mode, rdev) == 0;

		if(!ok)
		{
			const gchar	*fmt = (mode & S_IFIFO) ? _("Error copying FIFO: %s") : _("Error copying special file: %s");
			g_set_error(err, g_io_error_quark(), g_io_error_from_errno(errno), fmt, strerror(errno));
		}
		g_free(path);
	}
	else
		g_set_error(err, g_io_error_quark(), G_IO_ERROR_FAILED, _("Error copying special file: no local path"));

	g_object_unref(fi);

	return ok;
}

static gboolean copy_gfile_directory(MainInfo *min, DirPane *src, DirPane *dst, GFile *from, GFile *to, GError **err)
{
	GFileEnumerator	*fe;
	GFileInfo	*fi;
	gboolean	ok = TRUE;

	/* First create the destination directory. It must be possible without overwriting
	 * anything, gentoo's copy semantics do not allow merge on copy.
	*/
	if(!g_file_make_directory(to, NULL, err))
		return FALSE;

	/* Now iterate over the children of the source, and copy each file contained therein. */
	if((fe = g_file_enumerate_children(from, "standard::*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err)) == NULL)
		return FALSE;
	while(ok && (fi = g_file_enumerator_next_file(fe, NULL, err)) != NULL)
	{
		GFile	*sfile, *dfile;

		ok = FALSE;
		if((sfile = g_file_get_child(from, g_file_info_get_name(fi))) != NULL)
		{
			if((dfile = g_file_get_child(to, g_file_info_get_name(fi))) != NULL)
			{
				ok = copy_gfile(min, src, dst, sfile, dfile, err);
				g_object_unref(dfile);
			}
			g_object_unref(sfile);
		}
		g_object_unref(fi);
	}
	g_object_unref(fe);

	return ok;
}

/* 2009-09-09 -	Copies a file, using GIO. Neither file is freed. The file could be a directory. */
gboolean copy_gfile(MainInfo *min, DirPane *src, DirPane *dst, GFile *from, GFile *to, GError **err)
{
	GFileInfo	*fi;
	gboolean	ok;

	if(min == NULL || from == NULL || to == NULL)
		return FALSE;

	switch(ovw_overwrite_unary_file(dst, to))
	{
	case OVW_SKIP:
		return TRUE;
	case OVW_CANCEL:
		return FALSE;
	case OVW_PROCEED:
		break;
	case OVW_PROCEED_FILE:
	case OVW_PROCEED_DIR:
		if(!del_delete_gfile(min, to, FALSE, err))
			return FALSE;
	}
	if((fi = g_file_query_info(from, "standard::*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err)) == NULL)
		return FALSE;
	switch(g_file_info_get_file_type(fi))
	{
	case G_FILE_TYPE_REGULAR:
		pgs_progress_item_begin(min, g_file_info_get_display_name(fi), g_file_info_get_size(fi));
		ok = copy_gfile_regular(min, src, dst, from, to, err);
		pgs_progress_item_end(min);
		break;
	case G_FILE_TYPE_DIRECTORY:
		pgs_progress_item_begin(min, g_file_info_get_display_name(fi), g_file_info_get_size(fi));
		ok = copy_gfile_directory(min, src, dst, from, to, err);
		pgs_progress_item_end(min);
		break;
	case G_FILE_TYPE_SYMBOLIC_LINK:
		pgs_progress_item_begin(min, g_file_info_get_display_name(fi), g_file_info_get_size(fi));
		ok = copy_gfile_symlink(min, src, dst, from, to, err);
		pgs_progress_item_end(min);
		break;
	case G_FILE_TYPE_SPECIAL:
		pgs_progress_item_begin(min, g_file_info_get_display_name(fi), 0);
		ok = copy_gfile_special(min, src, dst, from, to, err);
		pgs_progress_item_end(min);
		break;
	default:
		g_error("Not copying '%s', type is %d which is unsupported", g_file_info_get_name(fi), g_file_info_get_file_type(fi));
		ok = FALSE;
	}
	g_object_unref(fi);

	return ok;
}

/* ----------------------------------------------------------------------------------------- */

/* 2003-11-24 -	Copy selected files and/or directories to destination pane's directory. */
gint cmd_copy(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	GSList		*slist, *iter;
	guint		num = 0;
	gboolean	ok = TRUE;
	GError		*err = NULL;

	if((src == NULL) || (dst == NULL))
		return 1;
	if((slist = dp_get_selection(src)) == NULL)
		return 1;

	ovw_overwrite_begin(min, _("\"%s\" Already Exists - Proceed With Copy?"), 0UL);
	pgs_progress_begin(min, _("Copying..."), PFLG_COUNT_RECURSIVE | PFLG_ITEM_VISIBLE | PFLG_BYTE_VISIBLE);
	for(iter = slist; ok && (iter != NULL); iter = g_slist_next(iter))
	{
		GFile	*sf, *df;

		sf = dp_get_file_from_row(src, iter->data);
		df = dp_get_file_from_name(dst, dp_row_get_name(dp_get_tree_model(src), iter->data));
		if((ok = copy_gfile(min, src, dst, sf, df, &err)))
		{
			dp_unselect(src, iter->data);
			num++;
		}
		else
			err_set_gerror(min, &err, CMD_ID, sf);
		g_object_unref(df);
		g_object_unref(sf);
	}
	if(num > 0)
		dp_rescan_post_cmd(dst);
	pgs_progress_end(min);
	ovw_overwrite_end(min);
	dp_free_selection(slist);

	return ok;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-06 -	Configuration initialization. */
void cfg_copy(MainInfo *min)
{
	if(copy_cmc == NULL)
	{
		/* Set the default values for module's options. */
		copy_options.modified	= FALSE;
		copy_options.copy_dates	= TRUE;
		copy_options.leave_fullsize = TRUE;
		copy_options.buf_size	= (1 << 18);

		copy_cmc = cmc_config_new("Copy", &copy_options);
		cmc_field_add_boolean(copy_cmc, "modified", NULL, offsetof(OptCopy, modified));
		cmc_field_add_boolean(copy_cmc, "copy_dates", _("Preserve Dates During Copy?"), offsetof(OptCopy, copy_dates));
		cmc_field_add_boolean(copy_cmc, "ignore_attrib_err", _("Ignore Failure to Copy Attributes (Date, Owner, Mode)?"), offsetof(OptCopy, ignore_attrib_err));
		cmc_field_add_boolean(copy_cmc, "leave_fullsize", _("Leave Failed Destination if Full Size?"), offsetof(OptCopy, leave_fullsize));
		cmc_field_add_size(copy_cmc, "buf_size", _("Buffer Size"), offsetof(OptCopy, buf_size), 1024, (1<<24), 1024);
		cmc_config_register(copy_cmc);
	}
}
