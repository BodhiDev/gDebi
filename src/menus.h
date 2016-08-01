/*
** Menu utility module header.
*/

#if !defined MENUS_H
#define	MENUS_H

/* ----------------------------------------------------------------------------------------- */

typedef struct MenuItem	MenuItem;
typedef struct Menu	Menu;
typedef struct MenuInfo	MenuInfo;

/* This is a public constant, hence the prefix. It's used by buttons.c. */
#define	MNU_MENU_NAME_SIZE	32

/* ----------------------------------------------------------------------------------------- */

extern void		mnu_initialize(void);

extern Menu *		mnu_menu_new(MenuInfo *mi, const gchar *name);
extern void		mnu_menu_destroy(MenuInfo *mi, Menu *menu);

extern void		mnu_menu_append_simple(Menu *m, const gchar *label, const gchar *cmd);
extern void		mnu_menu_append_separator(Menu *m);
extern void		mnu_menu_append_submenu(Menu *m, const gchar *label, const gchar *link);

extern GtkWidget *	mnu_menu_use(const MenuInfo *mi, const gchar *name);
extern void		mnu_menu_disuse(const MenuInfo *mi, const gchar *name);

extern GtkWidget *	mnu_menu_widget_get(const MenuInfo *mi, const gchar *name);
extern gboolean		mnu_menu_popup(const MenuInfo *mi, const gchar *name, GtkMenuPositionFunc pos,
					gpointer data, guint button, guint32 activate_time);
/* ----------------------------------------------------------------------------------------- */

extern MenuInfo *	mnu_menuinfo_new(MainInfo *min);
extern MenuInfo *	mnu_menuinfo_new_default(MainInfo *min);
extern MenuInfo *	mnu_menuinfo_copy(const MenuInfo *mi);

extern const gchar *	mnu_menuinfo_select(const MenuInfo *mi);

extern void		mnu_menuinfo_destroy(MenuInfo *mi);

#endif		/* MENUS_H */
