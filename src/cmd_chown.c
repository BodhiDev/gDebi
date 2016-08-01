/*
** 1998-06-07 -	A command to set the owner and group information on a file
**		(or set of files). Might get GUI-ish.
** 1998-06-08 -	Fixed the userinfo module, which allowed this one to take another
**		step towards completetion; now it preselects the current user/group
**		names from the option menus. I still miss some form of higher-level
**		control widgetry, though. Later...
** 1998-10-16 -	The defaults now work (even if you don't operate the menus).
** 1999-03-06 -	Adapted for new selection/generic/dirrow representations.
** 2000-03-23 -	Now uses combo boxes rather than menus to display user and group
**		lists. Also allows user to type in either number or name of the
**		desired user and/or group directly. All in all, a lot smoother.
** 2001-04-24 -	Added recursion option.
** 2010-03-02 -	Rewritten using GIO.
*/

#include "gentoo.h"

#include <ctype.h>
#include <stdlib.h>

#include "dirpane.h"
#include "errors.h"
#include "fileutil.h"
#include "userinfo.h"

#include "cmd_generic.h"
#include "cmd_chown.h"

#define	CMD_ID	"chown"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	GtkWidget	*combo;
	gint		history;			/* The weirdly named thing (see gtk_option_menu widget). */
} URow;

typedef struct {
	GtkWidget	*vbox;
	GtkWidget	*label;
	GtkWidget	*rowgrid;
	URow		row[2];
	guint		user, group;			/* The currently selected IDs. */
	gint		user_index, group_index;	/* Indices to default to. */
	GtkWidget	*recurse;
	gboolean	last_recurse;
} ChoInfo;

/* ----------------------------------------------------------------------------------------- */

static void cho_body(MainInfo *min, DirPane *src, DirRow2 *row, gpointer gen, gpointer user)
{
	gchar	label[2 * MAXNAMLEN + 64];
	ChoInfo	*cho = user;

	g_snprintf(label, sizeof label, _("Set Ownership for '%s':"), dp_row_get_name_display(dp_get_tree_model(src), row));
	gtk_label_set_text(GTK_LABEL(cho->label), label);
	gtk_widget_grab_focus(cho->row[0].combo);
}

/* ----------------------------------------------------------------------------------------- */

static gboolean parse_or_lookup(const gchar *str, long lookup(const gchar *name), long *tmp)
{
	while(isspace((guchar) *str))
		str++;
	if(isdigit((guchar) *str))
	{
		gchar	*eptr;

		*tmp = strtol(str, &eptr, 10);
		if(eptr == str)
			return FALSE;
	}
	else
		*tmp = lookup(str);
	return TRUE;
}

static gboolean get_own(ChoInfo *cho, uid_t *owner, gid_t *group)
{
	const gchar	*text;
	long		tmp;
	gboolean	ok = FALSE;

	if((text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(cho->row[0].combo))) == NULL)
		return FALSE;
	if(parse_or_lookup(text, usr_lookup_uid, &tmp))
	{
		*owner = tmp;
		if((text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(cho->row[1].combo))) != NULL)
		{
			if(parse_or_lookup(text, usr_lookup_gid, &tmp))
			{
				*group = tmp;
				ok = TRUE;
			}
		}
	}
	return ok;
}

static gboolean chown_gfile(MainInfo *min, GFile *file, uid_t owner, gid_t group, gboolean recurse, GError **err)
{
	gboolean	ok = FALSE;
	GFileInfo	*info;
	GFileType	type;

	if(file != NULL && (info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_UNIX_UID "," G_FILE_ATTRIBUTE_UNIX_GID,
						     G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err)) != NULL)
	{
		type = g_file_info_get_file_type(info);
		/* Don't try to change the file's type; now that we've buffered it we can remove it. */
		g_file_info_remove_attribute(info, G_FILE_ATTRIBUTE_STANDARD_TYPE);
		g_file_info_set_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_UID, owner);
		g_file_info_set_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_GID, group);
		ok = g_file_set_attributes_from_info(file, info, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err);
		g_object_unref(info);	/* Drop this early, no point in holding recursively. */
		if(ok)
		{
			if(type == G_FILE_TYPE_DIRECTORY && recurse)
			{
				GFileEnumerator	*fen;

				ok = FALSE;
				if((fen = g_file_enumerate_children(file, G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err)) != NULL)
				{
					GFileInfo	*chi;

					ok = TRUE;
					while(ok && (chi = g_file_enumerator_next_file(fen, NULL, err)) != NULL)
					{
						GFile	*child = g_file_get_child(file, g_file_info_get_name(chi));

						ok = chown_gfile(min, child, owner, group, recurse, err);
						g_object_unref(child);
					}
					g_object_unref(fen);
				}
			}
		}
	}
	return ok;
}

static gint cho_action(MainInfo *min, DirPane *src, DirPane *dst, DirRow2 *row, GError **err, gpointer user)
{
	ChoInfo		*cho = user;
	uid_t		owner = 0;
	gid_t		group = 0;
	GFile		*file;
	gboolean	ok = FALSE;

	if(!get_own(cho, &owner, &group))
		return FALSE;
	cho->last_recurse = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cho->recurse));
	if((file = dp_get_file_from_row(src, row)) != NULL)
	{
		ok = chown_gfile(min, file, owner, group, cho->last_recurse, err);
		g_object_unref(file);
	}
	if(ok)
		dp_unselect(src, row);

	return ok;
}

/* ----------------------------------------------------------------------------------------- */

gint cmd_chown(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	UsrCategory	cat[] = { UIC_USER, UIC_GROUP };
	gchar		*labtext[] = { N_("User"), N_("Group") };
	gint		i, index;
	GtkWidget	*label;
	GList		*list, *iter;
	static ChoInfo	cho;

	cho.vbox  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	cho.label = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(cho.vbox), cho.label, FALSE, FALSE, 0);
	cho.rowgrid = gtk_grid_new();
	for(i = 0; i < 2; i++)
	{
		label = gtk_label_new(_(labtext[i]));
		gtk_grid_attach(GTK_GRID(cho.rowgrid), label, 0, i, 1, 1);
		cho.row[i].combo = gtk_combo_box_text_new_with_entry();
		list = usr_string_list_create(cat[i], cat[i] == UIC_USER ? geteuid() : getgid(), &index);
		for(iter = list; iter != NULL; iter = g_list_next(iter))
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cho.row[i].combo), iter->data);
		gtk_combo_box_set_active(GTK_COMBO_BOX(cho.row[i].combo), index);
		usr_string_list_destroy(list);
		gtk_widget_set_hexpand(cho.row[i].combo, TRUE);
		gtk_widget_set_halign(cho.row[i].combo, GTK_ALIGN_FILL);
		gtk_grid_attach(GTK_GRID(cho.rowgrid), cho.row[i].combo, 1, i, 1, 1);
	}
	gtk_box_pack_start(GTK_BOX(cho.vbox), cho.rowgrid, FALSE, FALSE, 5);
	cho.recurse = gtk_check_button_new_with_label(_("Recurse Directories?"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cho.recurse), cho.last_recurse);
	gtk_box_pack_start(GTK_BOX(cho.vbox), cho.recurse, FALSE, FALSE, 0);
	gtk_widget_show_all(cho.vbox);

	return cmd_generic(min, _("Change Ownership"), CGF_SRC | CGF_NODST, cho_body, cho_action, NULL, &cho);
}
