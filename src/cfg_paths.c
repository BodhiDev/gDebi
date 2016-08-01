/*
** 1998-08-24 -	Configure search paths, i.e. lists of directories gentoo might search for
**		different kinds of files. Currently there's only one path; the one used
**		for icons. This might change in the future, though (sounds, helper apps...).
** 1998-12-25 -	Finally changed the config file format for paths. Much better now.
*/

#include "gentoo.h"
#include "fileutil.h"
#include "guiutil.h"
#include "strutil.h"
#include "xmlutil.h"

#include "configure.h"
#include "cfg_module.h"
#include "list_dialog.h"

#include "cfg_paths.h"

#define	NODE	"Paths"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	GtkWidget	*path;
} Path;

typedef struct {
	GtkWidget	*frame;
	GSList		*hgroup;
	GtkWidget	*hmode[3];
	GtkWidget	*hre;
	GtkWidget	*hicase;		/* Check button. */
} HFrame;

typedef struct {
	MainInfo	*main;
	GtkWidget	*vbox;
	GtkWidget	*pframe;		/* Frame for path widgets. */
	Path		path[PTID_NUM_PATHS];	/* Widgets for editing the paths. */

	HFrame		hide;

	HideInfo	hideinfo;
	gint		open;
	gboolean	modified;
} P_Paths;

static P_Paths	the_page;

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-24 -	User modified a path by editing it. Remember that. */
static gint evt_path_changed(GtkWidget *wid, gpointer user)
{
	P_Paths	*page = user;

	if(page->open)
		page->modified = TRUE;

	return TRUE;
}

/* 2011-09-27 -	Path editor for the list dialog, makes it easy to edit each single path by providing a standard file chooser dialog. */
static gboolean path_editor(GString *value, gpointer user)
{
	P_Paths		*page = user;
	GtkWidget	*fc;
	gint		ret;
	gboolean	success = FALSE;

	fc = gtk_file_chooser_dialog_new(_("Edit path"), GTK_WINDOW(page->main->gui->window), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, _("_Open"), GTK_RESPONSE_OK, _("_Cancel"), GTK_RESPONSE_CANCEL, NULL);
	/* For this usage, I really think it makes sense to call this, despite the recommendation not to. */
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fc), value->str);
	ret = gtk_dialog_run(GTK_DIALOG(fc));
	if(ret == GTK_RESPONSE_OK)
	{
		gchar	*filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));

		if(filename != NULL && *filename != '\0')
		{
			g_string_assign(value, filename);
			success = TRUE;
		}
		g_free(filename);
	}
	gtk_widget_destroy(fc);

	return success;
}

/* 2010-07-28 -	The "pick" button next to a path was clicked; pop up a list dialog. */
static void evt_path_pick_clicked(GtkWidget *wid, gpointer user)
{
	P_Paths		*page = user;
	const gint	index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "index"));
	GtkWidget	*entry = page->path[index].path;
	const gchar	*text;

	if((text = gtk_entry_get_text(GTK_ENTRY(entry))) != NULL)
	{
		gchar	tmp[1024];

		g_snprintf(tmp, sizeof tmp, "%s", text);
		ldl_dialog_sync_new_full_wait(tmp, sizeof tmp, ':', _("Edit path"), path_editor, page);
		if(strcmp(tmp, text) != 0)
			gtk_entry_set_text(GTK_ENTRY(entry), tmp);
	}
}

/* 1998-10-30 -	User just changed the hiding mode. */
static gint evt_hmode_clicked(GtkWidget *wid, gpointer user)
{
	P_Paths	*page = user;

	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)))
	{
		page->hideinfo.mode = (HMode) g_object_get_data(G_OBJECT(wid), "mode");
		gtk_widget_set_sensitive(page->hide.hre, page->hideinfo.mode == HIDE_REGEXP);
		gtk_widget_set_sensitive(page->hide.hicase, page->hideinfo.mode == HIDE_REGEXP);

		page->modified = TRUE;
	}

	return TRUE;
}

/* 1998-10-30 -	User edited the hide RE. Register change, and grab the string. */
static gint evt_hre_changed(GtkWidget *wid, gpointer user)
{
	P_Paths	*page = user;

	g_strlcpy(page->hideinfo.hide_re_src, gtk_entry_get_text(GTK_ENTRY(wid)), sizeof page->hideinfo.hide_re_src);
	page->modified = TRUE;
	return TRUE;
}

/* 1998-10-26 -	The "Ignore Case?" check button for hide RE has been hit. */
static gint evt_hicase_clicked(GtkWidget *wid, gpointer user)
{
	P_Paths	*page = user;

	page->hideinfo.no_case = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));
	page->modified = TRUE;

	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-24 -	Populate the paths. Not very complicated.
