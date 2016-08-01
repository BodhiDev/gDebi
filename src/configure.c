/*
** 1998-05-31 -	After a long wait, here it finally is - the gdtool GUI config
**		module! This will be *big*, I can feel it.
** 1998-06-16 -	Smartened up the window handling. Now the config window is only ever
**		built (created) once. It is then reused! Environmentally safe.
**		It also allows me to use the config GUI to keep all the state,
**		although I'm not exactly convinced that's what I want to do...
** 1998-06-22 -	Redesigned. Cut away all old notebook-page-creation code (~140
**		lines) and implemented a new, more modular way of doing it.
** 1998-07-26 -	Added a global cache of page descriptors, avoiding having to
**		repeat the describe-calls all the time.
** 1998-08-25 -	Implemented a slim way of having the individual page modules
**		notify this main module of program-wide things they want done when
**		config closes. As an example, it is important to rescan the directories
**		if the styles or types change.
** 1998-08-30 -	Fixed version handling in config file, at least somewhat. Also added
**		a system-wide config, which is loaded if the user doesn't have one.
** 1998-10-16 -	System-wide config path now configurable via a symbol.
** 1999-12-24 -	Now uses the window utility module to handle the root config window,
**		thus making its size and position configurable and savable.
** 2000-07-02 -	Translated.
** 2002-07-19 -	Renamed, and recast the interface to use a tree. Cleaner-looking, and
**		handles nested stuff better.
*/

#include "gentoo.h"

#include <stdlib.h>

#include "cmdseq.h"
#include "dialog.h"
#include "dirpane.h"
#include "fileutil.h"
#include "iconutil.h"
#include "nag_dialog.h"
#include "window.h"
#include "xmlutil.h"

#include "configure.h"

#include "cfg_module.h"

#include "cfg_buttonlayout.h"
#include "cfg_buttons.h"
#include "cfg_cmdcfg.h"
#include "cfg_cmdseq.h"
#include "cfg_controls.h"
#include "cfg_dialogs.h"
#include "cfg_dirpane.h"
#include "cfg_errors.h"
#include "cfg_menus.h"
#include "cfg_nag.h"
#include "cfg_paths.h"
#include "cfg_styles.h"
#include "cfg_types.h"
#include "cfg_windows.h"

/* This should be set in the Makefile, and passed along using the -D
** compiler option, If not, let's default to nice old Slackware style.
*/
#if !defined PATH_CFG
#define	PATH_CFG	"/usr/local/etc/"
#endif

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	MainInfo	*min;
	GtkWidget	*dlg;
	GtkWidget	*view;
	GtkTreeStore	*store;
	GtkTreeIter	treeiter[4];
	guint		level;		/* Current depth in tree, used when building. */
	GtkWidget	*first;		/* Keeps track of first leaf node during build, for select. */
	GtkWidget	*nbook;		/* A notebook widget holding all the config pages. */
	GtkWidget	*ok, *save, *cancel;
	gint		page;		/* Index of last selected page. */

	guint32		flags;		/* Flags for stuff that need to be done when window closes. */
} CfgGui;

/* A global (yuck!) vector of page descriptor functions. This is where pointers to new pages go. */
static const CMDescribeFunc describe_page[] = {	cdp_describe,
						ccs_describe, ccc_describe,
						cst_describe, ctp_describe,
						cbt_describe, cbl_describe,
						cpt_describe,
						cwn_describe, cdl_describe,
						cct_describe, cer_describe,
						cng_describe,
					};

#define	CFG_PAGES	(sizeof describe_page / sizeof describe_page[0])
/* A global vector of the resulting page descriptors. */
static const CfgModule	*cfg_page[CFG_PAGES];


static CfgGui	the_cfggui = { NULL };

/* ----------------------------------------------------------------------------------------- */

