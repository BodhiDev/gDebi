/*
** 1998-05-29 -	A command to change the access flags of a file or directory.
**		Made significantly simpler by the new cmd_generic module.
** 1999-03-06 -	Adapted for new selection/generic handling.
** 2010-03-02 -	GIO porting more or less complete.
*/

#include "gentoo.h"
#include "errors.h"
#include "dirpane.h"
#include "strutil.h"
#include "window.h"

#include "cmd_generic.h"
#include "cmd_chmod.h"

#define	CMD_ID	"chmod"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	GtkWidget	*frame;
	GtkWidget	*vbox;
	GtkWidget	*check[3];
	gulong		signal[3];
} PFrame;

typedef struct {
	GtkWidget	*vbox;
	GtkWidget	*label;
	GtkWidget	*fbox;
	PFrame		frame[4];
	GtkWidget	*entry_text;
	GtkWidget	*entry_octal;
	GtkWidget	*bbox;
	GtkWidget	*all, *none, *toggle, *revert;
	mode_t		last_mode;
	GtkWidget	*recurse;
	gboolean	last_recurse;
	GtkWidget	*nodirs;
	gboolean	last_nodirs;
} ChmInfo;

static const mode_t mask[] = {  S_ISUID, S_ISGID, S_ISVTX,
				S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP, S_IXGRP, S_IROTH, S_IWOTH, S_IXOTH };

/* ----------------------------------------------------------------------------------------- */

static mode_t get_checks(const ChmInfo *chm)
{
	mode_t	mode = 0U;
	guint	i, j, k;

	for(i = k = 0; i < sizeof chm->frame / sizeof *chm->frame; i++)
	{
		for(j = 0; j < sizeof chm->frame[i].check / sizeof *chm->frame[i].check; j++, k++)
		{
			if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chm->frame[i].check[j])))
				mode |= mask[k];
		}
	}
	return mode;
}

/* 2009-03-25 -	Update the textual representations. These are currently read-only. */
static void set_texts(ChmInfo *chm, mode_t mode)
{
	gchar	buf[32];

	stu_mode_to_text(buf, sizeof buf, mode);
	gtk_entry_set_text(GTK_ENTRY(chm->entry_text), buf + 1);	/* Skip the directory indicator. */
	g_snprintf(buf, sizeof buf, "%o", mode);
	gtk_entry_set_text(GTK_ENTRY(chm->entry_octal), buf);
}

static void set_checks(ChmInfo *chm, mode_t mode)
{
	guint	i, j, k;

	for(i = k = 0; i < sizeof chm->frame / sizeof *chm->frame; i++)
	{
		for(j = 0; j < sizeof chm->frame[i].check / sizeof *chm->frame[i].check; j++, k++)
		{
			g_signal_handler_block(G_OBJECT(chm->frame[i].check[j]), chm->frame[i].signal[j]);
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chm->frame[i].check[j]), mode & mask[k]);
			g_signal_handler_unblock(G_OBJECT(chm->frame[i].check[j]), chm->frame[i].signal[j]);
		}
	}
	set_texts(chm, mode);
}

static void chm_body(MainInfo *min, DirPane *src, DirRow2 *row, gpointer gen, gpointer user)
{
	ChmInfo		*chm = user;
	gchar		temp[2 * FILENAME_MAX + 128];
	mode_t		mode = dp_row_get_mode(dp_get_tree_model(src), row);

	g_snprintf(temp, sizeof temp, _("Set protection bits for \"%s\":"), dp_row_get_name_display(dp_get_tree_model(src), row));
	gtk_label_set_text(GTK_LABEL(chm->label), temp);
	set_checks(chm, chm->last_mode = (mode & 07777));
}

/* ----------------------------------------------------------------------------------------- */

static gboolean chmod_gfile(MainInfo *min, DirPane *src, const GFile *file, mode_t mode, gboolean recurse, gboolean nodirs, GError **err)
{
	GFileInfo	*fi;
	gboolean	ok = FALSE;

	if((fi = g_file_query_info((GFile *) file, G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_UNIX_MODE, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err)) != NULL)
	{
		const gboolean	isdir = (g_file_info_get_file_type(fi) == G_FILE_TYPE_DIRECTORY);

		/* Don't try to change the file's type. */
		g_file_info_remove_attribute(fi, G_FILE_ATTRIBUTE_STANDARD_TYPE);

		/* If non-directory or we're attacking dirs, set the mode first. */
		if(!isdir || !nodirs)
		{
			g_file_info_set_attribute_uint32(fi, G_FILE_ATTRIBUTE_UNIX_MODE, mode);
			ok = g_file_set_attributes_from_info((GFile *) file, fi, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err);
		}
		else
			ok = TRUE;
		/* Drop the info, we're done. */
		g_object_unref(fi);

		/* If successful so far, consider recursing. */
		if(ok && isdir && recurse)
		{
			GFileEnumerator	*fe;

			if((fe = g_file_enumerate_children((GFile *) file, "standard::name,unix::mode", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, err)) != NULL)
			{
				GFileInfo	*cfi;
				GFile		*child;

				while(ok && (cfi = g_file_enumerator_next_file(fe, NULL, err)) != NULL)
				{
					if((child = g_file_get_child((GFile *) file, g_file_info_get_name(cfi))) != NULL)
					{
						ok = chmod_gfile(min, src, child, mode, recurse, nodirs, err);
						g_object_unref(child);
					}
					else
						ok = FALSE;
					g_object_unref(cfi);
				}
				g_object_unref(fe);
			}
			else
				ok = FALSE;
		}
	}
	return ok;
}

