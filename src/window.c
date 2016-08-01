/*
** 1999-12-23 -	Hm, perhaps this is an unfortunate choice of module title. It might bloat,
**		or something. :) Anyway, the purpose of this module is to help handle
**		(toplevel) windows, specifically remembering their sizes and positions.
*/

#include "gentoo.h"

#include <ctype.h>
#include <stdlib.h>

#include "guiutil.h"
#include "graphics/icon_gentoo_small.xpm"

#include "window.h"

/* ----------------------------------------------------------------------------------------- */

typedef struct
{
	guint32		id;			/* Unique identifier for this windata. */
	WinType		type;			/* What type of window? */
	gboolean	persistent;		/* Makes close() actually do a hide(). */
	const gchar	*title;			/* Text for window's title bar. */
	const gchar	*label;			/* Short descriptive label (like "main", "config" etc). */
	gboolean	pos_grab;
	gboolean	size_grab;
	/* Data below here is stored in configuration file. */
	gint		x, y;			/* Window's opening position. */
	gint		width, height;		/* The desired size of this window. */
	gboolean	pos_use;		/* Should we set use the recorded position? */
	gboolean	pos_update;		/* Update recorded position when window closes? */
	gboolean	size_use;		/* Use recorded size? */
	gboolean	size_update;		/* Update recorded size with current size on close? */
} WinDef;

struct WinInfo
{
	MainInfo	*min;			/* The handiness of this pointer cannot be overstated. */
	GList		*windows;		/* List of WinDef:s as per above. There's no rush. */
	gint		border_width;		/* User's idea of her window manager's border size. Hackish. */
	gint		border_height;
};

/* ----------------------------------------------------------------------------------------- */

static WinDef * window_find(const WinInfo *wi, guint32 id);

/* ----------------------------------------------------------------------------------------- */

WinInfo * win_wininfo_new(MainInfo *min)
{
	WinInfo *wi;

	wi = g_malloc(sizeof *wi);
	wi->min = min;
	wi->windows = NULL;

	return wi;
}

/* 1999-12-23 -	Create a new WinInfo, initialized with the "default" (i.e., legacy) windows. */
WinInfo * win_wininfo_new_default(MainInfo *min)
{
	WinInfo *wi;

	wi = win_wininfo_new(min);

	/* This is what seems to be right on my stock GNOME desktop.
	** Mileage will vary, wildly.
	*/
	win_borders_set(wi, 4, 24);

	win_window_new(wi, WIN_MAIN, WIN_TYPE_SIMPLE_TOPLEVEL, FALSE, geteuid() == 0 ? "gentoo [root]" : "gentoo", "main");
	win_window_pos_grab_set(wi, WIN_MAIN, TRUE);
	win_window_size_set(wi, WIN_MAIN, 764, 932);
	win_window_size_use_set(wi, WIN_MAIN, TRUE);
	win_window_size_update_set(wi, WIN_MAIN, TRUE);
	win_window_size_grab_set(wi, WIN_MAIN, TRUE);

	win_window_new(wi, WIN_CONFIG, WIN_TYPE_COMPLEX_DIALOG, TRUE, _("Configure gentoo"), "config");
	win_window_pos_use_set(wi, WIN_CONFIG, FALSE);
	win_window_size_set(wi, WIN_CONFIG, -1, 464);
	win_window_size_use_set(wi, WIN_CONFIG, TRUE);
	win_window_size_update_set(wi, WIN_CONFIG, TRUE);

	win_window_new(wi, WIN_TEXTVIEW, WIN_TYPE_SIMPLE_TOPLEVEL, FALSE, _("Text Viewer"), "textview");
	win_window_pos_use_set(wi, WIN_TEXTVIEW, FALSE);
	win_window_size_set(wi, WIN_TEXTVIEW, 640, 480);
	win_window_size_use_set(wi, WIN_TEXTVIEW, TRUE);
	win_window_size_update_set(wi, WIN_TEXTVIEW, TRUE);

	win_window_new(wi, WIN_INFO, WIN_TYPE_SIMPLE_DIALOG, FALSE, "Info", "info");
	win_window_size_set(wi, WIN_INFO, 320, 480);
	win_window_size_use_set(wi, WIN_INFO, TRUE);
	win_window_size_update_set(wi, WIN_INFO, TRUE);

	return wi;
}

