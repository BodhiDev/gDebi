/*
** Custom button widget, that supports having a secondary click-function associated with being
** clicked by the middle mouse button. Very much inspired by Directory Opus on the Amiga.
**
** Original GTK+ 1.2.x version by Johan Hanson, re-written for GTK+ 2.0 and 3.0 by Emil Brink.
*/

#include <stdio.h>
#include <stdlib.h>

#include "config.h"

#include <gtk/gtk.h>

#include "odmultibutton.h"

static void	od_multibutton_class_init(ODMultiButtonClass *mbc);
static void	od_multibutton_init(ODMultiButton *mb);

/* This seems to be customary in GTK+-land. I find it a bit... weird. */
static GtkButtonClass	*button_class = NULL;

void od_multibutton_set_trace(ODMultiButton *widget, unsigned int trace_mask)
{
	g_return_if_fail(widget != NULL);
	g_return_if_fail(OD_IS_MULTIBUTTON(widget));

	widget->trace_mask = trace_mask;
}

GType od_multibutton_get_type(void)
{
	static GType	od_multibutton_type = 0;

	if(od_multibutton_type == 0)
	{
		static const GTypeInfo mb_info =
		{
			sizeof (ODMultiButtonClass),
			NULL,
			NULL,
			(GClassInitFunc) od_multibutton_class_init,
			NULL,
			NULL,
			sizeof (ODMultiButton),
			8,
			(GInstanceInitFunc) od_multibutton_init,
		};
		od_multibutton_type = g_type_register_static(GTK_TYPE_BUTTON, "ODMultiButton", &mb_info, 0);
	}
	return od_multibutton_type;
}

/* Set which "page" should be displayed. Typically called as user clicks on the button widget. */
static void od_multibutton_set_page(GtkWidget *widget, guint index)
{
	ODMultiButton	*mb;
	GtkWidget	*p, *op;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(index < sizeof mb->page / sizeof *mb->page);
	g_return_if_fail(OD_IS_MULTIBUTTON(widget));

	mb = OD_MULTIBUTTON(widget);

	p  = mb->page[index].widget;
	op = gtk_bin_get_child(GTK_BIN(mb));

	if(op != p)
	{
		if(op != NULL)
		{
			g_object_ref(G_OBJECT(op));
			gtk_container_remove(GTK_CONTAINER(mb), op);
		}
		if(p != NULL)
		{
			GtkWidget	*pparent = gtk_widget_get_parent(p);

			if(gtk_widget_get_state_flags(widget) != gtk_widget_get_state_flags(p))
				gtk_widget_set_state_flags(p, gtk_widget_get_state_flags(widget), TRUE);
			gtk_widget_show(p);
			if(pparent != NULL)
			{
				/* Manual reparenting. Semi-legacy code, not sure when this is needed at all. :/ */
				g_object_ref(p);
				gtk_container_remove(GTK_CONTAINER(pparent), p);
				gtk_container_add(GTK_CONTAINER(widget), p);
				g_object_unref(p);
			}
			else
			{
				gtk_container_add(GTK_CONTAINER(mb), p);
				g_object_unref(G_OBJECT(p));
			}
		}
		mb->last_index = index;
	}
	if(gtk_widget_is_drawable(widget))
		gtk_widget_queue_draw(widget);
}

/* Compute size of the page widgets, and set width & height members to the maximum page size. */
static void od_multibutton_page_size_calc(ODMultiButton *mb)
{
	guint	i;

	mb->width = mb->height = 0;
	for(i = 0; i < sizeof mb->page / sizeof *mb->page; i++)
	{
		GtkWidget	*w;

		if((w = mb->page[i].widget) != NULL)
		{
			GtkRequisition	req_min, req_max;

			gtk_widget_get_preferred_size(w, &req_min, &req_max);
			mb->width  = MAX(mb->width, req_min.width);
			mb->height = MAX(mb->height, req_min.height);
		}
	}
}

static void od_multibutton_get_preferred_width(GtkWidget *widget, gint *min_width, gint *max_width)
{
	ODMultiButton	*mb;
	gint		width;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(OD_IS_MULTIBUTTON(widget));

	mb = OD_MULTIBUTTON(widget);
	od_multibutton_page_size_calc(mb);
	width = mb->width;
	
	width += 2 * gtk_container_get_border_width(GTK_CONTAINER(widget));
	/* FIXME: Not sure if we need to dig up CSS box model spacing here (margin/padding). */

	if(min_width != NULL)
		*min_width = width;
	if(max_width != NULL)
		*max_width = width;
}

