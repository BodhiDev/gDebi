/*
** General menu handling routines. This is the stuff made editable through
** the cfg_menus module.
*/

#include "gentoo.h"

#include "cmdseq.h"
#include "dialog.h"
#include "dirpane.h"
#include "fileutil.h"
#include "guiutil.h"
#include "strutil.h"

#include "menus.h"

/* ----------------------------------------------------------------------------------------- */

#define	MENU_NONE		_("(None)")

#define	MENU_ITEM_LABEL_SIZE	32
#define	MENU_ITEM_ICON_SIZE	64

typedef enum {
	ITEM_SIMPLE,
	ITEM_SUBMENU,
	ITEM_SEPARATOR
} MIType;

typedef struct {
	gchar	label[MENU_ITEM_LABEL_SIZE];
	gchar	icon[MENU_ITEM_ICON_SIZE];
} MILabel;

struct item_simple {
	MILabel	label;
	GString	*cmdseq;
};

struct item_submenu {
	MILabel	label;
	gchar	menu[MNU_MENU_NAME_SIZE];
};

struct MenuItem {
	MIType	type;
	union {
	struct item_simple	simple;
	struct item_submenu	submenu;
	}	data;
};

typedef enum {
	MENU_NORMAL = 0,
	MENU_BUILTIN
} MType;

struct Menu {
	MType		type;
	gchar		name[MNU_MENU_NAME_SIZE];
	GArray		*item;
	gboolean	used;		/* Prevents re-use. Simplifies life. */
};

typedef struct {
	gchar	name[MNU_MENU_NAME_SIZE];
	void	(*build)(MainInfo *min, GtkWidget *menu);
} DynMenu;

struct MenuInfo {
	MainInfo	*min;
	GHashTable	*menus;
};

/* ----------------------------------------------------------------------------------------- */

static void	menu_build_action(MainInfo *min, GtkWidget *wid);
static void	menu_build_time(MainInfo *min, GtkWidget *wid);
static void	menu_build_parents(MainInfo *min, GtkWidget *wid);

static GHashTable	*dynamic_hash = NULL;

/* ----------------------------------------------------------------------------------------- */

void mnu_initialize(void)
{
	static const DynMenu	dyn_menu[] = {  { "<ActionMenu>",	menu_build_action },
						{ "<TimeMenu>",		menu_build_time },
						{ "<ParentsMenu>",	menu_build_parents }
	};
	guint			i;

	dynamic_hash = g_hash_table_new(g_str_hash, g_str_equal);
	for(i = 0; i < sizeof dyn_menu / sizeof dyn_menu[0]; i++)
		g_hash_table_insert(dynamic_hash, (gpointer) dyn_menu[i].name, (gpointer) &dyn_menu[i]);
}

/* ----------------------------------------------------------------------------------------- */

Menu * mnu_menu_new(MenuInfo *mi, const gchar *name)
{
	Menu	*m;

	/* Discourage non-unique names. Firmly. */
	if(g_hash_table_lookup(mi->menus, name) != NULL)
		return NULL;

	m = g_malloc(sizeof *m);
	g_strlcpy(m->name, name, sizeof m->name);
	m->item = g_array_new(FALSE, FALSE, sizeof (MenuItem));
	m->used = FALSE;

	g_hash_table_insert(mi->menus, m->name, m);

	return m;
}

/* Destroy a menu. */
void mnu_menu_destroy(MenuInfo *mi, Menu *menu)
{
	g_hash_table_remove(mi->menus, menu);
	g_array_free(menu->item, TRUE);
	g_free(menu);
}

static void menu_append(Menu *m, MenuItem *mi)
{
	m->item = g_array_append_vals(m->item, mi, 1);
}

/* Create a new menu in <mi>, as an exact duplicate of <old> (which
** _really_ should come from a different MenuInfo).
*/
static Menu * mnu_menu_new_copy(MenuInfo *mi, const Menu *old)
{
	Menu	*m;
	guint	i;

	if((m = mnu_menu_new(mi, old->name)) == NULL)
		return m;
	for(i = 0; i < old->item->len; i++)
		menu_append(m, &g_array_index(old->item, MenuItem, i));
	return m;
}