/* 1999-12-23 -	Create a copy of <wi>, sharing no memory with it. */
WinInfo * win_wininfo_copy(const WinInfo *wi)
{
	WinInfo *nwi;
	WinDef *win;
	GList *iter;

	nwi = win_wininfo_new(wi->min);

	for(iter = wi->windows; iter != NULL; iter = g_list_next(iter))
	{
		win = iter->data;

		win_window_new(nwi, win->id, win->type, win->persistent,
				win->title, win->label);
		win_window_pos_grab_set(nwi, win->id, win->pos_grab);
		win_window_pos_set(nwi, win->id, win->x, win->y);
		win_window_pos_use_set(nwi, win->id, win->pos_use);
		win_window_pos_update_set(nwi, win->id, win->pos_update);
		win_window_size_grab_set(nwi, win->id, win->size_grab);
		win_window_size_set(nwi, win->id, win->width, win->height);
		win_window_size_use_set(nwi, win->id, win->size_use);
		win_window_size_update_set(nwi, win->id, win->size_update);
	}
	nwi->border_width = wi->border_width;
	nwi->border_height = wi->border_height;
	return nwi;
}

void win_wininfo_destroy(WinInfo *wi)
{
	GList *iter;

	for(iter = wi->windows; iter != NULL; iter = g_list_next(iter))
		g_free(iter->data);
	g_list_free(wi->windows);
	g_free(wi);
}

/* ----------------------------------------------------------------------------------------- */

enum
{
	SUBFRAME_POSITION, SUBFRAME_SIZE
};

static void evt_boolean_clicked(GtkWidget *wid, gpointer user)
{
	gboolean *flag = user;
	gboolean *modified;

	*flag = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));
	if((modified = g_object_get_data(G_OBJECT(wid), "modified")) != NULL)
		*modified = TRUE;
}

static void evt_integer_changed(GtkAdjustment *adj, gpointer user)
{
	gint *value = user;
	gboolean *modified;

	*value = gtk_adjustment_get_value(adj);
	if((modified = g_object_get_data(G_OBJECT(adj), "modified")) != NULL)
		*modified = TRUE;
}

/* Grab position. We implicitly *know* that the window in question is the main
** gentoo window, and therefore access it directly through the MainInfo. We
** then put the grabbed position into the editing copy of the WinDef, and update
** the spin buttons to match. Of course, we don't forget setting that pesky old
** modified-flag, either. :) What would people think?
*/
static void evt_pos_grab_clicked(GtkWidget *wid, gpointer user)
{
	MainInfo *min;
	WinDef *wd = user;

	min = g_object_get_data(G_OBJECT(wid), "min");
	gdk_window_get_position(gtk_widget_get_window(min->gui->window), &wd->x, &wd->y);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_object_get_data(G_OBJECT(wid), "spin1")), wd->x);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_object_get_data(G_OBJECT(wid), "spin2")), wd->y);
	*(gboolean *) g_object_get_data(G_OBJECT(wid), "modified") = TRUE;
}

/* Grab size. Works only for main gentoo window. See comment for pos_grab above. */
static void evt_size_grab_clicked(GtkWidget *wid, gpointer user)
{
	MainInfo *min;
	WinDef *wd = user;

	min = g_object_get_data(G_OBJECT(wid), "min");
	wd->width  = gdk_window_get_width(gtk_widget_get_window(min->gui->window));
	wd->height = gdk_window_get_height(gtk_widget_get_window(min->gui->window));
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_object_get_data(G_OBJECT(wid), "spin1")), wd->width);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_object_get_data(G_OBJECT(wid), "spin2")), wd->height);
	*(gboolean *) g_object_get_data(G_OBJECT(wid), "modified") = TRUE;
}

