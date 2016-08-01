/*
** 1998-08-02 -	Header file for the file-typing module. Interesting?
*/

#if !defined TYPES_H
#define	TYPES_H

#define	TYP_IS_UNKNOWN(t)	((t)->mode == 0)

extern FType *	typ_type_new(CfgInfo *cfg, const gchar *name, mode_t mode, gint perm, const gchar *suffix, const gchar *name_re, const gchar *file_re);
extern FType *	typ_type_copy(FType *old);
extern GList *	typ_type_insert(GList *list, FType *after, FType *type);
extern GList *	typ_type_remove(GList *list, FType *type);
extern GList *	typ_type_lookup_glob(const GList *list, const gchar *glob);
extern void	typ_type_destroy(FType *type);

extern FType *	typ_type_get_unknown(const CfgInfo *cfg);

extern GList *	typ_type_set_name(GList *list, FType *type, const gchar *name);
extern GList *	typ_type_set_style(GList *list, FType *type, StyleInfo *si, const gchar *name);
extern GList *	typ_type_set_priority(GList *list, FType *type, guint16 prio);
extern GList *	typ_type_move(GList *list, FType *type, gint delta);

extern void	typ_identify_begin(DirPane *dp);
extern void	typ_identify(DirPane *dp, const DirRow2 *row);
extern gboolean	typ_identify_end(DirPane *dp);

extern void	typ_init(CfgInfo *cfg);

#endif		/* TYPES_H */