void mnu_menu_append_simple(Menu *m, const gchar *label, const gchar *cmdseq)
{
	MenuItem	mi;

	mi.type = ITEM_SIMPLE;
	g_strlcpy(mi.data.simple.label.label, label, sizeof mi.data.simple.label.label);
	mi.data.simple.label.icon[0] = '\0';
	mi.data.simple.cmdseq = g_string_new(cmdseq);
	menu_append(m, &mi);
}

void mnu_menu_append_separator(Menu *m)
{
	MenuItem	mi;

	mi.type = ITEM_SEPARATOR;
	menu_append(m, &mi);
}

void mnu_menu_append_submenu(Menu *m, const gchar *label, const gchar *menu)
{
	MenuItem	mi;

	mi.type = ITEM_SUBMENU;
	g_strlcpy(mi.data.submenu.label.label, label, sizeof mi.data.submenu.label.label);
	mi.data.submenu.label.icon[0] = '\0';
	g_strlcpy(mi.data.submenu.menu, menu, sizeof mi.data.submenu.menu);
	menu_append(m, &mi);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-08-26 -	Just check if <action> is on the list <actions> or not. We can't use g_slist_find(),
**		since we need to do the comparison by textual content, not pointers.
*/
static gboolean action_on_list(GSList *actions, const gchar *action)
{
	GSList	*iter;

	for(iter = actions; iter != NULL; iter = g_slist_next(iter))
	{
		if(strcmp(iter->data, action) == 0)
			return TRUE;
	}
	return FALSE;
}

/* 1999-08-26 -	Check if <action> is mentioned in each of the styles on <styles> (except <current>).
**		Returns TRUE if so, FALSE if not. This routine really makes my performance-centered
**		mind hurt. I guess I'll have to fix it someday. :)
** 2000-12-10 -	Added quicker way to check if style has a named action property, so now it hurts less.
*/
static gboolean action_in_intersection(GSList *styles, GSList *current, const gchar *action)
{
	const GSList	*siter;

	for(siter = styles; siter != NULL; siter = g_slist_next(siter))
	{
		if(siter == current)
			continue;
		if(!stl_style_property_has_action(siter->data, action))
			return FALSE;
	}
	return TRUE;
}

/* 2000-12-03 -	Dedicated activate-handler for <ActionMenu> items. Call the FileAction built-in. */
static void evt_menu_action_activate(GtkWidget *wid, gpointer user)
{
	MainInfo	*min;

	min = g_object_get_data(G_OBJECT(wid), "user");
	csq_execute_format(min, "FileAction action='%s'", user);
}

/* 2000-12-03 -	Build the Action submenu. It contains names of the actions available in
**		all currently selected files in the current pane.
*/
static void menu_build_action(MainInfo *min, GtkWidget *menu)
{
	DirPane	*dp = min->gui->cur_pane;
	GSList	*slist;

	if((slist = dp_get_selection(dp)) != NULL)
	{
		GSList		*iter, *styles = NULL, *actions = NULL;
		GList		*palist, *piter;
		GtkTreeModel	*tm = dp_get_tree_model(dp);
		Style		*stl;

		/* Build list of all styles used by rows in selection. */
		for(iter = slist; iter != NULL; iter = g_slist_next(iter))
		{
			const FType	*ftype = dp_row_get_ftype(tm, iter->data);

			if(ftype != NULL)
			{
				stl = ftype->style;
				if(g_slist_find(styles, stl) == NULL)
					styles = g_slist_prepend(styles, stl);
			}
		}

		/* Compute intersection of all styles' action properties. */
		for(iter = styles; iter != NULL; iter = g_slist_next(iter))
		{
			palist = stl_style_property_get_actions(iter->data);
			for(piter = palist; piter != NULL; piter = g_list_next(piter))
			{
				if(action_on_list(actions, piter->data))
					continue;
				if(action_in_intersection(styles, iter, piter->data))
					actions = g_slist_insert_sorted(actions, piter->data, (GCompareFunc) strcmp);
			}
			g_list_free(palist);
		}
		g_slist_free(styles);

		if(actions != NULL)
		{
			GtkWidget	*item;

			for(iter = actions; iter != NULL; iter = g_slist_next(iter))
			{
				item = gtk_menu_item_new_with_label(iter->data);
				g_object_set_data(G_OBJECT(item), "user", min);
				g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(evt_menu_action_activate), iter->data);
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
				gtk_widget_show(item);
			}
			g_slist_free(actions);
		}
		dp_free_selection(slist);
	}
	else
	{
		GtkWidget	*item;

		item = gtk_menu_item_new_with_label(_("(No Selection)"));
		gtk_widget_set_sensitive(item, FALSE);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		gtk_widget_show(item);
	}
}