/* 2011-07-24 -	Returns the base directory where the proper configuration file is to be found. */
static const gchar * get_config_dirname(void)
{
	static gchar	buf[1024] = "";

	if(buf[0] == '\0')
	{
		const gchar	*confdir;

		if((confdir = g_get_user_config_dir()) != NULL)
			g_snprintf(buf, sizeof buf, "%s" G_DIR_SEPARATOR_S PACKAGE, confdir);
	}
	return buf;
}

/* 2011-07-24 -	Returns pointer to the proper filename for our configuration file. */
static const gchar * get_config_filename(const gchar *filename)
{
	static gchar	buf[1024] = "";

	if(buf[0] == '\0')
	{
		const gchar	*confdir = get_config_dirname();

		g_snprintf(buf, sizeof buf, "%s" G_DIR_SEPARATOR_S "%s", confdir, filename);
	}
	return buf;
}

/* 2011-07-24 -	Returns pointer to the old and outdated filename used by our configuration file. */
static const gchar * get_config_filename_old(const gchar *filename)
{
	static gchar	buf[1024];

	if(buf[0] == '\0')
	{
		const gchar	*home = g_getenv("HOME");

		if(home == NULL)
			home = g_get_home_dir();
		if(home != NULL)
			g_snprintf(buf, sizeof buf, "%s" G_DIR_SEPARATOR_S "%s", home, filename);
	}
	return buf;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-06-26 -	Do the work of hiding the config GUI. */
static void hide_config(CfgGui *cgu)
{
	guint	i;

	for(i = 0; i < CFG_PAGES; i++)
	{
		if(cfg_page[i]->hide != NULL)
			cfg_page[i]->hide(cgu->min);
	}
	if(the_cfggui.flags & CFLG_RESET_KEYBOARD)
		kbd_context_clear(cgu->min->gui->kbd_ctx);

	if(the_cfggui.flags & CFLG_REBUILD_TOP)
		rebuild_top(cgu->min);
	if(the_cfggui.flags & CFLG_REBUILD_MIDDLE)
		rebuild_middle(cgu->min);

	/* The rescanning needs to happen early, since we now have potentially stale Type pointers in the panes. */
	if(the_cfggui.flags & CFLG_RESCAN_LEFT)
		dp_rescan(&cgu->min->gui->pane[0]);
	else if(the_cfggui.flags & CFLG_REDISP_LEFT)
		dp_redisplay_preserve(&cgu->min->gui->pane[0]);
	if(the_cfggui.flags & CFLG_RESCAN_RIGHT)
		dp_rescan(&cgu->min->gui->pane[1]);
	else if(the_cfggui.flags & CFLG_REDISP_RIGHT)
		dp_redisplay_preserve(&cgu->min->gui->pane[1]);

	if(the_cfggui.flags & CFLG_REBUILD_BOTTOM)
	{
		rebuild_bottom(cgu->min);
		csq_execute(cgu->min, "ActivateOther");
		csq_execute(cgu->min, "ActivateOther");
	}

	if(the_cfggui.flags & CFLG_FLUSH_ICONS)
		ico_flush(cgu->min);

	if(the_cfggui.flags & CFLG_RESET_KEYBOARD)
		ctrl_keys_install(cgu->min->cfg.ctrlinfo, cgu->min->gui->kbd_ctx);

	gtk_grab_remove(cgu->dlg);
	win_window_relink(cgu->min->cfg.wininfo, WIN_CONFIG, cgu->dlg);
	win_window_close(cgu->dlg);
}

/* 1998-06-26 -	The user just clicked the OK button. Let all page modules know, then hide the
**		GUI.
*/
static gint evt_ok_clicked(GtkWidget *wid, gpointer user)
{
	CfgGui		*cgu = user;
	MainInfo	*min = cgu->min;
	guint		i;

	cfg_modified_set(min);

	for(i = 0; i < CFG_PAGES; i++)
	{
		if(cfg_page[i]->accept != NULL)
			cfg_page[i]->accept(min);
	}
	hide_config(cgu);
	return TRUE;
}

/* 1998-09-18 -	Broke the saving code out of the button handler, and made it globally
**		accessible.
*/
void cfg_save_all(MainInfo *min)
{
	const gchar	*root = "GentooConfig", *rcname = get_config_filename(RCNAME);
	FILE		*out;
	guint		i;
	const CfgModule	*page;

	cfg_modified_clear(min);
	if((out = xml_put_open(rcname)) != NULL)
	{
		xml_put_node_open(out, root);
		xml_put_text(out, "version", VERSION);
		for(i = 0; i < CFG_PAGES; i++)
		{
			if((page = describe_page[i](min)) != NULL && (page->save != NULL))
				page->save(min, out);
		}
		xml_put_node_close(out, root);
		xml_put_close(out);
	}
	else
		dlg_dialog_async_new_error(_("Couldn't open configuration file for output"));
}

/* 1998-07-25 -	I guess it's becoming time to grow up and start outputting a config file.
**		XML seems to be the format of the week, so I'll just go for something like
**		that.
** 1998-09-18 -	Broke out the actual saving code and put in in a function of its own.
*/
static gint evt_save_clicked(GtkWidget *wid, gpointer user)
{
	CfgGui		*cgu = user;
	MainInfo	*min = cgu->min;
	guint		i;

	for(i = 0; i < CFG_PAGES; i++)
	{
		if(cfg_page[i]->accept != NULL)
			cfg_page[i]->accept(min);
	}
	cfg_save_all(min);
	hide_config(cgu);

	return TRUE;
}

/* 1998-07-12 -	User clicked the cancel button. Hide the GUI. */
static gint evt_cancel_clicked(GtkWidget *wid, gpointer user)
{
	hide_config(user);
	return TRUE;
}

/* 1998-05-31 -	Build the buttons at the bottom of the config window (OK, Save, Cancel). */
static void build_buttons(CfgGui *cgu)
{
	cgu->ok = gtk_button_new_with_label(_("OK"));
	g_signal_connect(G_OBJECT(cgu->ok), "clicked", G_CALLBACK(evt_ok_clicked), cgu);
	gtk_dialog_add_action_widget(GTK_DIALOG(cgu->dlg), cgu->ok, GTK_RESPONSE_OK);
	cgu->save = gtk_button_new_with_label(_("Save"));
	g_signal_connect(G_OBJECT(cgu->save), "clicked", G_CALLBACK(evt_save_clicked), cgu);
	gtk_dialog_add_action_widget(GTK_DIALOG(cgu->dlg), cgu->save, GTK_RESPONSE_OK);
	cgu->cancel = gtk_button_new_with_label(_("Cancel"));
	g_signal_connect(G_OBJECT(cgu->cancel), "clicked", G_CALLBACK(evt_cancel_clicked), cgu);
	gtk_dialog_add_action_widget(GTK_DIALOG(cgu->dlg), cgu->cancel, GTK_RESPONSE_CANCEL);

	gtk_widget_set_can_default(cgu->ok, TRUE);
	gtk_widget_set_can_default(cgu->save, TRUE);
	gtk_widget_set_can_default(cgu->cancel, TRUE);
	gtk_widget_grab_default(cgu->ok);
}

/* 1998-06-16 -	This gets called as the user clicks the close button of the config GUI.
**		Unlike what the function name might lead you to expect, we don't destroy the
**		GUI we so laborously (sp?) created. We just hide it so we can use it again
**		later. Neat for several reasons.
*/
static gint evt_cfg_delete(GtkWidget *wid, GdkEvent *evt, gpointer user)
{
	hide_config(user);

	return TRUE;
}

/* 2004-11-2x -	Page tree cursor changed, so switch page in the notebook on the right. */
static void evt_view_cursor_changed(GtkWidget *view, gpointer user)
{
	CfgGui		*cgu = user;
	GtkTreePath	*path;
	GtkTreeIter	iter;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(view), &path, NULL);
	if(path == NULL)
		return;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(cgu->store), &iter, path);
	if(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(cgu->store), &iter) > 0)	/* Disregard interior nodes. */
		return;
	gtk_tree_model_get(GTK_TREE_MODEL(cgu->store), &iter, 1, &cgu->page, -1);
	gtk_notebook_set_current_page(GTK_NOTEBOOK(cgu->nbook), cgu->page);
}

