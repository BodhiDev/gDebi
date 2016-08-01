/*
** 1999-12-23 -	This is the window module. It's main responsibility is to provide a
**		way of tracking a bunch of windows' sizes and positions, and making
**		that information persistent through the config file.
*/

#if !defined WINDOW_H
#define	WINDOW_H

#include "xmlutil.h"

typedef struct WinInfo	WinInfo;

/* The windows that are handled by this module are well-known and few,
** so we can simply define their global IDs here, and use these symbols
** in place of the <id> parameter in all calls of this module.
*/
enum {	WIN_MAIN,		/* The main window, with the panes and all. */
	WIN_CONFIG,		/* The window used by the Configure command. */
	WIN_TEXTVIEW,		/* The window used by ViewText and output capture. */
	WIN_INFO,		/* The window used by the Info command. */
	WIN_INVALID = ~0	/* No window can have this ID. */
};

/* The different types of windows we support. The difference is simply in the GTK+
** call used to create the widget for the window. Each type is commented with the
** kind of call we will issue in win_window_open() to create it.
*/
typedef enum {
	WIN_TYPE_SIMPLE_TOPLEVEL,	/* gtk_window_new(GTK_WINDOW_TOPLEVEL). */
	WIN_TYPE_SIMPLE_DIALOG,		/* gtk_window_new(GTK_WINDOW_DIALOG). */
	WIN_TYPE_SIMPLE_POPUP,		/* gtk_window_new(GTK_WINDOW_POPUP). */
	WIN_TYPE_COMPLEX_DIALOG		/* gtk_dialog_new(). */
} WinType;

/* ----------------------------------------------------------------------------------------- */

WinInfo *	win_wininfo_new(MainInfo *min);
WinInfo *	win_wininfo_new_default(MainInfo *min);
WinInfo *	win_wininfo_copy(const WinInfo *wi);
void		win_wininfo_destroy(WinInfo *wi);

GtkWidget *	win_wininfo_build(const WinInfo *wi, gboolean *modified);

void		win_wininfo_save(const WinInfo *wi, FILE *out);
void		win_wininfo_load(WinInfo *wi, const XmlNode *node);

void		win_borders_set(WinInfo *, gint width, gint height);
void		win_borders_get(const WinInfo *, gint *width, gint *height);

void		win_window_new(WinInfo *wi, guint32 id, WinType type, gboolean persistent, const gchar *title, const gchar *label);
void		win_window_pos_grab_set(const WinInfo *wi, guint32 id, gboolean grab);
void		win_window_pos_set(const WinInfo *wi, guint32 id, gint x, gint y);
gboolean	win_window_pos_get(const WinInfo *wi, guint32 id, gint *x, gint *y);
void		win_window_pos_use_set(const WinInfo *wi, guint32 id, gboolean enabled);
gboolean	win_window_pos_use_get(const WinInfo *wi, guint32 id);
void		win_window_pos_update_set(const WinInfo *wi, guint32 id, gboolean enabled);
gboolean	win_window_pos_update_get(const WinInfo *wi, guint32 id);

void		win_window_size_grab_set(const WinInfo *wi, guint32 id, gboolean grab);
void		win_window_size_set(const WinInfo *wi, guint32 id, gint width, gint height);
gboolean	win_window_size_get(const WinInfo *wi, guint32 id, gint *width, gint *height);
void		win_window_size_use_set(const WinInfo *wi, guint32 id, gboolean enabled);
gboolean	win_window_size_use_get(const WinInfo *wi, guint32 id);
void		win_window_size_update_set(const WinInfo *wi, guint32 id, gboolean enabled);
gboolean	win_window_size_update_get(const WinInfo *wi, guint32 id);

void		win_window_destroy(WinInfo *wi, guint32 id);

GtkWidget *	win_window_open(const WinInfo *wi, guint32 id);
void		win_window_relink(const WinInfo *wi, guint32 id, GtkWidget *window);
void		win_window_set_title(GtkWidget *window, const gchar *title);
void		win_window_show(GtkWidget *window);	/* Use instead of gtk_widget_show(). */
gboolean	win_window_update(GtkWidget *window);
gboolean	win_window_close(GtkWidget *window);

GtkWidget *	win_dialog_open(GtkWidget *main_window);	/* Use instead of gtk_dialog_new(), for icons. */

#endif		/* WINDOW_H */