static GtkWidget * subframe_build(const WinInfo *wi, WinDef *win, gint type, gboolean *modified)
{
	const gchar *ltext[] =
	{ N_("Position"), N_("Size") }, *etext[] =
	{ N_("X"), N_("Y"), N_("Width"), N_("Height") };
	GtkWidget	*label, *frame, *cbtn, *vbox, *grid, *spin1, *spin2;
	GtkAdjustment	*adj;
	gint		val1 = 0, val2 = 0;

	frame = gtk_frame_new(_(ltext[type]));
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	cbtn = gtk_check_button_new_with_label(_("Set on Open?"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cbtn), (type == SUBFRAME_POSITION) ? win_window_pos_use_get(wi, win->id) : win_window_size_use_get(wi, win->id));
	g_object_set_data(G_OBJECT(cbtn), "modified", modified);
	g_signal_connect(G_OBJECT(cbtn), "clicked", G_CALLBACK(evt_boolean_clicked), (type == SUBFRAME_POSITION) ? &win->pos_use : &win->size_use);
	gtk_box_pack_start(GTK_BOX(vbox), cbtn, FALSE, FALSE, 0);

	cbtn = gtk_check_button_new_with_label(_("Update on Close?"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cbtn), (type == SUBFRAME_POSITION) ? win_window_pos_update_get(wi, win->id) : win_window_size_update_get(wi, win->id));
	g_object_set_data(G_OBJECT(cbtn), "modified", modified);
	g_signal_connect(G_OBJECT(cbtn), "clicked", G_CALLBACK(evt_boolean_clicked), (type == SUBFRAME_POSITION) ? &win->pos_update : &win->size_update);
	gtk_box_pack_start(GTK_BOX(vbox), cbtn, FALSE, FALSE, 0);

	grid = gtk_grid_new();

	if(type == SUBFRAME_POSITION)
		win_window_pos_get(wi, win->id, &val1, &val2);
	else
		win_window_size_get(wi, win->id, &val1, &val2);

	label = gtk_label_new(_(etext[2 * type]));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
	adj = gtk_adjustment_new(val1, -1.0, 65535.0, 1.0, 25.0, 0.0);
	spin1 = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 0.0, 0.0);
	g_object_set_data(G_OBJECT(adj), "modified", modified);
	g_signal_connect(G_OBJECT(adj), "value_changed", G_CALLBACK(evt_integer_changed), type == SUBFRAME_POSITION ? &win->x : &win->width);
	gtk_widget_set_hexpand(spin1, TRUE);
	gtk_widget_set_halign(spin1, GTK_ALIGN_FILL);
	gtk_grid_attach(GTK_GRID(grid), spin1, 1, 0, 1, 1);
	label = gtk_label_new(_(etext[2 * type + 1]));
	gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
	adj = gtk_adjustment_new(val2, -1.0, 65535.0, 1.0, 25.0, 0.0);
	spin2 = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 0.0, 0.0);
	g_object_set_data(G_OBJECT(adj), "modified", modified);
	g_signal_connect(G_OBJECT(adj), "value_changed", G_CALLBACK(evt_integer_changed), type == SUBFRAME_POSITION ? &win->y : &win->height);
	gtk_grid_attach(GTK_GRID(grid), spin2, 1, 1, 1, 1);
	if((type == SUBFRAME_POSITION && win->pos_grab) || (type == SUBFRAME_SIZE && win->size_grab))
	{
		GtkWidget *grab;

		grab = gtk_button_new_with_label(_("Grab"));

		g_object_set_data(G_OBJECT(grab), "min", wi->min);
		g_object_set_data(G_OBJECT(grab), "spin1", spin1);
		g_object_set_data(G_OBJECT(grab), "spin2", spin2);
		g_object_set_data(G_OBJECT(grab), "modified", modified);
		g_signal_connect(G_OBJECT(grab), "clicked", type == SUBFRAME_POSITION ? G_CALLBACK(evt_pos_grab_clicked) : G_CALLBACK(evt_size_grab_clicked), win);
		gtk_grid_attach(GTK_GRID(grid), grab, 2, 0, 1, 2);
	}
	gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(frame), vbox);

	return frame;
}