/* Paint the "dog ear" that indicates second-mouse-button binding. This code is pretty much lifted
** from Johan Hanson's original ODEmilButton widget, although not a straight copy and paste.
*/
static void od_multibutton_paint_dog_ear(GtkWidget *widget, cairo_t *cr, const GtkAllocation *alloc, gint bw)
{
	const GtkStateFlags	sflags = gtk_widget_get_state_flags(widget);

	if(gtk_widget_is_drawable(widget) && sflags != GTK_STATE_FLAG_ACTIVE)
	{
		const gint	EAR_SIZE = 5;

		cairo_move_to(cr, alloc->width - 2 * bw - (EAR_SIZE + 1), 0);
		cairo_rel_line_to(cr, 0, EAR_SIZE);
		cairo_rel_line_to(cr, EAR_SIZE, 0);
		cairo_close_path(cr);
		cairo_set_line_width(cr, 0.5);
		cairo_stroke(cr);
	}
}

static void od_multibutton_paint_foreground(GtkWidget *widget, cairo_t *cr)
{
	g_return_if_fail(widget != NULL);
	g_return_if_fail(cr != NULL);
	g_return_if_fail(OD_IS_MULTIBUTTON(widget));

	if(gtk_widget_is_drawable(widget))
	{
		const ODMultiButton	*mb = OD_MULTIBUTTON(widget);
		GtkAllocation		alloc;
		const gint		bw = gtk_container_get_border_width(GTK_CONTAINER(widget));

		gtk_widget_get_allocation(widget, &alloc);
		if(mb->page[1].widget != NULL)
			od_multibutton_paint_dog_ear(widget, cr, &alloc, bw);
		if(mb->config && mb->config_down)
		{
			GtkStyleContext	*sctx = gtk_widget_get_style_context(widget);

			/* Trivial border-rendering, instead of faked sunken-in. */
			gtk_render_focus(sctx, cr, 0, 0, alloc.width - 2 * bw, alloc.height - 2 * bw);
		}
	}
}


/* This is the core expose handler. It simply relies on the superclass (GtkButton) to do most
** of the drawing, taking care to fool it into doing what we want by poking a little. We also
** have our own foreground painting routine, for that oh-so-cute dog ear.
*/
static gboolean od_multibutton_draw(GtkWidget *widget, cairo_t *cr)
{
	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(cr != NULL, FALSE);
	g_return_val_if_fail(OD_IS_MULTIBUTTON(widget), FALSE);

	/* First let GtkButton draw, getting the basic button painted. */
	GTK_WIDGET_CLASS(button_class)->draw(widget, cr);
	/* Then paint our own modifying graphics on top, to get the dog ear and config border. */
	od_multibutton_paint_foreground(widget, cr);

	return FALSE;
}

/* (Mouse) button press handler. Switch to page 2 if it's the middle button. */
static gboolean od_multibutton_button_press_event(GtkWidget *widget, GdkEventButton *evt)
{
	ODMultiButton	*mb;

	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(evt != NULL, FALSE);
	g_return_val_if_fail(OD_IS_MULTIBUTTON(widget), FALSE);

	if(evt->button >= 3)
		return FALSE;

	mb = OD_MULTIBUTTON(widget);

	/* Ignore presses of middle button in config mode. Otherwise, don't be picky about page being defined. */
	if(mb->config)
	{
		if(evt->button > 1)
			return FALSE;
	}
	else if(mb->page[evt->button - 1].widget != NULL)
	{
		od_multibutton_set_page(widget, evt->button - 1);
		/* GtkButton only handles button-1-presses, so fool it. :) */
		evt->button = 1;
	}
	else
		return FALSE;
	return GTK_WIDGET_CLASS(button_class)->button_press_event(widget, evt);
}

