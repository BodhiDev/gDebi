/*
** 1998-11-26 -	Header for the directory history handling module. Mainly called from the dirpane module.
*/

#if !defined DIRHISTORY_H
#define	DIRHISTORY_H

/* ----------------------------------------------------------------------------------------- */

typedef struct DHSel		DHSel;
typedef struct DirHistory	DirHistory;

/* ----------------------------------------------------------------------------------------- */

extern DirHistory *	dph_dirhistory_new(void);

extern const gchar *	dph_dirhistory_get_entry(const DirHistory *dh, guint index);

extern DHSel *		dph_dirsel_new(DirPane *dp);
extern void		dph_dirsel_apply(DirPane *dp, const DHSel *sel);
extern void		dph_dirsel_destroy(DHSel *sel);

extern gfloat		dph_vpos_get(const DirPane *dp);
extern void		dph_vpos_set(DirPane *dp, gfloat vpos);

extern void		dph_state_save(DirPane *dp);
extern void		dph_state_restore(DirPane *dp);

extern const gchar *	dph_entry_get_parse_name(const void *entry);

extern const gchar *	dph_history_get_first(const DirPane *dp);

extern void		dph_history_save(MainInfo *min, const DirPane *dp, gsize num);
extern void		dph_history_load(MainInfo *min, DirPane *dp, gsize num);

#endif		/* DIRHISTORY_H */