GtkWidget * win_wininfo_build(const WinInfo *wi, gboolean *modified)
{
	GtkWidget *vbox, *frame, *hbox, *sframe;
	GList *iter;
	gchar tmp[64];
	WinDef *win;

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	for(iter = wi->windows; iter != NULL; iter = g_list_next(iter))
	{
		win = iter->data;

		g_snprintf(tmp, sizeof tmp, "%c%s", toupper(*win->label), win->label + 1);
		frame = gtk_frame_new(tmp);
		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		sframe = subframe_build(wi, win, SUBFRAME_POSITION, modified);
		gtk_box_pack_start(GTK_BOX(hbox), sframe, TRUE, TRUE, 5);
		sframe = subframe_build(wi, win, SUBFRAME_SIZE, modified);
		gtk_box_pack_start(GTK_BOX(hbox), sframe, TRUE, TRUE, 5);
		gtk_container_add(GTK_CONTAINER(frame), hbox);
		gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 5);
	}
	return vbox;
}

/* ----------------------------------------------------------------------------------------- */

/* 2000-02-24 -	Write WinInfo out to given file <out>, at its current location. */
void win_wininfo_save(const WinInfo *wi, FILE *out)
{
	GList *iter;
	WinDef *wd;

	xml_put_node_open(out, "Windows");
	for(iter = wi->windows; iter != NULL; iter = g_list_next(iter))
	{
		wd = iter->data;

		xml_put_node_open(out, "Window");
		xml_put_uinteger(out, "id", wd->id);
		xml_put_integer(out, "x", wd->x);
		xml_put_integer(out, "y", wd->y);
		xml_put_integer(out, "w", wd->width);
		xml_put_integer(out, "h", wd->height);
		xml_put_boolean(out, "pos_use", wd->pos_use);
		xml_put_boolean(out, "pos_update", wd->pos_update);
		xml_put_boolean(out, "size_use", wd->size_use);
		xml_put_boolean(out, "size_update", wd->size_update);
		xml_put_node_close(out, "Window");
	}
	/* This goes INSIDE <Windows>, since there is no outer structure. :| */
	xml_put_node_open(out, "Borders");
	xml_put_integer(out, "width", wi->border_width);
	xml_put_integer(out, "height", wi->border_height);
	xml_put_node_close(out, "Borders");
	xml_put_node_close(out, "Windows");
}

static void window_load(const XmlNode *node, gpointer user)
{
	WinInfo	*wi = user;

	if(xml_node_has_name(node, "Window"))
	{
		guint32 id;

		if(xml_get_uinteger(node, "id", &id))
		{
			WinDef *wd;

			if((wd = window_find(wi, id)) != NULL)
			{
				xml_get_integer(node, "x", &wd->x);
				xml_get_integer(node, "y", &wd->y);
				xml_get_integer(node, "w", &wd->width);
				xml_get_integer(node, "h", &wd->height);
				xml_get_boolean(node, "pos_use", &wd->pos_use);
				xml_get_boolean(node, "pos_update", &wd->pos_update);
				xml_get_boolean(node, "size_use", &wd->size_use);
				xml_get_boolean(node, "size_update", &wd->size_update);
			}
		}
	}
	else if(xml_node_has_name(node, "Borders"))
	{
		xml_get_integer(node, "width", &wi->border_width);
		xml_get_integer(node, "height", &wi->border_height);
	}
}

/* 2000-02-24 -	Fill in <wi> with details for windows, loaded from <node>. */
void win_wininfo_load(WinInfo *wi, const XmlNode *node)
{
	/* Visit all children. Since the XML structure, for the usual raisins, is a bit
	** messed up, the Window elements are at the same level as the Borders.
	*/
	xml_node_visit_children(node, window_load, wi);
}