/* (Mouse) button release handler. Doesn't do much. */
static gboolean od_multibutton_button_release_event(GtkWidget *widget, GdkEventButton *evt)
{
	g_return_val_if_fail(widget != NULL, FALSE);
	g_return_val_if_fail(evt != NULL, FALSE);
	g_return_val_if_fail(OD_IS_MULTIBUTTON(widget), FALSE);

	if(evt->button > 2)
		return FALSE;

	/* In config mode, toggle lock on release. */
	if(OD_MULTIBUTTON(widget)->config)
	{
		if(evt->button > 1)
			return FALSE;
		OD_MULTIBUTTON(widget)->config_down ^= 1/*GTK_BUTTON(widget)->in_button*/;	/*FIXME?*/
	}

	/* GtkButton wants to believe button 1 was pressed, so let's make it so. */
	evt->button = 1;
	GTK_WIDGET_CLASS(button_class)->button_release_event(widget, evt);
	/* Reset to initial page. */
	od_multibutton_set_page(widget, 0);

	return FALSE;
}

/* Initialize class. */
static void od_multibutton_class_init(ODMultiButtonClass *mbc)
{
	GtkWidgetClass	*widget = (GtkWidgetClass *) mbc;

	/* It seems common practice to keep a superclass reference around as a global. */
	button_class = g_type_class_peek_parent(mbc);

	/* Override methods. */
	widget->draw			= od_multibutton_draw;
	widget->get_preferred_width	= od_multibutton_get_preferred_width;

	widget->button_press_event	= od_multibutton_button_press_event;
	widget->button_release_event	= od_multibutton_button_release_event;
}

/* Initialize a brand new multibutton instance. */
static void od_multibutton_init(ODMultiButton *mb)
{
	guint	i;

	gtk_widget_set_can_focus(GTK_WIDGET(mb), FALSE);
	gtk_widget_set_can_default(GTK_WIDGET(mb), FALSE);
	gtk_widget_set_receives_default(GTK_WIDGET(mb), FALSE);

	for(i = 0; i < sizeof mb->page / sizeof *mb->page; i++)
	{
		mb->page[i].widget = NULL;
		mb->page[i].user   = NULL;
	}
	mb->width	= mb->height = 0;
	mb->config	= FALSE;
	mb->config_down = FALSE;
	mb->last_index	= 0;
}

/* I like GTK+ 2 constructors. They're short and sweet, and just pass the buck. */
GtkWidget * od_multibutton_new(void)
{
	return g_object_new(OD_MULTIBUTTON_TYPE, NULL);
}

/* Enable or disable the special togglebutton-like "configuration mode". Used in gentoo's config UI. */
void od_multibutton_set_config(ODMultiButton *mb, gboolean config)
{
	g_return_if_fail(mb != NULL);
	g_return_if_fail(OD_IS_MULTIBUTTON(mb));

	if(mb->config && !config)
		od_multibutton_set_config_selected(mb, FALSE);
	mb->config = config;
}

/* Set the config toggle state. In practice (in gentoo), this is only ever used to deselect a button. */
void od_multibutton_set_config_selected(ODMultiButton *mb, gboolean selected)
{
	g_return_if_fail(mb != NULL);
	g_return_if_fail(OD_IS_MULTIBUTTON(mb));
	g_return_if_fail(OD_MULTIBUTTON(mb)->config);

	if(mb->config && selected != mb->config_down)
	{
		mb->config_down = selected;
		gtk_widget_queue_draw(GTK_WIDGET(mb));
	}
}

static void od_multibutton_remove_widget(ODMultiButton *mb, guint index)
{
	GtkWidget	*widget;

	g_return_if_fail(mb != NULL);
	g_return_if_fail(OD_IS_MULTIBUTTON(mb));
	g_return_if_fail(index > sizeof mb->page / sizeof *mb->page);

	widget = mb->page[index].widget;
	if(gtk_bin_get_child(GTK_BIN(mb)) == widget)
		gtk_container_remove(GTK_CONTAINER(mb), widget);
	else
		g_object_unref(G_OBJECT(widget));
	gtk_widget_queue_draw(GTK_WIDGET(mb));
}