/* 1998-06-16 -	Cooled up (?) this routine a lot. It is now recursive, in a rather
**		interesting manner. The point is not to reconstruct the entire config-
**		GUI each time it is needed, but rather to just build it once and then
**		hide/show it as needed.
*/
gint cfg_configure(MainInfo *min)
{
	CfgGui		*cgu = &the_cfggui;
	gchar		*name = NULL;
	guint		i;
	GtkWidget	*hbox, *scwin, *simple;
	GtkCellRenderer	*cr;
	GtkTreeViewColumn *vc;

	the_cfggui.flags = 0U;

	/* Do the widgets already exist? Then just update and display. */
	if(cgu->dlg != NULL)
	{
		for(i = 0; i < CFG_PAGES; i++)
		{
			if(cfg_page[i]->update != NULL)
				cfg_page[i]->update(min);
		}
		win_window_show(cgu->dlg);
		gtk_notebook_set_current_page(GTK_NOTEBOOK(cgu->nbook), the_cfggui.page);
		gtk_widget_show_all(cgu->dlg);
		gtk_grab_add(cgu->dlg);
		return 1;
	}

	/* Initialize the cache of page descriptors. */
	for(i = 0; i < CFG_PAGES; i++)
		cfg_page[i] = describe_page[i](min);

	cgu->min = min;
	cgu->dlg = win_window_open(min->cfg.wininfo, WIN_CONFIG);
	gtk_window_set_modal(GTK_WINDOW(cgu->dlg), TRUE);
	g_signal_connect(G_OBJECT(cgu->dlg), "delete_event", G_CALLBACK(evt_cfg_delete), cgu);

	cgu->store = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_INT);	/* Simply store notebook page# in second column. */
	cgu->level = 0U;

	cgu->first = NULL;
	cgu->nbook = gtk_notebook_new();
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(cgu->nbook), FALSE);

	for(i = 0; i < CFG_PAGES; i++)
	{
		if(cfg_page[i]->init != NULL)
		{
			if((simple = cfg_page[i]->init(min, &name)) != NULL)
			{
				gint	pn = gtk_notebook_append_page(GTK_NOTEBOOK(cgu->nbook), simple, NULL);

				gtk_tree_store_append(cgu->store, &cgu->treeiter[cgu->level], NULL);
				gtk_tree_store_set(cgu->store, &cgu->treeiter[cgu->level], 0, name, -1);
				gtk_tree_store_set(cgu->store, &cgu->treeiter[cgu->level], 1, pn, -1);
			}
		}
	}
	hbox  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	/* Build tree view, showing only the first column. */
	cgu->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(cgu->store));
	cr = gtk_cell_renderer_text_new();
	vc = gtk_tree_view_column_new_with_attributes("(ConfigPages)", cr, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(cgu->view), vc);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(cgu->view), FALSE);
	g_signal_connect(G_OBJECT(cgu->view), "cursor_changed", G_CALLBACK(evt_view_cursor_changed), cgu);
	gtk_container_add(GTK_CONTAINER(scwin), cgu->view);
	gtk_box_pack_start(GTK_BOX(hbox), scwin, FALSE, FALSE, 0);

	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scwin), cgu->nbook);
	gtk_box_pack_start(GTK_BOX(hbox), scwin, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(cgu->dlg))), hbox, TRUE, TRUE, 0);
	build_buttons(cgu);

	return cfg_configure(min);		/* Recurse! */
}