/* ----------------------------------------------------------------------------------------- */

void win_borders_set(WinInfo *wi, gint width, gint height)
{
	if(wi == NULL)
		return;
	wi->border_width = width;
	wi->border_height = height;
}

void win_borders_get(const WinInfo *wi, gint *width, gint *height)
{
	if(wi == NULL)
		return;
	if(width != NULL)
		*width = wi->border_width;
	if(height != NULL)
		*height = wi->border_height;
}

/* ----------------------------------------------------------------------------------------- */

void win_window_new(WinInfo *wi, guint32 id, WinType type, gboolean persistent, const gchar *title, const gchar *label)
{
	WinDef *wd;

	wd = g_malloc(sizeof *wd);
	wd->id = id;
	wd->type = type;
	wd->persistent = persistent;
	wd->title = title;
	wd->label = label;
	wd->pos_grab = FALSE;
	wd->size_grab = FALSE;

	wd->x = 32;
	wd->y = 32;
	wd->width = 32;
	wd->height = 32;

	wd->pos_use = FALSE;
	wd->pos_update = FALSE;
	wd->size_use = FALSE;
	wd->size_update = TRUE;

	wi->windows = g_list_append(wi->windows, wd);
}

static WinDef * window_find(const WinInfo *wi, guint32 id)
{
	GList *iter;

	if(wi == NULL)
		return NULL;

	for(iter = wi->windows; iter != NULL; iter = g_list_next(iter))
	{
		if(((WinDef *) iter->data)->id == id)
			return iter->data;
	}
	return NULL;
}

void win_window_pos_grab_set(const WinInfo *wi, guint32 id, gboolean grab)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
		wd->pos_grab = grab;
}

void win_window_pos_set(const WinInfo *wi, guint32 id, gint x, gint y)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
	{
		wd->x = x;
		wd->y = y;
	}
}

gboolean win_window_pos_get(const WinInfo *wi, guint32 id, gint *x, gint *y)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
	{
		if(x)
			*x = wd->x;
		if(y)
			*y = wd->y;
		return TRUE;
	}
	return FALSE;
}

void win_window_pos_use_set(const WinInfo *wi, guint32 id, gboolean enabled)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
		wd->pos_use = enabled;
}

gboolean win_window_pos_use_get(const WinInfo *wi, guint32 id)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
		return wd->pos_use;
	return FALSE;
}

void win_window_pos_update_set(const WinInfo *wi, guint32 id, gboolean enabled)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
		wd->pos_update = enabled;
}

gboolean win_window_pos_update_get(const WinInfo *wi, guint32 id)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
		return wd->pos_update;
	return FALSE;
}

void win_window_size_grab_set(const WinInfo *wi, guint32 id, gboolean grab)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
		wd->size_grab = grab;
}

void win_window_size_set(const WinInfo *wi, guint32 id, gint width, gint height)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
	{
		wd->width = width;
		wd->height = height;
	}
}

gboolean win_window_size_get(const WinInfo *wi, guint32 id, gint *width,
		gint *height)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
	{
		if(width)
			*width = wd->width;
		if(height)
			*height = wd->height;
		return TRUE;
	}
	return FALSE;
}

void win_window_size_use_set(const WinInfo *wi, guint32 id, gboolean enabled)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
		wd->size_use = enabled;
}

gboolean win_window_size_use_get(const WinInfo *wi, guint32 id)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
		return wd->size_use;
	return FALSE;
}

void win_window_size_update_set(const WinInfo *wi, guint32 id, gboolean enabled)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
		wd->size_update = enabled;
}

gboolean win_window_size_update_get(const WinInfo *wi, guint32 id)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
		return wd->size_update;
	return FALSE;
}

/* ----------------------------------------------------------------------------------------- */

