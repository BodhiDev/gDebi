/*
** 1998-05-24 -	Header for the built-in Delete command. Can you say "simple"?
*/

extern gboolean	del_delete_dir(MainInfo *min, const gchar *path, gboolean progress);
extern gboolean	del_delete_file(MainInfo *min, const gchar *path);

extern gboolean	del_delete_gfile(MainInfo *min, const GFile *file, gboolean progress, GError **error);

extern gint	cmd_delete(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);

extern void	cfg_delete(MainInfo *min);
