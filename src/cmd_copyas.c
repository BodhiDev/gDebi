/*
** 1998-09-12 -	We must have a Copy As command, since it's quite useful. Reuses all
**		copy_XXX() functions defined in the cmd_copy module, of course. Since
**		it so easy, I just had to let this module implement a Clone command, too!
** 1999-01-03 -	No longer passes the stat-structure from the dir listing to the copy module,
**		thus forcing it to do a re-read, which assures that the info (size) is fresh.
** 1999-03-06 -	Adapted for new selection/generic command handling.
** 1999-05-30 -	Added SymLinkAs and SymLinkClone commands. Nice.
*/

#include "gentoo.h"
#include "dirpane.h"
#include "errors.h"
#include "guiutil.h"
#include "overwrite.h"
#include "progress.h"
#include "strutil.h"
#include "cmd_copy.h"
#include "cmd_delete.h"
#include "cmd_generic.h"

#include "cmd_copyas.h"

#define	CMD_ID	"copy as"

/* ----------------------------------------------------------------------------------------- */

typedef enum { CPA_COPYAS, CPA_CLONE, CPA_SYMLINKAS, CPA_SYMLINKCLONE } CpaAction;

typedef struct {
	GtkWidget	*vbox;
	GtkWidget	*label;
	GtkWidget	*entry;
	CpaAction	action;
	MainInfo	*min;
	gint		ovw_open;
} CpaInfo;

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-12 -	Update body of copy as GUI. */
static void cpa_copy_body(MainInfo *min, DirPane *src, DirRow2 *row, gpointer gen, gpointer user)
{
	gchar		buf[FILENAME_MAX + 32];
	const gchar	*text;
	CpaInfo		*cpa = user;

	text = dp_row_get_name_display(dp_get_tree_model(src), row);
	if(cpa->action == CPA_COPYAS)
		g_snprintf(buf, sizeof buf, _("Enter Name for Copy of \"%s\""), text);
	else
		g_snprintf(buf, sizeof buf, _("Enter Name for Clone of \"%s\""), text);
	gtk_label_set_text(GTK_LABEL(cpa->label), buf);
	gtk_entry_set_text(GTK_ENTRY(cpa->entry), text);
	gtk_editable_select_region(GTK_EDITABLE(cpa->entry), 0, -1);
	gtk_widget_grab_focus(cpa->entry);

	cmd_generic_track_entry(gen, cpa->entry);

	if(cpa->ovw_open == FALSE)
	{
		if(cpa->action == CPA_COPYAS)
			ovw_overwrite_begin(min, _("\"%s\" Already Exists - Proceed With Copy?"), 0U);
		else
			ovw_overwrite_begin(min, _("\"%s\" Already Exists - Continue With Clone?"), 0U);
		pgs_progress_begin(min, cpa->action == CPA_COPYAS ? _("Copying As...") : _("Cloning..."), PFLG_COUNT_RECURSIVE | PFLG_ITEM_VISIBLE | PFLG_BYTE_VISIBLE);
		cpa->ovw_open = TRUE;
	}
}

/* 1999-05-30 -	Update body for symlink operation. */
static void cpa_link_body(MainInfo *min, DirPane *src, DirRow2 *row, gpointer gen, gpointer user)
{
	gchar		buf[FILENAME_MAX + 32];
	const gchar	*text;
	CpaInfo		*cpa = user;

	text = dp_row_get_name_display(dp_get_tree_model(src), row);
	if(cpa->action == CPA_SYMLINKAS)
		g_snprintf(buf, sizeof buf, _("Enter Name to Link \"%s\" As"), text);
	else if(cpa->action == CPA_SYMLINKCLONE)
		g_snprintf(buf, sizeof buf, _("Enter Name for Link Clone of \"%s\""), text);
	gtk_label_set_text(GTK_LABEL(cpa->label), buf);
	gtk_entry_set_text(GTK_ENTRY(cpa->entry), text);
	gtk_editable_select_region(GTK_EDITABLE(cpa->entry), 0, -1);
	gtk_widget_grab_focus(cpa->entry);

	cmd_generic_track_entry(gen, cpa->entry);

	if(cpa->ovw_open == FALSE)
	{
		ovw_overwrite_begin(min, _("\"%s\" Already Exists - Proceed With Symlink?"), OVWF_NO_RECURSE_TEST);
		pgs_progress_begin(min, "Linking...", -1);
		cpa->ovw_open = TRUE;
	}
}

