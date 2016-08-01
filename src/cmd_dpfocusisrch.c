/*
** 2003-11-04 -	Incremental search. Better late than never, right?
*/

#include <ctype.h>
#include <stdlib.h>

#include <gdk/gdkkeysyms.h>

#include "gentoo.h"
#include "cmdseq.h"
#include "dialog.h"
#include "dirpane.h"

#include "cmd_dpfocusisrch.h"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	GtkWidget	*hbox;			/* Really should start with hbox. */
	GtkWidget	*label, *entry;
} ISearchWidgetry;

/* Various state, kept as a static global. Ugly, but simplifies things. */
static struct {
	DirPane		*pane;				/* Non-NULL during ISearch, NULL when not in use. */
	guint		page;
	MainInfo	*min;
	char *	(*search)(const char *haystack, const char *needle);
	gboolean	offset_use;
	guint		offset;
	gboolean	select;
	ISearchWidgetry	*wid;

	gulong		sig_activate, sig_focus_out, sig_key_press;
} the_isearch_info = { NULL };

/* ----------------------------------------------------------------------------------------- */

/* 2003-11-08 -	Close down incremental search. Hides widgetry. */
static void isearch_close(void)
{
	MainInfo	*min = the_isearch_info.min;
	GtkWidget	*entry = the_isearch_info.wid->entry;
	DirPane		*pane = the_isearch_info.pane;

	/* Don't run if state seems invalid, close might already be in progress. */
	if(pane != NULL)
	{
		g_signal_handler_disconnect(G_OBJECT(entry), the_isearch_info.sig_activate);
		g_signal_handler_disconnect(G_OBJECT(entry), the_isearch_info.sig_key_press);
		g_signal_handler_disconnect(G_OBJECT(entry), the_isearch_info.sig_focus_out);

		the_isearch_info.pane = NULL;	/* Protect againsts against nesting. */
		dp_pathwidgetry_show(pane, 0U);

		kbd_context_attach(min->gui->kbd_ctx, GTK_WINDOW(min->gui->window));
	}
}

/* 2003-11-04 -	Handle keypresses. Traps special keys for handy functionality. */
static gint evt_isearch_keypress(GtkWidget *wid, GdkEventKey *evt, gpointer user)
{
	if(evt->keyval == GDK_KEY_Escape || (evt->keyval == GDK_KEY_g && (evt->state & GDK_CONTROL_MASK) != 0))
	{
		isearch_close();
		return TRUE;
	}
	return FALSE;
}

static gint evt_isearch_activate(GtkWidget *wid, gpointer user)
{
	isearch_close();
	return TRUE;
}

static gint evt_isearch_focus_out(GtkWidget *wid, GdkEventFocus *evt, gpointer user)
{
	isearch_close();
	return FALSE;
}

/* 2010-03-19 -	Callback for the TreeView's search. Gets a column number passed in, but ignores that and always uses name.
**		The return logic here is wack, which is why we're seemingly returning the inverse of what makes sense.
**		We're hardcoded to always check for the display name, regardless of the 'column' GTK+ hands us.
*/
static gboolean cb_search_compare(GtkTreeModel *model, gint column, const gchar *key, GtkTreeIter *iter, gpointer user)
{
	const gchar	*dname = dp_row_get_name_display(model, (DirRow2*) iter);

	if(dname != NULL)
	{
		if(the_isearch_info.offset_use)
		{
			const gsize	len = g_utf8_strlen(dname, -1);
			if(the_isearch_info.offset < len)
				dname = g_utf8_offset_to_pointer(dname, the_isearch_info.offset);
			else
				return TRUE;
			return !(strstr(dname, key) == dname);
		}
		return !(strstr(dname, key) != NULL);
	}
	return TRUE;
}

/* 2003-11-04 -	Incremental search. Initial implementation, with a rather non-satisfying UI. */
gint cmd_dpfocusisrch(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	GtkWidget	*entry;
	const gchar	*text, *start;

	if(the_isearch_info.pane != NULL)
		return FALSE;
	kbd_context_detach(min->gui->kbd_ctx, GTK_WINDOW(min->gui->window));

	the_isearch_info.pane = src;
	the_isearch_info.min  = min;
	the_isearch_info.wid  = (ISearchWidgetry *) dp_pathwidgetry_show(src, the_isearch_info.page);
	entry = the_isearch_info.wid->entry;
	gtk_widget_grab_focus(entry);

	gtk_tree_view_set_search_equal_func(GTK_TREE_VIEW(src->view), cb_search_compare, src, NULL);
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(src->view), 0);
	gtk_tree_view_set_search_entry(GTK_TREE_VIEW(src->view), GTK_ENTRY(entry));

	the_isearch_info.sig_activate  = g_signal_connect(G_OBJECT(entry), "activate", G_CALLBACK(evt_isearch_activate), NULL);
	the_isearch_info.sig_key_press = g_signal_connect(G_OBJECT(entry), "key_press_event", G_CALLBACK(evt_isearch_keypress), NULL);
	the_isearch_info.sig_focus_out = g_signal_connect(G_OBJECT(entry), "focus_out_event", G_CALLBACK(evt_isearch_focus_out), NULL);

	the_isearch_info.offset_use = FALSE;
	the_isearch_info.offset     = 0;
	if((start = car_keyword_get_value(ca, "start", "0")) != NULL)
	{
		gchar	*eptr;
		guint	o;

		o = strtoul(start, &eptr, 0);
		if(eptr > start)
		{
			the_isearch_info.offset_use = TRUE;
			the_isearch_info.offset     = (guint) o;
		}
	}
	the_isearch_info.select = car_keyword_get_boolean(ca, "select", FALSE);

	if((text = car_keyword_get_value(ca, "text", NULL)) != NULL)
		gtk_entry_set_text(GTK_ENTRY(entry), text);

	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 2003-11-08 -	Build dirpane path widgetry for incremental search. */
static GtkWidget ** widgetry_builder(MainInfo *min)
{
	ISearchWidgetry	*wid;

	wid = g_malloc(sizeof *wid);
	wid->hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	wid->label = gtk_label_new(_("ISearch"));
	gtk_box_pack_start(GTK_BOX(wid->hbox), wid->label, FALSE, FALSE, 5);
	wid->entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(wid->hbox), wid->entry, TRUE, TRUE, 0);

	return (GtkWidget **) wid;
}

/* 2003-11-08 -	Configure DpFocusISrch. Does not add user-settable options, just registers
**		a new pathwidgetry builder with the dirpane subsystem. Italian food.
*/
void cfg_dpfocusisrch(MainInfo *min)
{
	the_isearch_info.page = dp_pathwidgetry_add(widgetry_builder);
}