static void od_multibutton_set_widget(ODMultiButton *mb, guint index, GtkWidget *widget, gpointer user)
{
	g_return_if_fail(mb != NULL);
	g_return_if_fail(OD_IS_MULTIBUTTON(mb));
	g_return_if_fail(index < sizeof mb->page / sizeof *mb->page);

	if(mb->page[index].widget != widget)
	{
		if(mb->page[index].widget != NULL)
			od_multibutton_remove_widget(mb, index);

		mb->page[index].widget = widget;
		g_object_ref(G_OBJECT(widget));
	}
	mb->page[index].user = user;

	if(index == 0)
		od_multibutton_set_page(GTK_WIDGET(mb), 0);
}

/* Build a color attribute, suitable for a Pango markup <span> element, of the given name. */
static gboolean format_color_attribute(gchar *buf, gsize bufsize, const gchar *name, const GdkColor *color)
{
	if(buf == NULL || name == NULL || color == NULL)
		return FALSE;
	return g_snprintf(buf, bufsize, "%s=\"#%02X%02X%02X\"",
		name, color->red >> 8, color->green >> 8, color->blue >> 8) < bufsize;
}

/* Re-set the label text for the given button's indicated face. This rebuilds the formatting
** markup in the label to accomodate colors, if set.
*/
static void od_multibutton_reset_label(const ODMultiButton *mb, guint index, GtkLabel *label, const gchar *text, const GdkColor *bg, const GdkColor *fg)
{
	if(bg != NULL || fg != NULL)
	{
		gchar	tmp[1024], bga[32], fga[32];

		/* Warm up by building attributes. */
		bga[0] = fga[0] = '\0';
		if(bg != NULL)
			format_color_attribute(bga, sizeof bga, " background", bg);
		if(fg != NULL)
			format_color_attribute(fga, sizeof fga, " color", fg);
		/* Then build final label, ignoring any incoming span. Non-used colors are empty. */
		g_snprintf(tmp, sizeof tmp, "<span%s%s>%s</span>", fga, bga, text);
		gtk_label_set_markup_with_mnemonic(label, tmp);
	}
	else
		gtk_label_set_text_with_mnemonic(label, text);
}

/* Set the textual content of one of the faces of the button. The userdata is handy when clicked. */
void od_multibutton_set_text(ODMultiButton *mb, guint index, const gchar *text, const GdkColor *bg, const GdkColor *fg, gpointer user)
{
	GtkWidget	*w;

	g_return_if_fail(mb != NULL);
	g_return_if_fail(OD_IS_MULTIBUTTON(mb));
	g_return_if_fail(index < sizeof mb->page / sizeof *mb->page);

	/* If there is a widget set, and it's a label: just change it in place. */
	if((w = mb->page[index].widget) != NULL)
	{
		if(GTK_IS_LABEL(w))
		{
			od_multibutton_reset_label(mb, index, GTK_LABEL(w), text, bg, fg);
			if(gtk_widget_get_parent(GTK_WIDGET(mb)))
				gtk_widget_queue_resize(GTK_WIDGET(mb));
			if(gtk_widget_is_drawable(GTK_WIDGET(mb)))
				gtk_widget_queue_draw(GTK_WIDGET(mb));
		}
	}
	else	/* If no widget set, create label. */
	{
		w = gtk_label_new("");
		od_multibutton_reset_label(mb, index, GTK_LABEL(w), text, bg, fg);
	}
	if(GTK_IS_LABEL(w))
	{
		gtk_label_set_xalign(GTK_LABEL(w), 0.5f);
		gtk_label_set_yalign(GTK_LABEL(w), 0.5f);
	}
	od_multibutton_set_widget(mb, index, w, user);
}

/* Returns index of last active page. */
gint od_multibutton_get_index(const ODMultiButton *mb)
{
	g_return_val_if_fail(mb != NULL, -1);
	g_return_val_if_fail(OD_IS_MULTIBUTTON(mb), -1);

	return OD_MULTIBUTTON(mb)->last_index;
}

gpointer od_multibutton_get_userdata_last(const ODMultiButton *mb)
{
	g_return_val_if_fail(mb != NULL, NULL);
	g_return_val_if_fail(OD_IS_MULTIBUTTON(mb), NULL);

	return OD_MULTIBUTTON(mb)->page[OD_MULTIBUTTON(mb)->last_index].user;
}

/* ------------------------------------------------------------------------------------------------------------------------- */

#if defined ODMULTIBUTTON_STANDALONE