/* ----------------------------------------------------------------------------------------- */

static gboolean goto_iterate(const CfgGui *cgu, GtkTreeIter *iter, const char *label)
{
	GtkTreeIter	citer;
	const gchar	*lh;
	gint		lp;

	do
	{
		gtk_tree_model_get(GTK_TREE_MODEL(cgu->store), iter, 0, &lh, 1, &lp, -1);
		if(strcmp(lh, label) == 0)
		{
			GtkTreePath	*path;

			gtk_notebook_set_current_page(GTK_NOTEBOOK(cgu->nbook), lp);
			path = gtk_tree_model_get_path(GTK_TREE_MODEL(cgu->store), iter);
			gtk_tree_view_expand_to_path(GTK_TREE_VIEW(cgu->view), path);
			gtk_tree_view_set_cursor(GTK_TREE_VIEW(cgu->view), path, NULL, FALSE);
			gtk_tree_path_free(path);
			return TRUE;
		}
		if(gtk_tree_model_iter_children(GTK_TREE_MODEL(cgu->store), &citer, iter))
		{
			if(goto_iterate(cgu, &citer, label))
				return TRUE;
		}
	} while(gtk_tree_model_iter_next(GTK_TREE_MODEL(cgu->store), iter));

	return FALSE;
}

/* 2014-12-26 -	Switch to a named configuration page. */
void cfg_goto_page(const char *label)
{
	const CfgGui	*cgu = &the_cfggui;
	GtkTreeIter	iter;

	if(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(cgu->store), &iter))
	{
		goto_iterate(cgu, &iter, label);
	}
}