void win_window_destroy(WinInfo *wi, guint32 id)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
	{
		wi->windows = g_list_remove(wi->windows, wd);
		g_free(wd);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 2010-12-12 -	Rewritten to not use deprecated (and crazy-seeming) GDK calls. */
static void window_set_icon(GtkWindow *win)
{
	GdkPixbuf	*pbuf;

	if((pbuf = gdk_pixbuf_new_from_xpm_data(icon_gentoo_small_xpm)) != NULL)
	{
		gtk_window_set_default_icon(pbuf);
		gtk_window_set_icon_name(win, PACKAGE);
		g_object_unref(G_OBJECT(pbuf));	/* I believe GTK+ owns this, now. */
	}
}

/* 1999-12-23 -	Open the window described by <key> in <wi>. The window will not be shown yet. */
GtkWidget * win_window_open(const WinInfo *wi, guint32 id)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
	{
		GtkWidget *wid = NULL;

		switch (wd->type)
		{
		case WIN_TYPE_SIMPLE_TOPLEVEL:
		case WIN_TYPE_SIMPLE_DIALOG:
			wid = gtk_window_new(GTK_WINDOW_TOPLEVEL);
			break;
		case WIN_TYPE_SIMPLE_POPUP:
			wid = gtk_window_new(GTK_WINDOW_POPUP);
			break;
		case WIN_TYPE_COMPLEX_DIALOG:
			wid = win_dialog_open(wi->min->gui->window);
			break;
		}
		g_object_set_data(G_OBJECT(wid), "wininfo", (gpointer) wi);
		g_object_set_data(G_OBJECT(wid), "windef", (gpointer) wd);
		if(!wd->pos_use)
			gtk_window_set_position(GTK_WINDOW(wid), GTK_WIN_POS_MOUSE);
		gtk_widget_realize(wid);
		if(id == WIN_MAIN)
			window_set_icon(GTK_WINDOW(wid));
		else if(id != WIN_MAIN && id != WIN_TYPE_COMPLEX_DIALOG && wi->min->gui != NULL)
			gtk_window_set_transient_for(GTK_WINDOW(wid), GTK_WINDOW(wi->min->gui->window));
		if(wd->title != NULL)
			win_window_set_title(wid, wd->title);
		if(wd->label != NULL)
			gtk_window_set_role(GTK_WINDOW(wid), wd->label);
		return wid;
	}
	return NULL;
}

/* 2000-02-19 -	Refresh link between <wi> and <window>. Handy after <wi> has been replaced
**		while window was open, as happens in the config window case.
*/
void win_window_relink(const WinInfo *wi, guint32 id, GtkWidget *window)
{
	WinDef *wd;

	if((wd = window_find(wi, id)) != NULL)
	{
		g_object_set_data(G_OBJECT(window), "wininfo", (gpointer) wi);
		g_object_set_data(G_OBJECT(window), "windef", wd);
	}
}

/* 2002-08-10 -	Set title of a window. Since it seems to break if locale is set, we simply
**		un-set the locale temporarily. Hopefully, noone sets window titles in inner
**		loops around here.
** NOTE NOTE	This works for *all* windows, not just those created through this module.
**		That might surprise, but it felt more sensical to stick it in here anyway.
*/
void win_window_set_title(GtkWidget *win, const gchar *title)
{
	gtk_widget_realize(win);
	gtk_window_set_title(GTK_WINDOW(win), title);
}

