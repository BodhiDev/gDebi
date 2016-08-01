/*
** 1999-06-12 -	A simple command to show popup menu close the mouse pointer. Must be invoked
**		by a command bound to a mouse button, since we need the triggering GdkEventButton
**		handy.
*/

#include "gentoo.h"

#include "cmdarg.h"
#include "events.h"

#include "menus.h"
#include "cmd_menupopup.h"

/* ----------------------------------------------------------------------------------------- */

#define	CELL_SPACING	1		/* This is a private internal GtkCList constant. Oops. */

/* 2002-08-01 -	This gets called if a "atfocus" bareword was present in the MenuPopup command.
**		So, figure out where the focused row is, and fill in <x> and <y> suitably. This
**		is kind of a hack, since it involves font computations that feel as if they
**		should be inside GTK+, but... It might work. :)
*/
static void func_position(GtkMenu *menu, gint *x, gint *y, gboolean *push, gpointer data)
{
#if 0
	MainInfo	*min = data;
	GtkCList	*cp = min->gui->cur_pane->view;
	gint		wx, wy, px, py, pw, ph, fr;

	gdk_window_get_position(min->gui->window->window, &wx, &wy);
	gdk_window_get_position(GTK_WIDGET(cp)->window, &px, &py);
	gdk_window_get_size(GTK_WIDGET(cp)->window, &pw, &ph);
	*x = (wx + px) + pw / 2;
	*y = (wy + py) + ph / 2;
#endif
	g_warning("Menu popup pane positioning not implemented");
}

/* 2000-04-09 -	Pop up the dirpane menu for the current pane. Now also works if bound to a
**		(keyboard) key, as opposed to only working for mouse buttons as before.
** 2000-12-03 -	Generality-boosted, through the new menu module.
*/
gint cmd_menupopup(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	GdkEventButton		*evt;
	const gchar		*name;
	guint			button = 0;
	guint32			ac_time = 0U;
	GtkMenuPositionFunc	pos = NULL;

	if((evt = (GdkEventButton *) evt_event_get(GDK_BUTTON_PRESS)) != NULL)
	{
		button  = evt->button;
		ac_time = evt->time;
	}
	name = car_keyword_get_value(ca, "menu", "DirPaneMenu");
	if(car_bareword_present(ca, "atfocus"))
		pos = func_position;
	return mnu_menu_popup(min->cfg.menus, name, pos, min, button, ac_time);
}