/* ----------------------------------------------------------------------------------------- */

void cfg_tree_level_begin(const gchar *label)
{
	CfgGui	*cgu = &the_cfggui;

	if(cgu->level + 1 < sizeof cgu->treeiter / sizeof *cgu->treeiter)
	{
		gtk_tree_store_append(cgu->store, &cgu->treeiter[cgu->level], cgu->level > 0 ? &cgu->treeiter[cgu->level - 1] : NULL);
		gtk_tree_store_set(cgu->store, &cgu->treeiter[cgu->level], 0, label, -1);
		cgu->level++;
	}
}

void cfg_tree_level_append(const gchar *label, GtkWidget *page)
{
	CfgGui		*cgu = &the_cfggui;

	gtk_tree_store_append(cgu->store, &cgu->treeiter[cgu->level], &cgu->treeiter[cgu->level - 1]);
	gtk_tree_store_set(cgu->store, &cgu->treeiter[cgu->level], 0, label, -1);

	if(page != NULL)
	{
		gint	pn = gtk_notebook_append_page(GTK_NOTEBOOK(the_cfggui.nbook), page, NULL);
		gtk_tree_store_set(cgu->store, &cgu->treeiter[cgu->level], 1, pn, -1);
	}
}

/* 2002-07-19 -	Replace <old> tree widget with the <new> one. Handy for cases where config widgets
**		are seriously rebuilt during update(), such as in cfg_cmdcfg.c.
*/
void cfg_tree_level_replace(GtkWidget *old, GtkWidget *new)
{
	if(old)
	{
		gint	pn = gtk_notebook_page_num(GTK_NOTEBOOK(the_cfggui.nbook), old);
		gtk_container_remove(GTK_CONTAINER(the_cfggui.nbook), old);
		gtk_notebook_insert_page(GTK_NOTEBOOK(the_cfggui.nbook), new, NULL, pn);
		gtk_widget_show(new);
	}
}

void cfg_tree_level_end(void)
{
	if(the_cfggui.level > 0)
		the_cfggui.level--;
}

/* ----------------------------------------------------------------------------------------- */

static void load_node(const XmlNode *node, gpointer user)
{
	MainInfo	*min = user;
	guint		i;

	for(i = 0; i < CFG_PAGES; i++)
	{
		if(xml_node_has_name(node, cfg_page[i]->node) && cfg_page[i]->load != NULL)
		{
			cfg_page[i]->load(min, node);
			return;
		}
	}
}

