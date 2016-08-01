/*
** odmultibutton - A two-faced button widget.
**
** An attempt at writing a GTK+ button widget, for gentoo. This is a GTK+ 2.x
** rewrite from scratch, by Emil Brink. The original widget, called ODEmilButton,
** was done by Johan Hanson, but this is not a mere "port" of his code, it's a
** complete re-write, although "white box" in nature.
**
*/

#if !defined OD_MULTIBUTTON_H
#define OD_MULTIBUTTON_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define OD_MULTIBUTTON_TYPE		(od_multibutton_get_type())
#define OD_MULTIBUTTON(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), OD_MULTIBUTTON_TYPE, ODMultiButton))
#define OD_MULTIBUTTON_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass), OD_MULTIBUTTON_TYPE, ODMultiButtonClass))
#define OD_IS_MULTIBUTTON(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), OD_MULTIBUTTON_TYPE))
#define OD_IS_MULTIBUTTON_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), OD_MULTIBUTTON_TYPE))

typedef struct
{
	GtkWidget	*widget;
	gpointer	user;
} MultiButtonPage;

typedef struct
{
	GtkButton	button;
	/* This stuff really is private. */
	MultiButtonPage	page[2];
	guint		width, height;

	gboolean	config;
	gboolean	config_down;		/* Used in config mode, for toggle button action. */
	guint		last_index;

	guint		trace_mask;
} ODMultiButton;

typedef struct
{
	GtkButtonClass	parent_class;
} ODMultiButtonClass;

extern GType		od_multibutton_get_type(void);

extern GtkWidget *	od_multibutton_new(void);
extern void		od_multibutton_set_trace(ODMultiButton *mb, unsigned int trace_mask);
extern void		od_multibutton_set_config(ODMultiButton *mb, gboolean config);
extern void		od_multibutton_set_config_selected(ODMultiButton *mb, gboolean selected);
extern void		od_multibutton_set_text(ODMultiButton *mb, guint index, const gchar *text,
						const GdkColor *bg, const GdkColor *fg, gpointer user);
extern gint		od_multibutton_get_index(const ODMultiButton *mb);
extern gpointer		od_multibutton_get_userdata_last(const ODMultiButton *mb);

G_END_DECLS

#endif		/* OD_MULTIBUTTON_H */
