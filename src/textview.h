/*
** 1998-05-19 -	Header file for the text viewing module. Real complex.
** 1999-03-02 -	Completely redesigned interface. Now uses a (rather) opaque
**		GTK+ widget. Makes life easier for the cmdgrab module.
*/

#if !defined TEXTVIEW_H
#define TEXTVIEW_H

/* Open and return a pointer to a text viewing window. The only thing the caller
** is allowed to assume about it is that it is a GTK+ window widget. Its GTK+
** user data slot is used internally by the txv module, so keep out! Also, it's
** not a good idea to destroy it by any other means than calling txv_close() on it.
*/
extern GtkWidget *	txv_open(MainInfo *min, const gchar *label);

extern void	txv_show(GtkWidget *textviewer);

extern gulong	txv_connect_delete(GtkWidget *wid, GCallback func, gpointer user);
extern gulong	txv_connect_keypress(GtkWidget *wid, GCallback func, gpointer user);

extern void	txv_set_label(GtkWidget *wid, const gchar *text);
extern void	txv_set_label_from_file(GtkWidget *wid, GFile *file);

extern void	txv_text_append(GtkWidget *wid, const gchar *text, gsize length);
extern void	txv_text_append_with_color(GtkWidget *wid, const gchar *text, gsize length, const GdkColor *color);

extern gint	txv_text_load(GtkWidget *wid, GFile *file, gsize buf_size, const gchar *encoding, GError **err);
extern gint	txv_text_load_hex(GtkWidget *wid, GFile *file, GError **err);

extern void	txv_enable(GtkWidget *wid);

extern void	txv_close(GtkWidget *wid);

#endif		/* TEXTVIEW_H */
