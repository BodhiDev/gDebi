/*
** 1998-09-25 -	Header for the new command sequence module. It's a rewrite!
*/

#if !defined CMDSEQ_H
#define	CMDSEQ_H

#include "cmdarg.h"

/* ----------------------------------------------------------------------------------------- */

typedef gint (*Command)(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
typedef void (*CommandCfg)(MainInfo *min);

extern const gchar *	csq_cmdseq_unique_name(GHashTable *hash);

extern const gchar *	csq_cmdseq_map_name(const gchar *name, const gchar *context);

extern CmdSeq *	csq_cmdseq_new(const gchar *name, guint32 flags);
extern void	csq_cmdseq_set_name(GHashTable *hash, CmdSeq *cs, const gchar *name);
extern CmdSeq *	csq_cmdseq_copy(const CmdSeq *src);
extern void	csq_cmdseq_hash(GHashTable **hash, CmdSeq *cs);

extern void	csq_cmdseq_destroy(CmdSeq *cs);
extern gint	csq_cmdseq_row_append(CmdSeq *cs, CmdRow *row);
extern void	csq_cmdseq_rows_relink(CmdSeq *cs, GList *rows);
extern gint	csq_cmdseq_row_delete(CmdSeq *cs, CmdRow *row);
extern void	csq_cmdseq_row_delete_all(CmdSeq *cs);

extern gint	csq_execute(MainInfo *min, const gchar *name);
extern gint	csq_execute_format(MainInfo *min, const gchar *fmt, ...);
extern void	csq_continue(MainInfo *min);
extern gboolean	csq_handle_ba_flags(MainInfo *min, guint32 flags);

extern const gchar *	csq_cmdrow_type_to_string(CRType type);
extern CRType	csq_cmdrow_string_to_type(const gchar *type);

extern CmdRow *	csq_cmdrow_new(CRType type, const gchar *def, guint32 flags);
extern CmdRow *	csq_cmdrow_copy(const CmdRow *src);
extern void	csq_cmdrow_set_type(CmdRow *row, CRType type);
extern void	csq_cmdrow_set_def(CmdRow *row, const gchar *def);
extern void	csq_cmdrow_destroy(CmdRow *row);

extern void	csq_init_commands(MainInfo *min);

#endif		/* CMDSEQ_H */
