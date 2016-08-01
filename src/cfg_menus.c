/*
** A module for creating user-defined custom menus. The two immediate uses
** for this is 1) allow configuring the old dir pane menu, and 2) allow
** creation of menus to be bound to command buttons, finally leveraging
** the full power of the special widget used for those.
**
** A full-blown menu editor is a rather big thing, so the goal here is
** for it to grow. We'll see about that.
*/

#include "gentoo.h"

#include "cfg_module.h"

#include "cfg_menus.h"

/* ----------------------------------------------------------------------------------------- */

#define	NODE	"Menus"

typedef struct {
	GtkWidget	*vbox;
	GtkWidget	*scwin;
	GtkWidget	*mcmd[2];		/* Add and delete. */

	MainInfo	*min;			/* Dead handy. */
	gboolean	modified;
	MenuInfo	*menus;
	Menu		*menu;			/* Currently selected menu. */
} P_Menus;

static P_Menus	the_page;

/* ----------------------------------------------------------------------------------------- */

static GtkWidget * init(MainInfo *min, gchar **name)
{
	const gchar	*mlab[] = { "Add Menu", "Delete Menu" };
	P_Menus		*page = &the_page;
	GtkWidget	*hbox;
	guint		i;

	if(name != NULL)
		*name = _("Menus");

	page->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	page->min  = min;	
	page->menu = NULL;

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	for(i = 0; i < sizeof page->mcmd / sizeof page->mcmd[0]; i++)
	{
		page->mcmd[i] = gtk_button_new_with_label(_(mlab[i]));
		gtk_box_pack_start(GTK_BOX(hbox), page->mcmd[i], TRUE, TRUE, 5);
	}
	gtk_box_pack_start(GTK_BOX(page->vbox), hbox, FALSE, FALSE, 5);

	gtk_widget_show_all(page->vbox);
	return page->vbox;
}

/* ----------------------------------------------------------------------------------------- */

static void update(MainInfo *min)
{
	P_Menus	*page = &the_page;

	page->min	= min;
	page->modified	= FALSE;
	page->menus	= mnu_menuinfo_copy(min->cfg.menus);
}

/* ----------------------------------------------------------------------------------------- */

const CfgModule * cmu_describe(MainInfo *min)
{
	static const CfgModule	desc = { NODE, init, update, NULL, NULL, NULL, NULL };

	return &desc;
}

/* 2001-01-01 -	Return editing version of menu info. Dead handy. */
MenuInfo * cmu_get_menuinfo(void)
{
	return the_page.menus;
}
