/*
** 1998-08-27 -	I've gotten pretty far without a native command for creating directories,
**		but now is the time. I want one!
** 1998-09-01 -	Added the possibility to enter the newly created directory, something I
**		find I want to do occasionally. Having it here saves one click. :^)
** 1999-03-13 -	Adjusted for new dialog module.
** 1999-04-04 -	Moved "CD New" flag out into min->cfg, making it behave a bit more like
**		the other commands and paving the way for saving this info in config file.
** 1999-06-19 -	Adapted for the new dialog module. Cleaner now.
*/

#include "gentoo.h"
#include "cmd_delete.h"
#include "cmdseq.h"
#include "cmdseq_config.h"
#include "configure.h"
#include "dialog.h"
#include "dirpane.h"
#include "errors.h"
#include "gfam.h"
#include "guiutil.h"
#include "overwrite.h"
#include "strutil.h"

#include "cmd_mkdir.h"

#define	CMD_ID	"mkdir"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	GtkWidget	*vbox;
	GtkWidget	*entry;
	DirPane		*src;
} MkdInfo;

typedef struct {			/* Command-specific configuration data. */
	gboolean	modified;		/* Has the setting(s) been modified? */
	gboolean	cd_new;			/* CD into new directory after creation? */
	gboolean	focus_new;		/* Focus the new directory? */
} OptMkDir;

static OptMkDir	mkdir_options;
static CmdCfg	*mkdir_cmc = NULL;

/* ----------------------------------------------------------------------------------------- */

static gboolean make_dir(const MkdInfo *mdi)
{
	const gchar	*text;
	GFile		*dest;
	gboolean	ok = FALSE;
	GError		*err = NULL;

	if(mdi == NULL)
		return FALSE;
	text = gtk_entry_get_text(GTK_ENTRY(mdi->entry));
	if(*text == '\0')
		return FALSE;
	if(mdi->src->dir.root == NULL)
		return FALSE;

	err_clear(mdi->src->main);
	if((dest = g_file_get_child_for_display_name(mdi->src->dir.root, text, &err)) != NULL)
	{
		ovw_overwrite_begin(mdi->src->main, _("\"%s\" Already Exists - Proceed With MkDir?"), 0UL);
		switch(ovw_overwrite_unary_file(mdi->src, dest))
		{
		case OVW_SKIP:
		case OVW_CANCEL:
			ok = FALSE;
			break;
		case OVW_PROCEED:
			ok = TRUE;
			break;
		case OVW_PROCEED_FILE:
			ok = del_delete_gfile(mdi->src->main, dest, FALSE, &err);
			break;
		case OVW_PROCEED_DIR:
			ok = FALSE;
			break;
		}
		ovw_overwrite_end(mdi->src->main);
		if(ok)
		{
			ok = g_file_make_directory(dest, NULL, &err);
			if(ok)
			{
				gchar	*pn = g_file_get_parse_name(dest);

				if(mkdir_options.cd_new)
					ok = csq_execute_format(mdi->src->main, "DirEnter 'dir=%s'", pn);
				else if(mkdir_options.focus_new)
					ok = csq_execute_format(mdi->src->main, "DpGotoRow focus=%s nocase=no 're=%s'", mkdir_options.focus_new ? "yes" : "no", pn);
				if(!mkdir_options.cd_new)
					dp_rescan_post_cmd(mdi->src);
				g_free(pn);
			}
		}
		g_object_unref(dest);
	}
	if(err != NULL)
		err_set_gerror(mdi->src->main, &err, text, NULL);

	return ok;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-27 -	The mkdir entry point. Simple stuff. */
gint cmd_mkdir(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	Dialog		*dlg;
	MkdInfo		mi;
	GtkWidget	*wid;
	gboolean	ok = FALSE;

	mi.src = src;
	mi.vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	wid = gtk_label_new(_("Enter Name of Directory to Create"));
	gtk_box_pack_start(GTK_BOX(mi.vbox), wid, FALSE, FALSE, 0);
	mi.entry = gui_dialog_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(mi.entry), MAXNAMLEN - 1);
	gtk_box_pack_start(GTK_BOX(mi.vbox), mi.entry, FALSE, FALSE, 0);
	wid = cmc_field_build(mkdir_cmc, "cd_new", &mkdir_options);
	gtk_box_pack_start(GTK_BOX(mi.vbox), wid, FALSE, FALSE, 0);
	wid = cmc_field_build(mkdir_cmc, "focus_new", &mkdir_options);
	gtk_box_pack_start(GTK_BOX(mi.vbox), wid, FALSE, FALSE, 0);
	dlg = dlg_dialog_sync_new(mi.vbox, _("Make Directory"), NULL);
	gtk_widget_grab_focus(mi.entry);
	if(dlg_dialog_sync_wait(dlg) == DLG_POSITIVE)
		ok = make_dir(&mi);
	dlg_dialog_sync_destroy(dlg);

	return ok;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-04 -	Support function for dynamic command configuration management (buzz, buzz). */
void cfg_mkdir(MainInfo *min)
{
	if(mkdir_cmc == NULL)
	{
		/* Initialize default option values. */
		mkdir_options.modified	= FALSE;
		mkdir_options.cd_new	= FALSE;
		mkdir_options.focus_new = FALSE;

		mkdir_cmc = cmc_config_new("MkDir", &mkdir_options);
		cmc_field_add_boolean(mkdir_cmc, "modified", NULL, offsetof(OptMkDir, modified));
		cmc_field_add_boolean(mkdir_cmc, "cd_new", _("CD Into New Directory?"), offsetof(OptMkDir, cd_new));
		cmc_field_add_boolean(mkdir_cmc, "focus_new", _("Focus New Directory?"), offsetof(OptMkDir, focus_new));
		cmc_config_register(mkdir_cmc);
	}
}
