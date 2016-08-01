/*
** 1998-08-15 -	Header for the icon utility module.
*/

#if !defined ICONUTIL_H
#define	ICONUTIL_H

typedef struct IconInfo	IconInfo;

extern IconInfo *	ico_initialize(MainInfo *min);

extern GList *		ico_get_all(MainInfo *min, const gchar *path);
extern gboolean		ico_no_icon(const gchar *name);
extern void		ico_free_all(GList *list);

extern GdkPixbuf *	ico_icon_get_pixbuf(MainInfo *min, const gchar *name);

extern void		ico_flush(MainInfo *min);

#endif		/* ICONUTIL_H */