/* 2000-12-03 -	Create a menu with a single item, whose label shows the current
**		local time, in 24-hour HH:MM.SS format. Initially created just to
**		test the dynamic submenu handling, but I think I'll keep it. ;)
**		Selecting the item does nothing.
*/
static void menu_build_time(MainInfo *min, GtkWidget *menu)
{
	gchar		buf[16];
	time_t		now;
	struct tm	*nowf;
	GtkWidget	*item;

	time(&now);
	nowf = localtime(&now);
	strftime(buf, sizeof buf, "%H:%M.%S", nowf);

	item = gtk_menu_item_new_with_label(buf);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	gtk_widget_show(item);
}

/* 2004-04-18 -	User picked an item from the <ParentsMenu> menu. Go there. */
static void evt_menu_parents_activate(GtkWidget *wid, gpointer user)
{
	const gchar	*text = NULL;

	if(!GTK_IS_MENU_ITEM(wid))
		return;
	wid = gtk_bin_get_child(GTK_BIN(wid));
	if(!GTK_IS_LABEL(wid))
		return;
	text = gtk_label_get_text(GTK_LABEL(wid));
	if(text != NULL)
		csq_execute_format(user, "DirEnter 'dir=%s'", stu_escape(text));
}

/* 2004-04-18 -	Build menu containing parent directories of current pane's dir. */
static void menu_build_parents(MainInfo *min, GtkWidget *menu)
{
	const DirPane	*dp = min->gui->cur_pane;
	gchar		canon[PATH_MAX], *sep;
	GtkWidget	*item;

	if(dp == NULL)
		return;
	fut_path_canonicalize(dp->dir.path, canon, sizeof canon);
	while((sep = strrchr(canon, G_DIR_SEPARATOR)) != NULL)
	{
		*sep = '\0';
		if(canon[0] == '\0')
			break;
		item = gtk_menu_item_new_with_label(canon);
		g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(evt_menu_parents_activate), min);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	}
	/* Always append a root ("/") item, since the root has itself as its parent. Avoids empty menu, as bonus. */
	item = gtk_menu_item_new_with_label("/");
	g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(evt_menu_parents_activate), min);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	gtk_widget_show_all(menu);
}

/* ----------------------------------------------------------------------------------------- */

/* The standard callback for a menu item; just run the attached command. */
static void evt_menu_activate(GtkWidget *wid, gpointer user)
{
	MainInfo	*min;

	min = g_object_get_data(G_OBJECT(wid), "user");
	csq_execute(min, user);
}

static void evt_menu_show(GtkWidget *wid, gpointer user)
{
	DynMenu	*dm = user;
	dm->build(g_object_get_data(G_OBJECT(wid), "user"), wid);
}

static void evt_menu_hide(GtkWidget *wid, gpointer user)
{
	GList	*list, *iter;

	for(iter = list = gtk_container_get_children(GTK_CONTAINER(wid)); iter != NULL; iter = g_list_next(iter))
		gtk_container_remove(GTK_CONTAINER(wid), iter->data);
	g_list_free(list);
}

static GtkWidget * do_menu_widget_build(const MenuInfo *mi, const gchar *name, Menu *parent)
{
	DynMenu		*d;
	Menu		*m;
	GtkWidget	*menu, *menuitem, *smenu;
	guint		i;
	MenuItem	*mit;

	if(!name)
		return NULL;

	/* Dynamic menus are empty, and filled-in on demand in the "show" signal handler.
	** This isn't the most pleasing solution performance-wise, but it's still rather
	** elegant, IMO. The "hide" handler empties the menu again.
	*/
	if((d = g_hash_table_lookup(dynamic_hash, name)) != NULL)
	{
		menu = gtk_menu_new();
		g_object_set_data(G_OBJECT(menu), "user", mi->min);
		g_signal_connect(G_OBJECT(menu), "show", G_CALLBACK(evt_menu_show), d);
		g_signal_connect(G_OBJECT(menu), "hide", G_CALLBACK(evt_menu_hide), d);
		return menu;
	}

	if((m = g_hash_table_lookup(mi->menus, name)) == NULL)
		return NULL;

	menu = gtk_menu_new();
	for(i = 0; i < m->item->len; i++)
	{
		mit = &g_array_index(m->item, MenuItem, i);
		switch(mit->type)
		{
			case ITEM_SIMPLE:
				menuitem = gtk_menu_item_new_with_label(mit->data.simple.label.label);
				g_signal_connect(G_OBJECT(menuitem), "activate",
						 G_CALLBACK(evt_menu_activate), mit->data.simple.cmdseq->str);
				g_object_set_data(G_OBJECT(menuitem), "user", mi->min);
				break;
			case ITEM_SEPARATOR:
				menuitem = gtk_menu_item_new();
				break;
			case ITEM_SUBMENU:
				if((smenu = do_menu_widget_build(mi, mit->data.submenu.menu, m)) != NULL)
				{
					menuitem = gtk_menu_item_new_with_label(mit->data.submenu.label.label);
					gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), smenu);
				}
				else
					menuitem = NULL;
				break;
			default:
				menuitem = NULL;
		}
		if(menuitem)
		{
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
			gtk_widget_show(menuitem);
		}
	}
	return menu;
}

