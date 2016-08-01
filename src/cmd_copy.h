/*
** 1998-05-23 -	Header file for the native COPY command implementation. Shortish.
*/

/* Core file copying routines, might need a bit more thought on use. */
extern gboolean	copy_regular(MainInfo *min, const gchar *from, const gchar *to, const struct stat *sstat);
extern gboolean	copy_device(MainInfo *min, const gchar *from, const gchar *to, const struct stat *sstat);
extern gboolean	copy_link(MainInfo *min, const gchar *from, const gchar *to, const struct stat *sstat);
extern gboolean copy_fifo(MainInfo *min, const gchar *from, const gchar *full_to, const struct stat *sstat);
extern gboolean	copy_socket(MainInfo *min, const gchar *from, const gchar *to, const struct stat *sstat);

/* Top-level copying functions, for external use. */
extern gboolean	copy_dir(MainInfo *min, const gchar *from, const gchar *to);
extern gboolean	copy_file(MainInfo *min, const gchar *from, const gchar *to, const struct stat *sstat);

extern gboolean	copy_gfile(MainInfo *min, DirPane *src, DirPane *dst, GFile *from, GFile *to, GError **err);

extern gint	cmd_copy(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);

extern gsize	cpy_get_buf_size(void);

extern void	cfg_copy(MainInfo *min);
