/*
** 1998-09-12 -	This is GETSIZE, a recursive size computation command. It will assign
**		the size of all contained files to directories.
** 1998-12-19 -	Rewritten to use the new fut_dir_size() function, rather than its own
**		routine. Simpler.
** 1999-03-06 -	Adjusted for the new selection handling.
** 1999-04-09 -	Added the cmd_clearsize() function, implementing the ClearSize command.
*/

#include "gentoo.h"

#include <fcntl.h>

#include "fileutil.h"
#include "dirpane.h"
#include "errors.h"
#include "progress.h"
#include "cmdseq_config.h"

#include "cmd_getsize.h"

#define	CMD_ID	"getsize"
#define	CMD_ID2	"clearsize"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	gboolean	modified;
	gboolean	unselect;		/* Unselect rows when done computing size? */
} OptGetSize;

static OptGetSize	getsize_options;
static CmdCfg		*getsize_cmc = NULL;

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-12 -	Recursive directory sizer.
** 1999-04-09 -	Now also updates the blocks field of the directory's stat buffer.
*/
gint cmd_getsize(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	GtkTreeModel	*m = dp_get_tree_model(src);
	GSList		*slist, *iter;
	guint		num = 0;
	FUCount		fuc;
	gboolean	ok = TRUE;
	GError		*err = NULL;

	if((slist = dp_get_selection(src)) == NULL)
		return 1;
	pgs_progress_begin(min, _("Getting sizes..."), PFLG_BUSY_MODE);
	for(iter = slist; ok && (iter != NULL); iter = g_slist_next(iter))
	{
		if(dp_row_get_file_type(m, iter->data, TRUE) == G_FILE_TYPE_DIRECTORY)
		{
			GFile	*here = dp_get_file_from_row(src, iter->data);

			if((ok = fut_size_gfile(min, here, NULL, &fuc, &err)))
			{
				dp_row_set_size(m, iter->data, fuc.num_bytes);
				dp_row_set_flag(m, iter->data, DPRF_HAS_SIZE);
				num++;
				if(getsize_options.unselect)
					dp_unselect(src, iter->data);
			}
			else
				err_set_gerror(min, &err, CMD_ID, here);
		}
	}
	pgs_progress_end(min);
	dp_free_selection(slist);

	if(num)
	{
		dp_update_stats(src);
		dp_show_stats(src);
	}
	return ok;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-09 -	Clear the recursive sizes from all selected directories. Ignores files, but
**		unselects them.
*/
gint cmd_clearsize(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	GtkTreeModel	*m = dp_get_tree_model(src);
	GSList		*slist, *iter;
	guint		num = 0;
	GError		*err = NULL;
	gboolean	ok = TRUE;

	if((slist = dp_get_selection(src)) == NULL)
		return 1;

	for(iter = slist; ok && (iter != NULL); iter = g_slist_next(iter))
	{
		if(dp_row_get_file_type(m, iter->data, TRUE) == G_FILE_TYPE_DIRECTORY && dp_row_get_flags(m, iter->data, DPRF_HAS_SIZE))
		{
			/* Now, we need to re-scan the file's info, and replace it in the model. Interesting, but
			** clearly a nitty job that needs to be done inside the dirpane module, not inline here.
			*/
			ok = dp_rescan_row(src, iter->data, &err);
			if(!ok)
				err_set_gerror(min, &err, CMD_ID2, dp_get_file_from_row(src, iter->data));
			num++;
		}
		if(getsize_options.unselect)
			dp_unselect(src, iter->data);
	}
	dp_free_selection(slist);

	if(num)
	{
		dp_update_stats(src);
		dp_show_stats(src);
	}
	return ok;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-25 -	Configure the GetSize command's (few) options. */
void cfg_getsize(MainInfo *min)
{
	if(getsize_cmc == NULL)
	{
		getsize_options.modified = FALSE;
		getsize_options.unselect = TRUE;

		getsize_cmc = cmc_config_new("GetSize", &getsize_options);
		cmc_field_add_boolean(getsize_cmc, "modified", NULL, offsetof(OptGetSize, modified));
		cmc_field_add_boolean(getsize_cmc, "unselect", _("Unselect Rows When Done?"), offsetof(OptGetSize, unselect));
		cmc_config_register(getsize_cmc);
	}
}
