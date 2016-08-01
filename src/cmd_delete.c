/*
** 1998-05-21 -	Everybody's favourite, the native delete command. Dangerous stuff if you don't
**		quite know what you're doing!
** 1998-05-29 -	Converted to use the new cmd_generic module, looks a lot better and actually IS
**		better, too. Gives user more control.
** 1998-07-28 -	Turned on the NODST flag in the cmd_generic call. Really should have done that
**		earlier! Hm. I guess I need more beta testing.
** 1998-09-18 -	Rewrote large parts of this one. Now uses the standard overwrite protection/
**		confirmation module, rather than rolling its own. No longer uses the generic
**		command interface.
** 1998-12-15 -	Commented out the progress reporting, since it didn't work well.
** 1999-03-06 -	Adjusted for the new selection/dirrow access methods.
** 1999-05-29 -	Restructured. Now exports two utility functions handy to use from elsewhere.
*/

#include "gentoo.h"
#include "cmdseq_config.h"
#include "configure.h"
#include "dialog.h"
#include "dirpane.h"
#include "errors.h"
#include "fileutil.h"
#include "gfam.h"
#include "overwrite.h"
#include "progress.h"

#include "cmd_delete.h"

#define	CMD_ID	"delete"

/* ----------------------------------------------------------------------------------------- */

enum { MODE_ASK = 0, MODE_SET, MODE_DONT_SET };

typedef struct {			/* Options used by the "Delete" command. */
	gboolean	modified;
	gint		set_mode;
} OptDelete;

static OptDelete	delete_options;
static CmdCfg		*delete_cmc = NULL;

/* ----------------------------------------------------------------------------------------- */

#if 0
static gboolean get_set_mode(MainInfo *min, const gchar *filename)
{
	if(delete_options.set_mode == MODE_ASK)
	{
		gchar	buf[PATH_MAX + 256];
		GtkWidget	*vbox, *label, *check;
		Dialog	*dlg;
		gint	ans;

		vbox  = gtk_vbox_new(FALSE, 0);
		g_snprintf(buf, sizeof buf, _("%s\ncould not be deleted due to access restrictions.\nAttempt to change protection and retry?"), filename);
		label = gtk_label_new(buf);
		gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
		check = gtk_check_button_new_with_label(_("Remember the answer (alters config)"));
		gtk_box_pack_start(GTK_BOX(vbox), check, FALSE, FALSE, 0);
		dlg = dlg_dialog_sync_new(vbox, _("Access Problem"), _("Change|Leave Alone"));
		ans = dlg_dialog_sync_wait(dlg);

		if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)))
		{
			gint	arm;
			if(ans == DLG_POSITIVE)
				arm = MODE_SET;
			else
				arm = MODE_DONT_SET;
			if(delete_options.set_mode != arm)
			{
				delete_options.set_mode = arm;
				delete_options.modified = TRUE;
				cfg_modified_set(min);
			}
		}
		dlg_dialog_sync_destroy(dlg);
		return ans == DLG_POSITIVE;
	}
	return delete_options.set_mode == MODE_SET;
}

/* 2003-10-23 -	Set modes of <filename> to something that really should help with deleting.
**		The file itself must be writable, and for a directory executable, and the
**		parent must be writable. Feels a bit dirty.
*/
static gboolean set_mode(MainInfo *min, const gchar *filename, gboolean dir)
{
	if(get_set_mode(min, filename))
	{
		/* First make actual target writable, and executable if directory. */
		if(chmod(filename, S_IRUSR | S_IWUSR | (dir ? S_IXUSR : 0)) == 0)
		{
			gchar	parent[PATH_MAX];

			/* Now, compute parent filename, and see if it's a directory. */
			if(fut_path_canonicalize(filename, parent, sizeof parent))
			{
				gchar	*sep;

				if((sep = strrchr(parent, G_DIR_SEPARATOR)) != NULL)
				{
					struct stat	pstat;

					*sep = '\0';
					if(lstat(parent, &pstat) == 0)
					{
						if(S_ISDIR(pstat.st_mode))
							return chmod(parent, pstat.st_mode | S_IWUSR) == 0;
					}
				}
			}
		}
	}
	return FALSE;
}
#endif

/* ----------------------------------------------------------------------------------------- */