static gint chm_action(MainInfo *min, DirPane *src, DirPane *dst, DirRow2 *row, GError **err, gpointer user)
{
	ChmInfo		*chm = user;
	const mode_t	mode = get_checks(chm);
	GFile		*dfile;
	gboolean	ok;

	chm->last_recurse = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chm->recurse));
	chm->last_nodirs  = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chm->nodirs));
	dfile = dp_get_file_from_row(src, row);
	ok = chmod_gfile(min, src, dfile, mode, chm->last_recurse, chm->last_nodirs, err);
	if(ok)
		dp_unselect(src, row);

	return ok;
}

/* ----------------------------------------------------------------------------------------- */

static void evt_clicked(GtkWidget *wid, gpointer user)
{
	ChmInfo	*chm = user;

	if(wid == chm->all)
		set_checks(chm, S_ISUID | S_ISGID | S_ISVTX | S_IRUSR | S_IWUSR | S_IXUSR |
				S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
	else if(wid == chm->none)
		set_checks(chm, 0);
	else if(wid == chm->toggle)
		set_checks(chm, (~get_checks(chm)) & 07777);
	else if(wid == chm->revert)
		set_checks(chm, chm->last_mode);
	else
		set_texts(chm, get_checks(chm));
}

/* 1998-05-29 -	Build a protection frame, with three checkboxes. If type is 0, we build a
**		special one (with setuid/setgid/sticky), otherwise a standard (read/write/exec).
*/
static void build_frame(ChmInfo *ci, gint pos, gint type)
{
	gchar	*label[] = { N_("Special"), N_("Owner"),   N_("Group"),  N_("Others") };
	gchar	*check[] = { N_("Set UID"), N_("Set GID"), N_("Sticky"), N_("Read"), N_("Write"), N_("Execute") };
	PFrame	*fr = &ci->frame[pos];
	gint	i;

	fr->frame = gtk_frame_new(_(label[pos]));
	fr->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	for(i = 0; i < 3; i++)
	{
		fr->check[i] = gtk_check_button_new_with_label(_(check[type * 3 + i]));
		fr->signal[i] = g_signal_connect(G_OBJECT(fr->check[i]), "toggled", G_CALLBACK(evt_clicked), ci);
		gtk_box_pack_start(GTK_BOX(fr->vbox), fr->check[i], TRUE, TRUE, 0);
	}
	gtk_container_add(GTK_CONTAINER(fr->frame), fr->vbox);
}

gint cmd_chmod(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	static ChmInfo	ci;
	guint		i;
	GtkWidget	*hbox, *w;

	ci.vbox  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
	ci.label = gtk_label_new(_("Protection Bits"));
	ci.fbox  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	for(i = 0; i < sizeof ci.frame / sizeof ci.frame[0]; i++)
	{
		build_frame(&ci, i, (i == 0) ? 0 : 1);
		gtk_box_pack_start(GTK_BOX(ci.fbox), ci.frame[i].frame, TRUE, TRUE, 5);
	}
	gtk_box_pack_start(GTK_BOX(ci.vbox), ci.label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(ci.vbox), ci.fbox,  TRUE,  TRUE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	w = gtk_label_new(_("Textual"));
	gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
	ci.entry_text = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(ci.entry_text), 10);
	gtk_entry_set_width_chars(GTK_ENTRY(ci.entry_text), 10);
	gtk_editable_set_editable(GTK_EDITABLE(ci.entry_text), FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), ci.entry_text, FALSE, FALSE, 0);

	ci.entry_octal = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(ci.entry_octal),  4);
	gtk_entry_set_width_chars(GTK_ENTRY(ci.entry_octal), 4);
	gtk_editable_set_editable(GTK_EDITABLE(ci.entry_octal), FALSE);
	gtk_box_pack_end(GTK_BOX(hbox), ci.entry_octal, FALSE, FALSE, 0);
	w = gtk_label_new(_("Octal"));
	gtk_box_pack_end(GTK_BOX(hbox), w, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(ci.vbox), hbox, FALSE, FALSE, 0);

	ci.bbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	ci.all	  = gtk_button_new_with_label(_("All"));
	ci.none	  = gtk_button_new_with_label(_("None"));
	ci.toggle = gtk_button_new_with_label(_("Toggle"));
	ci.revert = gtk_button_new_with_label(_("Revert"));

	g_signal_connect(G_OBJECT(ci.all),    "clicked", G_CALLBACK(evt_clicked), &ci);
	g_signal_connect(G_OBJECT(ci.none),   "clicked", G_CALLBACK(evt_clicked), &ci);
	g_signal_connect(G_OBJECT(ci.toggle), "clicked", G_CALLBACK(evt_clicked), &ci);
	g_signal_connect(G_OBJECT(ci.revert), "clicked", G_CALLBACK(evt_clicked), &ci);
	gtk_box_pack_start(GTK_BOX(ci.bbox), ci.all,    TRUE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(ci.bbox), ci.none,   TRUE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(ci.bbox), ci.toggle, TRUE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(ci.bbox), ci.revert, TRUE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(ci.vbox), ci.bbox, TRUE, TRUE, 0);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	ci.recurse = gtk_check_button_new_with_label(_("Recurse Directories?"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ci.recurse), ci.last_recurse);
	gtk_box_pack_start(GTK_BOX(hbox), ci.recurse, TRUE, TRUE, 0);
	ci.nodirs = gtk_check_button_new_with_label(_("Don't Touch Directories?"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ci.nodirs), ci.last_nodirs);
	gtk_box_pack_start(GTK_BOX(hbox), ci.nodirs, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(ci.vbox), hbox, FALSE, FALSE, 0);

	return cmd_generic(min, _("Change Mode"), CGF_SRC, chm_body, chm_action, NULL, &ci);
}
