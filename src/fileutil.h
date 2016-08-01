/*
** 1998-05-25 -	Header file for the collection of miscellaneous file utilities.
*/

/* This is filled-in by a call to fut_dir_size(). Very informative. */
typedef struct {
	guint	num_dirs;
	guint	num_files;
	guint	num_links;
	guint	num_specials;

	guint	num_total;	/* Sum of the above. */

	guint64	num_bytes;	/* For completeness. */
	guint64	num_blocks;
} FUCount;

/* ----------------------------------------------------------------------------------------- */

/* Directory size computation, the next generation. */
gboolean	fut_size_gfile(MainInfo *min, const GFile *dir, guint64 *bytes, FUCount *fc, GError **error);

/* Asynchronous directory size computation. Calls <func> with a const FUCount *
** every now and then, then with NULL when done.
*/
typedef void (*SizeFunc)(const FUCount *count, gpointer user);
gpointer	fut_dir_size_start(const gchar *path, SizeFunc func, gpointer user);
void		fut_dir_size_stop(gpointer handle);

/* Asynchronous (in fact, multi-threaded) directory size computation. */
gpointer	fut_dir_size_gfile_start(GFile *dir, SizeFunc func, gpointer user);
void		fut_dir_size_gfile_stop(gpointer handle);

const gchar *	fut_locate(const gchar *paths, const gchar *name);
GList *		fut_scan_path(const gchar *paths, gint (*filter)(const gchar *path, const gchar *name));
void		fut_free_path(GList *list);
gint		fut_path_component(const gchar *paths, gchar *component);

void		fut_interpolate(gchar *buffer);

gboolean	fut_cd(const gchar *new_dir, gchar *old, gsize old_max);

#if 0
GList *		fut_stat_chain_new(const gchar *path);
gboolean	fut_stat_chain_prefix_equal(const GList *chain1, const GList *chain2);
void		fut_stat_chain_free(GList *chain);
#endif

gboolean	fut_path_canonicalize(const gchar *path, gchar *outbuf, gsize outmax);

gboolean	fut_is_parent_of(const char *from, const char *to);

gboolean	fut_exists(const gchar *name);

gboolean	fut_can_read(const struct stat *stat, uid_t uid, gid_t gid);
gboolean	fut_can_read_named(const gchar *name);
gboolean	fut_can_write(const struct stat *stat, uid_t uid, gid_t gid);

gboolean	fut_check_hide(DirPane *dp, const gchar *name);