static gboolean delete_gfile_dir(MainInfo *min, GFile *dir, gboolean progress, GError **err)
{
	gboolean	ret = TRUE;
	GFileEnumerator	*fe;

	if((fe = g_file_enumerate_children(dir, "standard::*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err)) != NULL)
	{
		GFileInfo	*fi;

		while(ret && (fi = g_file_enumerator_next_file(fe, NULL, err)) != NULL)
		{
			GFile	*child;

			child = g_file_get_child(dir, g_file_info_get_name(fi));
			if(child != NULL)
			{
				switch(g_file_info_get_file_type(fi))
				{
				case G_FILE_TYPE_REGULAR:
				case G_FILE_TYPE_SYMBOLIC_LINK:
				case G_FILE_TYPE_SPECIAL:
				case G_FILE_TYPE_SHORTCUT:
					ret = g_file_delete(child, NULL, err);
					break;
				case G_FILE_TYPE_DIRECTORY:
					ret = delete_gfile_dir(min, child, progress, err);
					break;
				default:
					ret = FALSE;
				}
				g_object_unref(child);
			}
			else
				g_warning("Couldn't get child file");
			g_object_unref(fi);
		}
		g_object_unref(fe);
	}
	if(ret)
		ret = g_file_delete(dir, NULL, err);
	return ret;
}

/* 2009-09-07 -	Delete a GFile. Takes no prisoners; if the indicated file is a directory,
**		recursively deletes all there is.
**
**		The caller owns whatever ends up in <error>.
*/
gboolean del_delete_gfile(MainInfo *min, const GFile *file, gboolean progress, GError **error)
{
	GFileInfo	*fi;
	gboolean	ret = FALSE;

	if(min == NULL || file == NULL)
		return FALSE;

	if((fi = g_file_query_info((GFile *) file, "standard::*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error)) != NULL)
	{
		switch(g_file_info_get_file_type(fi))
		{
		case G_FILE_TYPE_REGULAR:
		case G_FILE_TYPE_SYMBOLIC_LINK:
		case G_FILE_TYPE_SPECIAL:
		case G_FILE_TYPE_SHORTCUT:
			ret = g_file_delete((GFile *) file, NULL, error);
			break;
		case G_FILE_TYPE_DIRECTORY:
			ret = delete_gfile_dir(min, (GFile *) file, progress, error);
			break;
		default:
			ret = FALSE;
		}
		g_object_unref(fi);
	}
	return ret;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-18 -	A new entrypoint for the delete command. Completely replaces the old one,
**		which has been removed.
*/
gint cmd_delete(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	gboolean	ok = TRUE;
	guint		num = 0;
	GSList		*slist,	 *iter;
	GError		*err = NULL;

	if((slist = dp_get_selection(src)) == NULL)
		return 1;

	err_clear(min);
	ovw_overwrite_begin(min, _("Really Delete \"%s\"?"), 0UL);
	pgs_progress_begin(min, _("Deleting..."), PFLG_BUSY_MODE);
	fam_rescan_block();
	for(iter = slist; ok && (iter != NULL); iter = g_slist_next(iter))
	{
		DirRow2	*row = iter->data;
		GFile	*file;

		if((file = dp_get_file_from_row(src, iter->data)) != NULL)
		{
			gchar	*pn = g_file_get_parse_name(file);

			pgs_progress_item_begin(min, pn, 0);
			g_free(pn);
			if(pgs_progress_item_update(min, 0) == PGS_PROCEED)
			{
				pgs_progress_item_end(min);
				switch(ovw_overwrite_unary_file(src, file))
				{
				case OVW_SKIP:
					break;
				case OVW_CANCEL:
					ok = FALSE;
					break;
				case OVW_PROCEED:	/* Fall-throughs. */
				case OVW_PROCEED_DIR:
				case OVW_PROCEED_FILE:
					ok = del_delete_gfile(min, file, TRUE, &err);
					if(ok)
					{
						dp_unselect(src, row);
						num++;
					}
					else
						err_set_gerror(min, &err, CMD_ID, file);
					break;
				}
			}
			g_object_unref(file);
		}
	}
	fam_rescan_unblock();
	pgs_progress_end(min);
	ovw_overwrite_end(min);
	if(num)
		dp_rescan_post_cmd(src);
	dp_free_selection(slist);

	return ok;
}

/* ----------------------------------------------------------------------------------------- */

/* 2003-10-23 -	Configuration initialization. Simple. */
void cfg_delete(MainInfo *min)
{
	if(delete_cmc == NULL)
	{
		/* Set the default values for module's options. */
		delete_options.modified	= FALSE;
		delete_options.set_mode = MODE_ASK;

		delete_cmc = cmc_config_new("Delete", &delete_options);
		cmc_field_add_boolean(delete_cmc, "modified", NULL, offsetof(OptDelete, modified));
		cmc_field_add_enum   (delete_cmc, "set_mode", _("On Access Failure"), offsetof(OptDelete, set_mode),
							  _("Ask User|Automatically Try Changing, and Retry|Fail"));
		cmc_config_register(delete_cmc);
	}
}