/* 1998-07-26 -	Load the entire program configuration. Pretty complex stuff, made considerably
**		less so by the modularization and tree organization.
** 1998-08-30 -	Now keeps knowledge about config file name to itself. First checks if there
**		is a local config; if so, it is loaded. If not, the system-wide default
**		config from /etc/local/etc/ is used. If that fails, whine.
** 1998-10-21 -	Now returns a set of flags, rather than the single first boolean.
** 1999-08-25 -	Made the error dialog shown when no config is found a bit more informative.
*/
guint32 cfg_load_config(MainInfo *min)
{
	XmlNode		*tree;
	gchar		name[PATH_MAX] = "";
	const gchar	*rcdir, *rcname;
	guint32		i, flags = 0UL, bad_dir = 0;

	/* Since we know this function is called during boot, ensure the user's config directory exists. */
	rcdir = get_config_dirname();
	if(*rcdir == '\0')
		g_error("Failed to retrieve user's configuration directory, can't save configuration data");
	else if(!fut_exists(rcdir))
		g_mkdir_with_parents(rcdir, 0700);

	/* Does the user seem to have a local config? */
	rcname = get_config_filename(RCNAME);
	if(!fut_can_read_named(rcname))
	{
		rcname = get_config_filename_old("." RCNAME);	/* Nope, check old location and name. */
		bad_dir = 1;
	}
	if(!fut_can_read_named(rcname))
	{
		g_snprintf(name, sizeof name, PATH_CFG G_DIR_SEPARATOR_S "%s", RCNAME);		/* Nope, check for global one. */
		rcname = name;
		bad_dir = 1;
	}

	/* If loading from a "bad" (non-standard) directory, nag. */
	if(bad_dir)
		ndl_dialog_sync_new_wait(min, "rcpath", _("Configuration Path Notice"), _("Configuration was not loaded from the current default location.\nPress Save in the Configuration window to update."));

	/* Initialize the cache of page descriptors. */
	for(i = 0; i < CFG_PAGES; i++)
		cfg_page[i] = describe_page[i](min);

	if((tree = xml_tree_load(rcname)) != NULL)
	{
		const gchar	*fver;

		if(xml_get_text(tree, "version", &fver) && strcmp(fver, VERSION) != 0)
			g_warning(_("Config file version (%s) doesn't match program version (%s)"), fver, VERSION);
		xml_node_visit_children(tree, load_node, (gpointer) min);
		xml_tree_destroy(tree);
	}
	else
	{
		const gchar	*rcname, *oldrcname;
		gchar		syscfg[PATH_MAX], whine[3 * PATH_MAX];

		rcname = get_config_filename(RCNAME);
		oldrcname = get_config_filename_old("." RCNAME);
		g_snprintf(syscfg, sizeof syscfg, PATH_CFG G_DIR_SEPARATOR_S "%s", RCNAME);
		g_snprintf(whine, sizeof whine, _("Couldn't find any configuration file; checked:\n"
				"\"%s\",\n\"%s\" and\n\"%s\".\n"
				"Using built-in minimal configuration."),
				rcname, oldrcname, syscfg);
		dlg_dialog_async_new_error(whine);
		flags |= CLDF_NONE_FOUND;
	}
	return flags;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-25 -	Log a request to get something done. */
void cfg_set_flags(guint32 flags)
{
	the_cfggui.flags |= flags;
}

/* 1999-04-09 -	Set the 'configuration modified' flag. */
void cfg_modified_set(MainInfo *min)
{
	min->cfg.flags |= CFLG_CHANGED;
}

/* 1999-04-09 -	Clear the 'configuration modified' flag. Use with care. */
void cfg_modified_clear(MainInfo *min)
{
	min->cfg.flags &= ~CFLG_CHANGED;
}