** 1998-10-26 -	Now also initializes the hide widgetry.
*/
static void populate_paths(CfgInfo *cfg, P_Paths *page)
{
	gint	i;

	for(i = 0; i < PTID_NUM_PATHS; i++)
		gtk_entry_set_text(GTK_ENTRY(page->path[i].path), cfg->path.path[i]->str);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->hide.hmode[cfg->path.hideinfo.mode]), TRUE);
	gtk_entry_set_text(GTK_ENTRY(page->hide.hre), cfg->path.hideinfo.hide_re_src);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->hide.hicase), cfg->path.hideinfo.no_case);
	gtk_widget_set_sensitive(page->hide.hre, page->hideinfo.mode == HIDE_REGEXP);
	gtk_widget_set_sensitive(page->hide.hicase, page->hideinfo.mode == HIDE_REGEXP);
}

/* 1998-08-24 -	Open up the paths config page. Very simple at this time, since there's only
**		one path to configure.
*/
static GtkWidget * cpt_init(MainInfo *min, gchar **name)
{
	P_Paths		*page = &the_page;
	GtkWidget	*grid, *label, *vbox;
	const struct {
		const gchar	*label;
		gboolean	with_button;
	} pinfo[] = {
		{ N_("Icons"),		TRUE },
		{ N_("GTK+ RC"),	TRUE },
		{ N_("fstab"),		FALSE },
		{ N_("mtab"),		FALSE }
	};
	const gchar	*hlab[] = { N_("None"), N_("Beginning With Dot (.)"), N_("Matching RE") };
	guint		i;

	if(name == NULL)
		return NULL;

	*name = _("Paths & Hide");

	page->main = min;
	page->open = FALSE;
	page->modified = FALSE;

	page->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	page->pframe = gtk_frame_new(_("Paths"));

	grid = gtk_grid_new();
	if(sizeof pinfo / sizeof *pinfo == PTID_NUM_PATHS)
	{
		for(i = 0; i < PTID_NUM_PATHS; i++)
		{
			gint	width;

			label = gtk_label_new(_(pinfo[i].label));
			gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
			page->path[i].path = gtk_entry_new();
			gtk_widget_set_hexpand(page->path[i].path, TRUE);
			gtk_widget_set_halign(page->path[i].path, GTK_ALIGN_FILL);
			g_signal_connect(G_OBJECT(page->path[i].path), "changed", G_CALLBACK(evt_path_changed), page);
			width = pinfo[i].with_button ? 1 : 2;
			gtk_grid_attach(GTK_GRID(grid), page->path[i].path, 1, i, width, 1);
			if(pinfo[i].with_button)
			{
				GtkWidget	*pick;

				pick = gui_details_button_new();
				g_object_set_data(G_OBJECT(pick), "index", GINT_TO_POINTER(i));
				g_signal_connect(G_OBJECT(pick), "clicked", G_CALLBACK(evt_path_pick_clicked), page);
				gtk_grid_attach(GTK_GRID(grid), pick, 2, i, 1, 1);
			}
#if defined __OpenBSD__ || defined __FreeBSD__ || defined __NetBSD__
			/* Mountlist and mounted fs files are non-configurable on BSD systems. */
			if(i == 2 || i == 3)
				gtk_widget_set_sensitive(page->path[i].path, FALSE);
#endif
		}
	}
	else
		g_warning("**CFGPATHS: Missing labels!");

	gtk_container_add(GTK_CONTAINER(page->pframe), grid);
	gtk_box_pack_start(GTK_BOX(page->vbox), page->pframe, FALSE, FALSE, 5);

	page->hide.frame  = gtk_frame_new(_("Hide Entries"));
	page->hide.hgroup = gui_radio_group_new(3, hlab, page->hide.hmode);
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	for(i = 0; i < sizeof page->hide.hmode / sizeof page->hide.hmode[0]; i++)
	{
		g_object_set_data(G_OBJECT(page->hide.hmode[i]), "mode", GINT_TO_POINTER(i));
		g_signal_connect(G_OBJECT(page->hide.hmode[i]), "clicked", G_CALLBACK(evt_hmode_clicked), page);
		if(i < 2)
			gtk_box_pack_start(GTK_BOX(vbox), page->hide.hmode[i], FALSE, FALSE, 0);
		else
		{
			GtkWidget	*hbox;

			hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			gtk_box_pack_start(GTK_BOX(hbox), page->hide.hmode[i], FALSE, FALSE, 0);
			page->hide.hre = gtk_entry_new();
			gtk_entry_set_max_length(GTK_ENTRY(page->hide.hre), sizeof min->cfg.path.hideinfo.hide_re_src - 1);
			g_signal_connect(G_OBJECT(page->hide.hre), "changed", G_CALLBACK(evt_hre_changed), page);
			gtk_box_pack_start(GTK_BOX(hbox), page->hide.hre, TRUE, TRUE, 0);
			page->hide.hicase = gtk_check_button_new_with_label(_("Ignore Case?"));
			g_signal_connect(G_OBJECT(page->hide.hicase), "clicked", G_CALLBACK(evt_hicase_clicked), page);
			gtk_box_pack_start(GTK_BOX(hbox), page->hide.hicase, FALSE, FALSE, 0);
			gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
		}
	}
	gtk_container_add(GTK_CONTAINER(page->hide.frame), vbox);
	gtk_box_pack_start(GTK_BOX(page->vbox), page->hide.frame, FALSE, FALSE, 5);

	gtk_widget_show_all(page->vbox);

	return page->vbox;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-24 -	Update path widgets. Tough. */
static void cpt_update(MainInfo *min)
{
	P_Paths	*page = &the_page;

	page->hideinfo = min->cfg.path.hideinfo;
	populate_paths(&min->cfg, page);
	page->open = TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-24 -	Accept changes to paths (if any).
** 1998-08-30 -	Now detects changes, and only alters the main current setting if there really
**		has been changes made. Also added a switch() allowing different actions to be
**		carried out when a path has changed. Initial use: rescan when icon path changes.
*/
static void cpt_accept(MainInfo *min)
{
	P_Paths		*page = &the_page;
	const gchar	*text;
	gint		i;

	if(page->open && page->modified)
	{
		for(i = 0; i < PTID_NUM_PATHS; i++)
		{
			text = gtk_entry_get_text(GTK_ENTRY(page->path[i].path));
			if(strcmp(text, min->cfg.path.path[i]->str))
			{
				g_string_assign(min->cfg.path.path[i], text);
				switch((PathID) i)
				{
					case PTID_ICON:
						cfg_set_flags(CFLG_FLUSH_ICONS);
						break;
					default:
						break;
				}
			}
		}
		min->cfg.path.hideinfo = page->hideinfo;
		fut_check_hide(&min->gui->pane[0], NULL);		/* Frees any precompiled RE. */
		cfg_set_flags(CFLG_RESCAN_BOTH);
		page->modified = FALSE;
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-24 -	Save out the paths. Not very complex.
** 1998-10-26 -	Added the hide info, too.
*/
static gint cpt_save(MainInfo *min, FILE *out)
{
	gint	i;

	xml_put_node_open(out, NODE);

	xml_put_node_open(out, "PathList");
	for(i = 0; i < PTID_NUM_PATHS; i++)
	{
		xml_put_node_open(out, "Path");
		xml_put_integer(out, "index", i);
		xml_put_text(out, "path", min->cfg.path.path[i]->str);
		xml_put_node_close(out, "Path");
	}
	xml_put_node_close(out, "PathList");

	xml_put_node_open(out, "HideInfo");
	xml_put_integer(out, "mode", min->cfg.path.hideinfo.mode);
 	xml_put_text(out, "re", min->cfg.path.hideinfo.hide_re_src);
	xml_put_boolean(out, "re_nocase", min->cfg.path.hideinfo.no_case);
	xml_put_node_close(out, "HideInfo");

	xml_put_node_close(out, NODE);

	return TRUE;
}

static void load_path(const XmlNode *data, gpointer user)
{
	CfgInfo	*cfg = user;
	gint	index;

	if(xml_get_integer(data, "index", &index))
	{
		if((index >= 0) && (index < PTID_NUM_PATHS))
		{
			const gchar	*path;

			if(xml_get_text(data, "path", &path))
				g_string_assign(cfg->path.path[index], path);
		}
		else
			g_warning("**CFGPATHS: Path index %d is illegal", index);
	}
}

static void load_hideinfo(MainInfo *min, const XmlNode *node)
{
	gint	tmp;
	union {
		HMode	hmode;
		gint	integer;
	} htmp;

	xml_get_integer(node, "mode", &htmp.integer);
	min->cfg.path.hideinfo.mode = htmp.hmode;
	xml_get_text_copy(node, "re", min->cfg.path.hideinfo.hide_re_src, sizeof min->cfg.path.hideinfo.hide_re_src);
	if(xml_get_boolean(node, "re_nocase", &tmp))
		min->cfg.path.hideinfo.no_case = tmp;
}

/* 1998-08-24 -	Load paths from config tree. Ugly. */
static void cpt_load(MainInfo *min, const XmlNode *node)
{
	const XmlNode		*data;

	if((data = xml_tree_search(node, "PathList")) != NULL)
		xml_node_visit_children(data, load_path, &min->cfg);
	if((data = xml_tree_search(node, "HideInfo")) != NULL)
		load_hideinfo(min, data);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-24 -	Hide the paths page. Not very much to do. */
static void cpt_hide(MainInfo *min)
{
	the_page.open = FALSE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-24 -	Describe the paths config page. */
const CfgModule * cpt_describe(MainInfo *min)
{
	static const CfgModule	desc = { NODE, cpt_init, cpt_update, cpt_accept, cpt_save, cpt_load, cpt_hide };

	return &desc;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-25 -	Return pointer to current setting for path <id>. */
const gchar * cpt_get_path(PathID id)
{
	GtkEntry	*ent = NULL;

	switch(id)
	{
		case PTID_ICON:
			ent = GTK_ENTRY(the_page.path[id].path);
			break;
		default:
			g_warning("cpt_get_path(): Unknown path %d requested", id);
	}
	if(ent != NULL)
		return gtk_entry_get_text(ent);

	return NULL;
}

