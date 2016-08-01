/*
** 1998-05-23 -	Header file for the error-handling module.
*/

extern void	err_clear(MainInfo *min);
extern void	err_printf(MainInfo *min, const gchar *fmt, ...);
extern gint	err_set(MainInfo *min, gint code, const gchar *source, const gchar *obj);
extern void	err_set_gerror(MainInfo *min, GError **error, const gchar *source, const GFile *file);
extern void	err_show(MainInfo *min);