/* Get a GTK+ menu representing the named menu. Dynamic content is not generated
** at this point; it is set up (using the "show" and "hide" signals) to be generated
** when (if) needed.
*/
GtkWidget * mnu_menu_widget_get(const MenuInfo *mi, const gchar *name)
{
	return do_menu_widget_build(mi, name, NULL);
}

GtkWidget * mnu_menu_use(const MenuInfo *mi, const gchar *name)
{
	return do_menu_widget_build(mi, name, NULL);
}

void mnu_menu_disuse(const MenuInfo *mi, const gchar *name)
{
	Menu	*m;

	if((m = g_hash_table_lookup(mi->menus, name)) == NULL)
		return;
	m->used = FALSE;
}

/* ----------------------------------------------------------------------------------------- */

static gboolean evt_popup_destroy(GtkWidget *wid, GdkEvent *evt, gpointer user)
{
	gui_events_flush();

	return FALSE;
}

static void evt_popup_hide(GtkWidget *wid, gpointer user)
{
	gboolean	ret;

	gui_events_flush();
	g_signal_emit_by_name(G_OBJECT(wid), "destroy_event", NULL, &ret);
}

gboolean mnu_menu_popup(const MenuInfo *mi, const gchar *name, GtkMenuPositionFunc pos, gpointer data, guint button, guint32 activate_time)
{
	GtkWidget	*menu;

	if((menu = mnu_menu_widget_get(mi, name)) != NULL)
	{
		g_signal_connect_after(G_OBJECT(menu), "hide", G_CALLBACK(evt_popup_hide), NULL);
		g_signal_connect(G_OBJECT(menu), "destroy_event", G_CALLBACK(evt_popup_destroy), NULL);
		gtk_menu_popup(GTK_MENU(menu), NULL, NULL, pos, data, button, activate_time);
/*		fprintf(stderr, "Popping up menu at %p\n", menu);*/
	}
	return menu != NULL;
}

/* ----------------------------------------------------------------------------------------- */

MenuInfo * mnu_menuinfo_new(MainInfo *min)
{
	MenuInfo	*mi;

	mi = g_malloc(sizeof *mi);

	mi->min	  = min;
	mi->menus = g_hash_table_new(g_str_hash, g_str_equal);

	return mi;
}