static void evt_clicked(GtkWidget *wid, gpointer user)
{
	g_printf("clicked, index=%d, data=%p\n", od_multibutton_get_index(OD_MULTIBUTTON(wid)),
		 od_multibutton_get_userdata_last(OD_MULTIBUTTON(wid)));
}

static void mb_random_color(GtkWidget *wid, unsigned int face)
{
	GdkColor	col;

	col.red   = rand();
	col.green = rand();
	col.blue  = rand();
	od_multibutton_set_background(OD_MULTIBUTTON(wid), face, &col);
}

static void evt_color_clicked(GtkWidget *wid, gpointer user)
{
	mb_random_color(user, 0);
	mb_random_color(user, 1);
}

static void evt_clear_clicked(GtkWidget *wid, gpointer user)
{
	od_multibutton_set_config_selected(OD_MULTIBUTTON(user), FALSE);
}

static void evt_exit_config_clicked(GtkWidget *wid, gpointer user)
{
	od_multibutton_set_config(OD_MULTIBUTTON(user), FALSE);
}

int main(int argc, char *argv[])
{
	GtkWidget	*win, *box, *test, *btn;
	GdkColormap	*cmap;
	guint		i;

	gtk_init(&argc, &argv);

	win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	box = gtk_hbox_new(FALSE, 0);

	btn = gtk_button_new_with_label("Filler");
	gtk_box_pack_start(GTK_BOX(box), btn, TRUE, TRUE, 0);

	test = od_multibutton_new();
	od_multibutton_set_config(OD_MULTIBUTTON(test), TRUE);
//	gtk_container_set_border_width(GTK_CONTAINER(test), 5);
//	od_multibutton_set_widget_text(OD_MULTIBUTTON(test), 0, "Achtung", NULL);
	od_multibutton_set_widget_text(OD_MULTIBUTTON(test), 0, "Delete", NULL);
	od_multibutton_set_widget_text(OD_MULTIBUTTON(test), 1, "Copy <span color=\"#ff0000\">As</span>", NULL);
	g_signal_connect(G_OBJECT(test), "clicked",  G_CALLBACK(evt_clicked), NULL);
	gtk_box_pack_start(GTK_BOX(box), test, TRUE, TRUE, 0);

	btn = gtk_button_new_with_label("Random Color");
	g_signal_connect(G_OBJECT(btn), "clicked", G_CALLBACK(evt_color_clicked), test);
	gtk_box_pack_start(GTK_BOX(box), btn, TRUE, TRUE, 0);

	btn = gtk_button_new_with_label("Clear Lock");
	g_signal_connect(G_OBJECT(btn), "clicked", G_CALLBACK(evt_clear_clicked), test);
	gtk_box_pack_start(GTK_BOX(box), btn, TRUE, TRUE, 0);

	{
		GtkWidget	*btn, *evb, *lab;
		GdkColor	test;

		lab = gtk_label_new("Event Test");
		evb = gtk_event_box_new();
		gtk_container_add(GTK_CONTAINER(evb), lab);
		gdk_color_parse("red", &test);
		gtk_widget_modify_bg(evb, GTK_STATE_NORMAL, &test);
		gtk_widget_modify_bg(evb, GTK_STATE_PRELIGHT, &test);
		gtk_widget_modify_bg(evb, GTK_STATE_ACTIVE, &test);
		btn = gtk_button_new();
		gtk_container_add(GTK_CONTAINER(btn), evb);
		gtk_box_pack_start(GTK_BOX(box), btn, TRUE, TRUE, 0);
	}

/*	btn = od_multibutton_new();
	od_multibutton_set_config(OD_MULTIBUTTON(btn), TRUE);
	od_multibutton_set_widget_text(OD_MULTIBUTTON(btn), 0, "Test Test", NULL);
	mb_random_colors(btn, 0);
	gtk_box_pack_start(GTK_BOX(box), btn, TRUE, TRUE, 0);
*/
	btn = gtk_button_new_with_label("Exit Config");
	g_signal_connect(G_OBJECT(btn), "clicked", G_CALLBACK(evt_exit_config_clicked), test);
	gtk_box_pack_start(GTK_BOX(box), btn, TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(win), box);

	gtk_widget_show_all(win);
	gtk_main();

	return EXIT_SUCCESS;
}

#endif		/* ODMULTIBUTTON_STANDALONE */