/* 1999-12-23 -	Use this rather than gtk_widget_show() when you want to the window to appear, since
**		this gives us a chance to set the window's size and position according to preference.
*/
void win_window_show(GtkWidget *window)
{
	WinInfo	*wi;
	WinDef	*wd;

	if(gtk_widget_get_visible(window))
		return;

	wi = g_object_get_data(G_OBJECT(window), "wininfo");
	wd = g_object_get_data(G_OBJECT(window), "windef");
	if((wd != NULL) && wd->size_use && wd->width >= 0)
		gtk_window_set_default_size(GTK_WINDOW(window), wd->width, wd->height);
	/* For the main window, make sure to make it possible to shrink very much. */
	if(wd != NULL && wd->id == WIN_MAIN)
	{
		GdkGeometry	geo;

		geo.min_width = 10;
		geo.min_height = 10;
		gtk_window_set_geometry_hints(GTK_WINDOW(window), window, &geo, GDK_HINT_MIN_SIZE);
	}

	/* This causes the window to appear at some more or less random position. We then reposition
	** it to the configured place. This is visually distracting, unattractive, and almost s*cks,
	** but unfortunately I haven't been able to figure out how to fix it. If you move the show()
	** call to below the if, it works, but not all the time. :( Tips welcome.
	*/
	gtk_widget_show_all(window);

	/* If user wants to set position, check where it ended up and move it home. */
	if((wd != NULL) && wd->pos_use && wd->x >= 0)
	{
		gint	cx, cy;

		gtk_window_get_position(GTK_WINDOW(window), &cx, &cy);
		if(wi != NULL)	/* Any borders to adjust for? */
		{
			cx += wi->border_width;
			cy += wi->border_height;
		}
		gtk_window_move(GTK_WINDOW(window), wd->x - cx, wd->y - cy);
		gui_events_flush();
		gtk_window_get_position(GTK_WINDOW(window), &cx, &cy);
	}
}

/* 2000-03-04 -	If <a_new> is different from *<a>, update *<a> to <a_new> and return TRUE, else
**		return FALSE. Likewise for <b>, whatever that would mean.
*/
static gboolean update_pair(gint *a, gint *b, gint a_new, gint b_new)
{
	if((*a != a_new) || (*b != b_new))
	{
		*a = a_new;
		*b = b_new;
		return TRUE;
	}
	return FALSE;
}

/* 2000-03-18 -	Do an update for the given window, changing the config as needed. Returns TRUE
**		if the config was indeed changed.
*/
gboolean win_window_update(GtkWidget *win)
{
	WinDef *wd;

	if((wd = g_object_get_data(G_OBJECT(win), "windef")) != NULL)
	{
		gboolean ret = FALSE;
		gint ta, tb;

		if(wd->pos_update)
		{
			gdk_window_get_root_origin(gtk_widget_get_window(win), &ta, &tb);
			ret |= update_pair(&wd->x, &wd->y, ta, tb);
		}
		if(wd->size_update)
		{
			ta = gdk_window_get_width(gtk_widget_get_window(win));
			tb = gdk_window_get_height(gtk_widget_get_window(win));
			ret |= update_pair(&wd->width, &wd->height, ta, tb);
		}
		return ret;
	}
	return FALSE;
}

/* 1999-12-23 -	Close the given window. Use this rather than a direct gtk_widget_destroy(), since
**		we need a chance to update the position and size fields if needed. Also, if the
**		window is classified as "persistent", we don't actually destroy it, but only hide
**		it. Returns TRUE if the config was changed, i.e. if one of the update flags were
**		set AND the relevant property actually had changed.
*/
gboolean win_window_close(GtkWidget *win)
{
	WinDef *wd;

	if((wd = g_object_get_data(G_OBJECT(win), "windef")) != NULL)
	{
		gboolean ret;

		ret = win_window_update(win);
		if(wd->persistent)
			gtk_widget_hide(win);
		else
			gtk_widget_destroy(win);
		return ret;
	}
	gtk_widget_destroy(win);
	return FALSE;
}

/* ----------------------------------------------------------------------------------------- */

/* 2010-12-12 -	New single way of opening a dialog, that tries to make sure it gets the proper
**		linkage to the main window. This shares the icon, among (possibly) other things.
*/
GtkWidget * win_dialog_open(GtkWidget *main_window)
{
	GtkWidget	*wid = gtk_dialog_new();

	if(main_window != NULL)
		gtk_window_set_transient_for(GTK_WINDOW(wid), GTK_WINDOW(main_window));

	return wid;
}