/* 2009-09-14 -	Creates a symbolic link at <dfile>, whose contents is the basename of <sfile>. */
static gboolean symlink_gfile(MainInfo *min, DirPane *src, DirPane *dst, const GFile *gfile, const GFile *dfile, GError **error)
{
	gchar		*bn;
	gboolean	ret = FALSE;

	if(gfile == NULL || dfile == NULL)
		return FALSE;
	if((bn = g_file_get_basename((GFile *) gfile)) != NULL)
	{
		ret = g_file_make_symbolic_link((GFile *) dfile, bn, NULL, error);
		g_free(bn);
	}
	return ret;
}

/* 1998-09-12 -	Perform action for copy as/clone command. */
static gint cpa_action(MainInfo *min, DirPane *src, DirPane *dst, DirRow2 *row, GError **error, gpointer user)
{
	const gchar	*text;
	CpaInfo		*cpa = user;
	OvwRes		ores;
	GFile		*sfile = NULL, *dfile = NULL;
	gint		ret;

	text = gtk_entry_get_text(GTK_ENTRY(cpa->entry));
	if(text == NULL || *text == '\0')
		return 0;

	/* Enforce difference between clone and copy: clone name is without path. */
	switch(cpa->action)
	{
		case CPA_COPYAS:
		case CPA_SYMLINKAS:
			if(g_utf8_strchr(text, -1, G_DIR_SEPARATOR) == NULL)
				dfile = dp_get_file_from_name(dst, text);
			else
				dfile = g_file_parse_name(text);
			break;
		case CPA_CLONE:
		case CPA_SYMLINKCLONE:
			if(g_utf8_strchr(text, -1, G_DIR_SEPARATOR) != NULL)
				return 0;
			dfile = dp_get_file_from_name(src, text);
			break;
	}
	if(dfile == NULL)
		return 0;

	ores = ovw_overwrite_unary_file(dst, dfile);
	if(ores == OVW_SKIP)
		ret = 1;
	else if(ores == OVW_CANCEL)
		ret = 0;
	else
	{
		ret = 1;
		if(ores == OVW_PROCEED_FILE || ores == OVW_PROCEED_DIR)
			ret = del_delete_gfile(min, dfile, FALSE, error);
		if(ret)
		{
			if((sfile = dp_get_file_from_row(src, row)) != NULL)
			{
				if((cpa->action == CPA_COPYAS) || (cpa->action == CPA_CLONE))	/* Copy? */
					ret = copy_gfile(min, src, dst, sfile, dfile, error);
				else
					ret = symlink_gfile(min, src, dst, sfile, dfile, error);
				g_object_unref(sfile);
			}
			else
				ret = 0;
		}
	}
	g_object_unref(dfile);
	if(ret)
		dp_unselect(src, row);

	return ret;
}

static void cpa_free(gpointer user)
{
	CpaInfo	*cpa = user;

	if(cpa->ovw_open)
	{
		ovw_overwrite_end(cpa->min);
		pgs_progress_end(cpa->min);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-12 -	Fire up either a COPYAS, CLONE, or SYMLINK command, depending on the <action> arg. */
static gint copyas_or_clone(MainInfo *min, DirPane *src, DirPane *dst, CpaAction action)
{
	static CpaInfo	cpa;

	cpa.action = action;
	cpa.min	   = min;
	cpa.ovw_open = FALSE;

	cpa.vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	cpa.label = gtk_label_new(_("Copy As"));
	cpa.entry = gui_dialog_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(cpa.entry), FILENAME_MAX - 1);
	gtk_box_pack_start(GTK_BOX(cpa.vbox), cpa.label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(cpa.vbox), cpa.entry, FALSE, FALSE, 0);

	if(cpa.action == CPA_COPYAS)
		return cmd_generic(min, _("Copy As"), CGF_NOALL, cpa_copy_body, cpa_action, cpa_free, &cpa);
	else if(cpa.action == CPA_CLONE)
		return cmd_generic(min, _("Clone"), CGF_NOALL | CGF_NODST, cpa_copy_body, cpa_action, cpa_free, &cpa);
	else if(cpa.action == CPA_SYMLINKAS)
		return cmd_generic(min, _("Symbolic Link As"), CGF_NOALL, cpa_link_body, cpa_action, cpa_free, &cpa);
	else if(cpa.action == CPA_SYMLINKCLONE)
		return cmd_generic(min, _("Symbolic Link Clone"), CGF_NOALL | CGF_NODST, cpa_link_body, cpa_action, cpa_free, &cpa);
	return 0;
}

/* ----------------------------------------------------------------------------------------- */

gint cmd_copyas(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	return copyas_or_clone(min, src, dst, CPA_COPYAS);
}

gint cmd_clone(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	return copyas_or_clone(min, src, dst, CPA_CLONE);
}

gint cmd_symlinkas(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	return copyas_or_clone(min, src, dst, CPA_SYMLINKAS);
}

gint cmd_symlinkclone(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	return copyas_or_clone(min, src, dst, CPA_SYMLINKCLONE);
}