MenuInfo * mnu_menuinfo_new_default(MainInfo *min)
{
	MenuInfo	*mi;
	Menu		*menu;

	mi = mnu_menuinfo_new(min);

	menu = mnu_menu_new(mi, "SelectMenu");
	mnu_menu_append_simple(menu, _("All"),       "SelectAll");
	mnu_menu_append_simple(menu, _("None"),      "SelectNone");
	mnu_menu_append_simple(menu, _("Toggle"),    "SelectToggle");
	mnu_menu_append_simple(menu, _("RegExp..."), "SelectRE");

/*	menu = mnu_menu_new(mi, "PointlessComplexityMenu");
	mnu_menu_append_submenu(menu, "Action2", "<ActionMenu>");
	mnu_menu_append_submenu(menu, "Action2", "<ActionMenu>");
	mnu_menu_append_submenu(menu, "Action2", "<ActionMenu>");
	mnu_menu_append_submenu(menu, "Action2", "<ActionMenu>");
	mnu_menu_append_submenu(menu, "Action2", "<ActionMenu>");
	mnu_menu_append_submenu(menu, "Action2", "<ActionMenu>");
*/
	menu = mnu_menu_new(mi, "DirPaneMenu");
	mnu_menu_append_simple(menu, _("Parent"), "DirParent");
	mnu_menu_append_simple(menu, _("Other"),  "DirFromOther");
	mnu_menu_append_simple(menu, _("Rescan"), "DirRescan");
	mnu_menu_append_submenu(menu, _("Select"), "SelectMenu");
	mnu_menu_append_separator(menu);
	mnu_menu_append_submenu(menu, _("Action"), "<ActionMenu>");
/*	mnu_menu_append_submenu(menu, "Time", "<TimeMenu>");*/
	mnu_menu_append_simple(menu, _("Run..."), "Run");
	mnu_menu_append_separator(menu);
	mnu_menu_append_simple(menu, _("Configure..."), "Configure");
/*	mnu_menu_append_submenu(menu, "Pointless", "PointlessComplexityMenu");*/

/*	menu = mnu_menu_new(mi, "ShortcutMenu");
	mnu_menu_append_simple(menu, "Home", "DirEnter ~");
	mnu_menu_append_simple(menu, "Local", "DirEnter /usr/local");
	mnu_menu_append_simple(menu, "/", "DirEnter /");
*/
	return mi;
}

static void copy_menu(gpointer key, gpointer value, gpointer user)
{
	mnu_menu_new_copy(user, value);
}

MenuInfo * mnu_menuinfo_copy(const MenuInfo *mi)
{
	MenuInfo	*nmi;

	nmi = mnu_menuinfo_new(mi->min);
	g_hash_table_foreach(mi->menus, copy_menu, nmi);

	return nmi;
}

#if 0
static void append_menu(gpointer key, gpointer value, gpointer user)
{
	GList	**list = user;
	Menu	*m = value;

	if(m->used)
		return;
	*list = g_list_insert_sorted(*list, m->name, (GCompareFunc) strcmp);
}

static void evt_select_select_row(GtkWidget *wid, gint row, gint col, GdkEventButton *evt, gpointer user)
{
	gchar	**selected = user;

	*selected = gtk_clist_get_row_data(GTK_CLIST(wid), row);
}

static void evt_select_unselect_row(GtkWidget *wid, gint row, gint col, GdkEventButton *evt, gpointer user)
{
	gchar	**selected = user;

	*selected = NULL;
}

/* 2001-01-01 -	Pop up dialog, let user pick a non-used menu. */
const gchar * mnu_menuinfo_select(const MenuInfo *mi)
{
	GList		*list = NULL, *iter;
	GtkWidget	*scwin, *clist;
	gchar		*row[] = { NULL }, *selected = NULL;
	gint		ri, choice;
	Dialog		*dlg;

	list = g_list_append(list, MENU_NONE);
	g_hash_table_foreach(mi->menus, append_menu, &list);

	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	clist = gtk_clist_new(1);
	for(iter = list; iter != NULL; iter = g_list_next(iter))
	{
		row[0] = iter->data;
		ri = gtk_clist_append(GTK_CLIST(clist), row);
		gtk_clist_set_row_data(GTK_CLIST(clist), ri, row[0]);
	}
	g_signal_connect(G_OBJECT(clist), "select_row", G_CALLBACK(evt_select_select_row), &selected);
	g_signal_connect(G_OBJECT(clist), "unselect_row", G_CALLBACK(evt_select_unselect_row), &selected);
	gtk_container_add(GTK_CONTAINER(scwin), clist);
	gtk_widget_set_size_request(scwin, 200, 384);

	dlg = dlg_dialog_sync_new(scwin, _("Select Menu"), NULL);
	choice = dlg_dialog_sync_wait(dlg);
	dlg_dialog_sync_destroy(dlg);

	g_list_free(list);

	return choice == 0 ? (selected ? selected : MENU_NONE) : NULL;
}
#endif

static void destroy_menu(gpointer key, gpointer value, gpointer user)
{
/*	printf("Destroying menu \"%s\"\n", ((Menu *) value)->name);*/
	mnu_menu_destroy(user, value);
}

void mnu_menuinfo_destroy(MenuInfo *mi)
{
	g_hash_table_foreach(mi->menus, destroy_menu, mi);
	g_free(mi);
}
