/*
** 1998-05-17 -	I just have to build an Opus-look-alike GTK GUI! :)
** 1998-09-11 -	Er, it like, grew, or something.
*/

#include "gentoo.h"

#include <dlfcn.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <sys/wait.h>

#if defined ENABLE_NLS
#include <locale.h>
#endif

#include "errors.h"
#include "configure.h"
#include "dirpane.h"
#include "dialog.h"
#include "userinfo.h"
#include "file.h"
#include "fileutil.h"
#include "gfam.h"
#include "guiutil.h"
#include "xmlutil.h"
#include "strutil.h"
#include "types.h"
#include "styles.h"
#include "sizeutil.h"
#include "buttons.h"
#include "buttonlayout.h"
#include "queue.h"
#include "cmdseq.h"
#include "children.h"
#include "dpformat.h"
#include "keyboard.h"
#include "controls.h"
#include "iconutil.h"
#include "cmdseq_config.h"
#include "menus.h"
#include "nag_dialog.h"

#include "cfg_module.h"
#include "cfg_nag.h"
#include "cfg_windows.h"

/* ----------------------------------------------------------------------------------------- */

/* 1998-05-18 -	Filter out files the user really doesn't want to see, and that recursive
**		directory traversing code really, really, REALLY, doesn't.
*/
static gboolean dir_filter(const gchar *name)
{
	if((strcmp(name, ".") == 0) || (strcmp(name, "..") == 0))
		return FALSE;
	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 2000-03-18 -	User clicked close button on window. Just react as if the Quit command was run. */
static gboolean evt_main_delete(GtkWidget *wid, GdkEventAny *evt, gpointer user)
{
	csq_execute(user, "Quit");

	return TRUE;
}

/* 1998-10-26 -	Initialize some fields in the dp structure. */
static void init_pane(DirPane *dp, gint index)
{
	dp->index = index;
	dp->complete.prefix[0] = '\0';
	dp->dir.stat = NULL;
	dp->dir.stat_alloc = 0;
	dp->dir.stat_use   = 0;
	dp->hist = dph_dirhistory_new();
	dp->dir.path[0] = '\0';
}

static void evt_paned_notify_position(GObject *obj, GParamSpec *spec, gpointer user)
{
	MainInfo	*min = user;
	GdkWindow	*pwin;
	gint		pos, np;

	if(!dp_realized(min))
		return;

	pos = gtk_paned_get_position(GTK_PANED(obj));
	pwin = gtk_widget_get_window(min->gui->panes);
	np = min->cfg.dp_paning.orientation == DPORIENT_HORIZ ? gdk_window_get_width(pwin) : gdk_window_get_height(pwin);
	switch(min->cfg.dp_paning.mode)
	{
		case DPSPLIT_FREE:
			break;
		case DPSPLIT_RATIO:
			min->cfg.dp_paning.value = (gdouble) pos / np;
			break;
		case DPSPLIT_ABS_LEFT:
			min->cfg.dp_paning.value = pos;
			break;
		case DPSPLIT_ABS_RIGHT:
			min->cfg.dp_paning.value = np - pos;
			break;
	}
}

/* 1998-05-17 -	Build a couple of dir-panes.
** 1998-05-18 -	Now also puts the panes in a horizontally paned window, for extra Opusity. ;^)
** 1998-08-02 -	Fixed *huge* bug where pane 1 (the right) was built using pane 0's format!!
*/
static GtkWidget * build_dirpanes(MainInfo *min)
{
	GtkWidget	*pane;
	GuiInfo		*gui;

	gui = min->gui;

	gui->panes = gtk_paned_new(min->cfg.dp_paning.orientation == DPORIENT_HORIZ ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL);
	init_pane(&gui->pane[0], 0);
	pane = dp_build(min, &min->cfg.dp_format[0], &gui->pane[0]);
	gtk_paned_add1(GTK_PANED(gui->panes), pane);
	init_pane(&gui->pane[1], 1);
	pane = dp_build(min, &min->cfg.dp_format[1], &gui->pane[1]);
	gtk_paned_add2(GTK_PANED(gui->panes), pane);
	gui->sig_pane_notify = g_signal_connect(G_OBJECT(gui->panes), "notify::position", G_CALLBACK(evt_paned_notify_position), min);
	gtk_widget_show(gui->panes);

	gtk_widget_grab_focus(min->gui->pane[0].view);

	return gui->panes;
}

static void evt_top_button_press_event(GtkWidget *top, GdkEventButton *evt, gpointer user)
{
	if(evt->button == 1 && evt->type == GDK_2BUTTON_PRESS)
		csq_execute(user, "About");
}

/* 1998-05-19 -	Create the top widgetry, returning something the caller can just add to a box. Sets the
**		<label> pointer to a GtkLabel which can be used later to display funny messages and stuff.
**		The label is internally wrapped in a GtkEventbox, which makes updating it possible without
**		causing instant epilepsy in everyone watching. GTK+ really is smooth.
** 1998-11-26 -	Made the label focusable (?), in order to *finally* have somewhere safe to put the focus
**		when I don't want it on path entry widgets. Nice.
** 2010-10-03 -	Added sneaky click-listener, so that we can run About on double-click.
*/
static GtkWidget * build_top(MainInfo *min)
{
	GtkLabel	**label = (GtkLabel **) &min->gui->top;
	GtkWidget	*top, *lab;

	if(min->cfg.errors.display == ERR_DISPLAY_TITLEBAR)
	{
		if(*label != NULL)
			gtk_widget_destroy(GTK_WIDGET(*label));
		*label = NULL;
		return NULL;
	}

	top = gtk_event_box_new();
	lab = gtk_label_new("");
	gtk_container_add(GTK_CONTAINER(top), lab);
	gtk_widget_show_all(top);
	if(label != NULL)
		*label = GTK_LABEL(lab);
	g_signal_connect(G_OBJECT(top), "button_press_event", G_CALLBACK(evt_top_button_press_event), min);

	return top;
}

/* 2002-05-31 -	Rewritten yet again, now using the first weak version of a specialized button layout module. */
static GtkWidget * build_bottom(MainInfo *min)
{
	GtkWidget	*hbox, *sb, *cb;

	if((sb = btn_buttonsheet_build(min, &min->cfg.buttons, "Shortcuts", FALSE, NULL, NULL)) != NULL)
		btn_buttonsheet_built_add_keys(min, GTK_CONTAINER(sb), NULL);
	if((cb = btn_buttonsheet_build(min, &min->cfg.buttons, NULL, FALSE, NULL, min)) != NULL)
		btn_buttonsheet_built_add_keys(min, GTK_CONTAINER(cb), NULL);
	if((hbox = btl_buttonlayout_pack(min->cfg.buttonlayout, cb, sb)) != NULL)
		gtk_widget_show_all(hbox);

	return hbox;
}

void rebuild_top(MainInfo *min)
{
	GtkWidget	*top;

	if(min->gui->top != NULL)
	{
		gtk_widget_destroy(min->gui->top);
		min->gui->top = NULL;
	}
	top = build_top(min);
	if(top != NULL)
	{
		gtk_box_pack_start(GTK_BOX(min->gui->vbox), top, FALSE, FALSE, 0);
		gtk_box_reorder_child(GTK_BOX(min->gui->vbox), top, 0);
	}
	dp_show_stats(min->gui->cur_pane);
	gui_set_main_title(min, NULL);
}

/* 1998-10-26 -	Rebuild the middle part of the GUI, i.e. the panes. */
void rebuild_middle(MainInfo *min)
{
	GtkWidget	*left, *right;

	gtk_container_remove(GTK_CONTAINER(min->gui->panes), min->gui->pane[0].vbox);
	gtk_container_remove(GTK_CONTAINER(min->gui->panes), min->gui->pane[1].vbox);
	gtk_widget_destroy(min->gui->panes);
	min->gui->panes = gtk_paned_new(min->cfg.dp_paning.orientation == DPORIENT_HORIZ ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL);
	gtk_box_pack_start(GTK_BOX(min->gui->vbox), min->gui->panes, TRUE, TRUE, 0);
	gtk_box_reorder_child(GTK_BOX(min->gui->vbox), min->gui->panes, 1);
	if((left = dp_build(min, &min->cfg.dp_format[0], &min->gui->pane[0])) != NULL)
		gtk_paned_add1(GTK_PANED(min->gui->panes), left);
	if((right = dp_build(min, &min->cfg.dp_format[1], &min->gui->pane[1])) != NULL)
		gtk_paned_add2(GTK_PANED(min->gui->panes), right);
	min->gui->sig_pane_notify = g_signal_connect(G_OBJECT(min->gui->panes), "notify::position", G_CALLBACK(evt_paned_notify_position), min);
	gtk_widget_show(min->gui->panes);
	dp_split_refresh(min);
}

/* 1998-07-14 -	Rebuild the bottom part of the GUI, whose main responsibility is to contain
**		the button bank(s).
*/
void rebuild_bottom(MainInfo *min)
{
	gtk_widget_destroy(min->gui->bottom);
	if((min->gui->bottom = build_bottom(min)) != NULL)
	{
		gtk_box_pack_start(GTK_BOX(min->gui->vbox), min->gui->bottom, FALSE, FALSE, 0);
		gtk_widget_show(min->gui->bottom);
	}
}

/* 2008-04-20 -	Replaced old size_allocation-tracking code with new, using configure events instead. */
static gboolean evt_main_configure(GtkWidget *wid, GdkEventConfigure *req, gpointer user)
{
	dp_split_refresh(user);

	return FALSE;	/* Keep propagating. */
}

static GtkWidget * build_gui(MainInfo *min)
{
	GtkWidget	*top, *panes, *bottom;

	min->gui = g_malloc(sizeof *min->gui);
	min->gui->window = NULL;
	min->gui->window = win_window_open(min->cfg.wininfo, WIN_MAIN);
	gtk_widget_set_name(min->gui->window, "gentoo");
	g_object_set_data(G_OBJECT(min->gui->window), "user", min);
	min->gui->sig_main_configure = g_signal_connect(G_OBJECT(min->gui->window), "configure_event", G_CALLBACK(evt_main_configure), min);
	min->gui->sig_main_delete = g_signal_connect(G_OBJECT(min->gui->window), "delete_event", G_CALLBACK(evt_main_delete), min);
	min->gui->pane[0].main = min->gui->pane[1].main = min;
	min->gui->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	min->gui->kbd_ctx = kbd_context_new(min);

	top = build_top(min);
	if((panes = build_dirpanes(min)) != NULL)
	{
		bottom = build_bottom(min);
		if(top != NULL)
			gtk_box_pack_start(GTK_BOX(min->gui->vbox), top,    FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(min->gui->vbox), panes,  TRUE,  TRUE,  0);
		if(bottom != NULL)
			gtk_box_pack_start(GTK_BOX(min->gui->vbox), bottom, FALSE, FALSE, 0);
		gtk_widget_show_all(min->gui->vbox);
		min->gui->middle = panes;
		min->gui->bottom = bottom;
		min->gui->cur_pane = &min->gui->pane[0];
		kbd_context_attach(min->gui->kbd_ctx, GTK_WINDOW(min->gui->window));
		ctrl_keys_install(min->cfg.ctrlinfo, min->gui->kbd_ctx);
		return min->gui->vbox;
	}
	gtk_widget_destroy(top);
	gtk_widget_destroy(min->gui->vbox);

	return NULL;
}

/* 1998-08-23 -	Initialize the paths config data. */
static void init_paths(CfgInfo *cfg)
{
	const gchar	*confdir;

	cfg->path.path[PTID_ICON]  = g_string_new(PATH_ICN);
	if((confdir = g_get_user_config_dir()) != NULL)
	{
		cfg->path.path[PTID_GTKRC] = g_string_new(confdir);
		g_string_append(cfg->path.path[PTID_GTKRC], G_DIR_SEPARATOR_S PACKAGE);
	}
	else
		cfg->path.path[PTID_GTKRC] = g_string_new(PATH_GRC);
	cfg->path.path[PTID_FSTAB] = g_string_new("/etc/fstab");
	cfg->path.path[PTID_MTAB]  = g_string_new("/proc/mounts");

	cfg->path.hideinfo.mode = HIDE_NONE;
	cfg->path.hideinfo.hide_re_src[0] = '\0';
	cfg->path.hideinfo.hide_re = NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-11-29 -	Load the GTK+ RC file, allowing users to configure gentoo's looks. The
**		file is always named ".gentoogtkrc" (too long, I know) but you can put
**		it anywhere as long as you given gentoo the path (in the config).
** 2001-08-12 -	The dot is now optional, but is given priority if found.
** 2011-07-24 -	Now we even prefer "gtkrc", without "gentoo", to match .config/gentoo location.
** 2012-05-02 -	Ported to use GtkCssProvider API, for GTK+ 3.0.
*/
static void load_gtk_rc(MainInfo *min)
{
	const gchar	*names[] = { "gtkrc", "gentoogtkrc", ".gentoogtkrc" };
	const gchar	*name;
	gsize		i;

	for(i = 0; i < sizeof names / sizeof *names; i++)
	{
		if((name = fut_locate(min->cfg.path.path[PTID_GTKRC]->str, names[i])) != NULL)
		{
			GtkCssProvider	*prov;

			if((prov = gtk_css_provider_new()) != NULL)
			{
				if(gtk_css_provider_load_from_path(prov, name, NULL))
				{
					GdkScreen	*scr = gtk_widget_get_screen(min->gui->window);

					gtk_style_context_add_provider_for_screen(scr, GTK_STYLE_PROVIDER(prov), GTK_STYLE_PROVIDER_PRIORITY_THEME);
					break;
				}
			}
		}
	}
}

/* 1999-04-04 -	Initialize default (er, and non-configurable) command options. I'll
**		build a GUI for this stuff real soon now.
*/
static void init_cmd_options(CfgInfo *cfg)
{
	cfg->opt_overwrite.show_info = TRUE;
	g_strlcpy(cfg->opt_overwrite.datefmt, "%Y-%m-%d %H:%M.%S", sizeof cfg->opt_overwrite.datefmt);
}

#if 0
/* 1999-05-10 -	This is for temporary key checks during development. Simpler to get a trigger this way
**		than to hack the controls and/or keyboard modules. Is that a sign of bad code there? Naah. :)
*/
#include <gdk/gdkkeysyms.h>
static void evt_key_press(GtkWidget *wid, GdkEventKey *evt, gpointer user)
{
	MainInfo	*min = user;

	printf("Someone pressed %d ('%c'), state=%04X\n", evt->keyval, evt->keyval, evt->state);

	if(evt->keyval == GDK_v)
	{
		GtkAdjustment	*adj;

		if((adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(min->gui->cur_pane->scwin))) != NULL)
			printf("Adj: lower=%g upper=%g value=%g\n", adj->lower, adj->upper, adj->value);
	}
	else if(evt->keyval == GDK_z)
		dp_unfocus(min->gui->cur_pane);
	else if(evt->keyval == GDK_d)
	{
		Dialog	*dlg;
		gint	ret;

		dlg = dlg_dialog_sync_new(NULL, "Hello", "_A|_B");
		ret = dlg_dialog_sync_wait(dlg);
		dlg_dialog_sync_destroy(dlg);
	}
	else if(evt->keyval == GDK_1)
	{
		min->cfg.dp_paning.mode++;
		if(min->cfg.dp_paning.mode > DPSPLIT_ABS_RIGHT)
			min->cfg.dp_paning.mode = DPSPLIT_FREE;
	}
	else if(evt->keyval == GDK_0)
		gtk_widget_hide(min->gui->pane[0].scwin);
	else if(evt->keyval == GDK_plus)
		gtk_widget_show(min->gui->pane[0].scwin);
}
#endif

/* 2002-02-19 -	Resolve what the initial directory is going to be, and go there. */
static gboolean enter_initial_dirs(MainInfo *min, const gchar *dirleft, const gchar *dirright)
{
	const gchar	*dir;
	const gchar	*odir[2];
	const gchar	*cmd[] = { "ActivateLeft", "ActivateRight" };
	gint		i;
	gboolean	ok = TRUE;

	odir[0] = dirleft;
	odir[1] = dirright;
	for(i = sizeof cmd / sizeof *cmd - 1; i >= 0; i--)
	{
		if((dir = odir[i]) == NULL)
		{
			dir = min->cfg.dp_format[i].def_path;
			if(!dir[0])
				dir = dph_history_get_first(&min->gui->pane[i]);
		}
		if(dir == NULL)
			dir = usr_get_home();
		csq_execute(min, cmd[i]);
		ok &= csq_execute_format(min, "DirEnter 'dir=%s'", stu_escape(dir)) != 0;
	}

	return ok;
}

/* 2003-11-25 -	An idle handler that should only trigger once, to run commands from command line. */
static gint evt_idle_run(gpointer user)
{
	MainInfo	*min = user;

	if(min->run_commands != NULL)
	{
		gsize	i;

		for(i = 0; min->run_commands[i] != NULL; i++)
			csq_execute(min, min->run_commands[i]);
	}

	return FALSE;		/* Causes the handler to be removed, once really is enough. */
}

static gint cmp_list_commands(gconstpointer a, gconstpointer b)
{
	return strcmp(a, b);
}

static void cb_list_commands(gpointer key, gpointer value, gpointer user)
{
	GList	**head = user;

	*head = g_list_insert_sorted(*head, key, cmp_list_commands);
}

static void do_list_commands(MainInfo *min)
{
	GList	*cmdseq = NULL, *iter;

	g_hash_table_foreach(min->cfg.commands.builtin, cb_list_commands, &cmdseq);
	for(iter = cmdseq; iter != NULL; iter = g_list_next(iter))
		printf("%s\n", (const gchar *) iter->data);
}

int main(int argc, char *argv[])
{
	static MainInfo	min = { NULL };

	gboolean	show_version = FALSE;
	gboolean	root_ok = FALSE;
	gboolean	no_rc = FALSE, no_gtk_rc = FALSE, no_dir_history = FALSE, list_commands = FALSE;
	gchar		*dir_left = NULL;
	gchar		*dir_right = NULL;
#if defined ENABLE_NLS
	gboolean	show_locale_info = FALSE;
	gint		i;
#endif
	GOptionEntry	option_entries[] = {
	{ "version", 0, 0, G_OPTION_ARG_NONE, &show_version,  N_("Report the version to standard output, and exit"), NULL },
#if defined ENABLE_NLS
	{ "locale-info", 0, 0, G_OPTION_ARG_NONE, &show_locale_info, N_("Report internal locale details, and exit"), NULL },
#endif
	{ "root-ok", 0, 0, G_OPTION_ARG_NONE, &root_ok, N_("Allows gentoo to be run by the root user. Could be dangerous!"), NULL },
	{ "no-rc", 0, 0, G_OPTION_ARG_NONE, &no_rc, N_("Do not load the ~/.config/gentoo/gentoorc configuration file; instead, use default values"), NULL },
	{ "no-gtk-rc", 0, 0, G_OPTION_ARG_NONE, &no_gtk_rc, N_("Do not load the ~/.config/gentoo/gtkrc GTK+ configuration file; instead, use system defaults"), NULL },
	{ "no-dir-history", 0, 0, G_OPTION_ARG_NONE, &no_dir_history, N_("Do not load the ~/.config/gentoo/dirhistory file; instead, start with empty history"), NULL },
	{ "run", 'r', 0, G_OPTION_ARG_STRING_ARRAY, &min.run_commands, N_("Run COMMAND, a gentoo command. Done before user interaction allowed, but after configuration "
			      "file has been read in. Can be used many times to run several commands in sequence"), N_("COMMAND") },
	{ "left", '1', 0, G_OPTION_ARG_STRING, &dir_left, N_("Use DIR as path for the left directory pane. Overrides default (and history)"), N_("DIR") },
	{ "right", '2', 0, G_OPTION_ARG_STRING, &dir_right, N_("Use DIR as path for the right directory pane. Overrides default (and history)"), N_("DIR") },
	{ "list-commands", 0, 0, G_OPTION_ARG_NONE, &list_commands, N_("Print a list of all built-in commands, and exit"), NULL },
	{ NULL }
	};
	GtkWidget	*box;
	GOptionContext	*context;
	GError		*err = NULL;

#if defined ENABLE_NLS
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	/* Translate help texts for command line options. */
	for(i = 0; option_entries[i].long_name != NULL; i++)
	{
		option_entries[i].description = _(option_entries[i].description);
		option_entries[i].arg_description = _(option_entries[i].arg_description);
	}
#endif

	/* Use glib's option parser, for consistency, features and just less code in general. Nice. */
	context = g_option_context_new(_("- a graphical file manager using GTK+"));
	g_option_context_add_main_entries(context, option_entries, PACKAGE);
	g_option_context_add_group(context, gtk_get_option_group(TRUE));	/* Import GTK+'s options. */
	if(!g_option_context_parse(context, &argc, &argv, &err))
	{
		g_print(_("Failed to parse command line options: %s\n"), err->message);
		return EXIT_FAILURE;
	}

	/* Must be run before application-level options parsing. */
	gtk_init(&argc, &argv);

	min.vfs.vfs = g_vfs_get_default();

	if(show_version)
	{
		puts(VERSION);
		return EXIT_SUCCESS;
	}
#if defined ENABLE_NLS
	if(show_locale_info)
	{
		const gchar	*enc, *cset, **fenc;

		printf("PACKAGE=\"%s\"\n", PACKAGE);
		printf("LOCALEDIR=\"%s\"\n", LOCALEDIR);

		enc = g_getenv("G_FILENAME_ENCODING");
		if(enc == NULL)
			enc = "(not set)";
		printf("G_FILENAME_ENCODING=\"%s\"\n", enc);

		g_get_filename_charsets(&fenc);
		printf("Filename encoding: \"%s\"\n", (fenc != NULL && *fenc != NULL) ? *fenc : "(unknown)");

		g_get_charset(&cset);
		if(cset == NULL)
			cset = "(unknown)";
		printf("Native charset: \"%s\"\n", cset);
		return EXIT_SUCCESS;
	}
#endif

	if(geteuid() == 0 && !root_ok)
	{
		fprintf(stderr, _("%s: To allow running as root, use the '--root-ok' option\n"), argv[0]);
		return EXIT_FAILURE;
	}

	dpf_initialize();
	mnu_initialize();

	min.cfg.flags = 0;
	dpf_init_defaults(&min.cfg);
	csq_init_commands(&min);

	if(list_commands)
	{
		do_list_commands(&min);
		return EXIT_SUCCESS;
	}

	min.cfg.menus = mnu_menuinfo_new_default(&min);
	btn_buttoninfo_new_default(&min, &min.cfg.buttons);
	min.cfg.buttonlayout = btl_buttonlayout_new();
	init_paths(&min.cfg);
	min.cfg.style = stl_styleinfo_default();
	typ_init(&min.cfg);
	min.cfg.wininfo	 = win_wininfo_new_default(&min);
	min.cfg.ctrlinfo = ctrl_new_default(&min);
	min.cfg.errors.display = ERR_DISPLAY_STATUSBAR;
	cng_initialize(&min.cfg.nag);
	min.cfg.dir_filter = dir_filter;
	chd_initialize(&min);
	min.que = que_initialize();

	min.ico = ico_initialize(&min);

	min.cfg.flags = 0;
	if(!no_rc)
		cfg_load_config(&min);
	if(!usr_init())
		g_warning(_("Couldn't initialize userinfo module - username resolving won't work"));

	if((box = build_gui(&min)) != NULL)
	{
		dp_initialize(min.gui->pane, sizeof min.gui->pane / sizeof *min.gui->pane);

		if(!no_gtk_rc)
			load_gtk_rc(&min);

		fam_initialize(&min);

		dlg_main_window_set(GTK_WINDOW(min.gui->window));
		dlg_position_set(min.cfg.dialogs.pos);
#if 0
		g_signal_connect(G_OBJECT(min.gui->window), "key_press_event", G_CALLBACK(evt_key_press), &min);
#endif
		init_cmd_options(&min.cfg);

		gtk_container_add(GTK_CONTAINER(min.gui->window), box);

		if(!no_dir_history)
			dph_history_load(&min, min.gui->pane, sizeof min.gui->pane / sizeof *min.gui->pane);

		gtk_window_set_resizable(GTK_WINDOW(min.gui->window), TRUE);

		win_window_show(min.gui->window);

		err_clear(&min);
		if(enter_initial_dirs(&min, dir_left, dir_right))
		{
			if(min.gui->top != NULL)
			{
				gchar	buf[128];

				g_snprintf(buf, sizeof buf, _("gentoo v%s by Emil Brink <emil@obsession.se>"), VERSION);
				gtk_label_set_text(GTK_LABEL(min.gui->top), buf);
			}
		}
		else
			err_show(&min);

		ndl_dialog_sync_new_wait(&min, "gio-warning", _("Development Version Warning"), _("This version of gentoo is considered somewhat new and untested.\nThere have been major changes to almost all parts of the program since the previous version.\nPlease be a bit careful, and make sure you report any problem to the author. Thanks."));

		g_idle_add(evt_idle_run, &min);
		gtk_main();
	}
	chd_kill_children();
	fam_shutdown(&min);

	return EXIT_SUCCESS;
}
