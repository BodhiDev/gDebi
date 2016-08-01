/*
** 1998-07-12 -	Header for the new GUI utility module, that implements a few utility
**		functions which are handy when building various kinds of GUIs.
*/

extern GtkWidget *	gui_details_button_new(void);
extern GtkWidget *	gui_dialog_entry_new(void);
extern GSList *		gui_radio_group_new(guint num, const gchar **label, GtkWidget *but[]);

extern gpointer		gui_progress_begin(const gchar *body, const gchar *button);
extern gboolean		gui_progress_update(gpointer anchor, gfloat value, const gchar *obj);
extern void		gui_progress_end(gpointer anchor);

extern GtkWidget *	gui_build_menu(const gchar *text[], GCallback func, gpointer data);
extern GtkWidget *	gui_build_combo_box(const gchar *text[], GCallback func, gpointer user);
extern void		gui_combo_box_set_blocked(GtkWidget *widget, gboolean blocked);

typedef struct GuiHandlerGroup	GuiHandlerGroup;

extern GuiHandlerGroup*	gui_handler_group_new(void);
extern gulong		gui_handler_group_connect(GuiHandlerGroup *g, GObject *obj, const gchar *signal, GCallback cb, gpointer user);
extern void		gui_handler_group_block(const GuiHandlerGroup *g);
extern void		gui_handler_group_unblock(const GuiHandlerGroup *g);

extern void		gui_set_main_title(MainInfo *min, const gchar *title);
extern void		gui_color_from_rgba(GdkColor *color, const GdkRGBA *rgba);
extern void		gui_rgba_from_color(GdkRGBA *rgba, const GdkColor *color);

extern void		gui_events_flush(void);
