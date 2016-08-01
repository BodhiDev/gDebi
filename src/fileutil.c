/*
** 1998-05-25 -	This module contains various file and directory related operations, that
**		are needed here and there in the application.
** 1999-11-13 -	Rewrote the core fut_copy() function. Shorter, simpler, better. Added
**		a complementary fut_copy_partial() function that copies just parts of files.
*/

#include "gentoo.h"

#include <ctype.h>
#include <fcntl.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "errors.h"
#include "progress.h"
#include "strutil.h"
#include "userinfo.h"

#include "fileutil.h"

/* ----------------------------------------------------------------------------------------- */

#define	USER_NAME_SIZE	(64)		/* Just an assumption, really. */

#define	MIN_COPY_CHUNK	(1 << 17)

/* ----------------------------------------------------------------------------------------- */

/* 1998-05-21 -	Enter a directory, saving the old path for restoration later.
**		If <old> is NULL, no storing is done. Returns boolean success.
*/
gboolean fut_cd(const gchar *new_dir, gchar *old, gsize old_max)
{
	if(old != NULL)
	{
		if(getcwd(old, old_max) == NULL)
			return FALSE;
	}
	return chdir(new_dir) == 0 ? TRUE : FALSE;
}

#if 0
/* 2003-10-10 -	Compute a directory stat chain from <path>. It's made
**		up of a list of stat(2) structures up to the root.
*/
GList * fut_stat_chain_new(const gchar *path)
{
	gchar	canon[PATH_MAX];
	GList	*chain = NULL;
	GString	*dir;

	if(!fut_path_canonicalize(path, canon, sizeof canon))
		return NULL;

	dir = g_string_new(canon);

	while(dir->len && strcmp(dir->str, G_DIR_SEPARATOR_S) != 0)
	{
		struct stat	stbuf;

		if(stat(dir->str, &stbuf) == 0)
		{
			struct stat	*st = g_malloc(sizeof *st);
			const char *sep;

			*st = stbuf;
			chain = g_list_append(chain, st);
			if((sep = strrchr(dir->str, G_DIR_SEPARATOR)) != NULL)
				g_string_truncate(dir, sep - dir->str);
			else
				break;
		}
		else	/* A stat() call failed, then path is bad and no chain exists. Fail. */
		{
			fut_stat_chain_free(chain);
			return NULL;
		}
	}
	return chain;
}

/* 2003-10-10 -	Returns true if the second chain is a prefix of the first, which can also
**		be more clearly put as "chain1 represents a subdir of chain2". Um, at least
**		that's a different wording.
*/
gboolean fut_stat_chain_prefix_equal(const GList *chain1, const GList *chain2)
{
	if(!chain1 || !chain2)
		return FALSE;

	for(; chain1 && chain2; chain1 = g_list_next(chain1), chain2 = g_list_next(chain2))
	{
		const struct stat	*s1 = chain1->data, *s2 = chain2->data;

		if(s1->st_dev != s2->st_dev || s1->st_ino != s2->st_ino)
			return FALSE;
	}
	return TRUE;
}

void fut_stat_chain_free(GList *chain)
{
	GList	*iter;

	for(iter = chain; iter != NULL; iter = g_list_next(iter))
		g_free(iter->data);
	g_list_free(chain);
}
#endif

/* 2003-10-10 -	Get next path part, up to (but not including) the next separator. */
static GString * get_part(GString *in, char *out)
{
	const char	*sep = strchr(in->str, G_DIR_SEPARATOR);

	if(sep)
	{
		gsize	len = sep - in->str;
		strncpy(out, in->str, len);
		out[len] = '\0';
		g_string_erase(in, 0, len + 1);
		return in;
	}
	return NULL;
}

/* 2003-10-10 -	Make a path canonical, i.e. make it absolute and remove any "..", "."
**		and repeated slashes it might contain. Pure string operation.
*/
gboolean fut_path_canonicalize(const gchar *path, gchar *outpath, gsize maxout)
{
	GString	*dir, *out;
	char	part[PATH_MAX];

	if(!path || *path == '\0' || !outpath || maxout < 2)
		return FALSE;

	if(*path == G_DIR_SEPARATOR)
		dir = g_string_new(path);
	else
	{
		char	buf[PATH_MAX];

		if(getcwd(buf, sizeof buf) == NULL)
			return FALSE;
		if(buf[0] != G_DIR_SEPARATOR)
			dir = g_string_new(G_DIR_SEPARATOR_S);
		else
			dir = g_string_new(buf);
		g_string_append_c(dir, G_DIR_SEPARATOR);
		g_string_append(dir, path);
	}
	g_string_append_c(dir, G_DIR_SEPARATOR);		/* Must end in separator for get_part(). */

	out = g_string_new("");
	while(get_part(dir, part))
	{
		if(*part)
		{
			if(strcmp(part, ".") == 0)
				continue;
			else if(strcmp(part, "..") == 0)
			{
				const char	*sep = strrchr(out->str, G_DIR_SEPARATOR);
				g_string_truncate(out, sep - out->str);
				continue;
			}
			g_string_append_c(out, G_DIR_SEPARATOR);
			g_string_append(out, part);
		}
	}
	g_string_free(dir, TRUE);
	if(out->len < maxout)
	{
		strcpy(outpath, out->str);
		g_string_free(out, TRUE);
		return TRUE;
	}
	g_string_free(out, TRUE);
	return FALSE;
}

/* 2003-10-10 -	Check if the directory <to> is a child directory of <from>. If it is, copying <from>
**		into <to> is a genuine Bad Idea.
*/
gboolean fut_is_parent_of(const char *from, const char *to)
{
	gchar	buf1[PATH_MAX], buf2[PATH_MAX];

	if(fut_path_canonicalize(from, buf1, sizeof buf1) &&
	   fut_path_canonicalize(to,   buf2, sizeof buf2))
	{
		gsize	len = strlen(buf1);

		if(strncmp(buf1, buf2, len) == 0)
			return buf2[len] == G_DIR_SEPARATOR;	/* If last char is /, buf1 is really a prefix. */
	}
	return FALSE;
}

/* 1998-09-23 -	Return 1 if the named file exists, 0 otherwise. Since this is Unix
**		(I think), this matter is somewhat complex. But I choose to ignore
**		that for now, and pretend it's nice and simple. :)
*/
gboolean fut_exists(const gchar *name)
{
	return access(name, F_OK) == 0;
}

/* ----------------------------------------------------------------------------------------- */

static gboolean	do_size_gfile_info(MainInfo *min, const GFile *object, const GFileInfo *fi, guint64 *bytes, FUCount *fc, GError **error);

static gboolean do_size_gfile_dir(MainInfo *min, const GFile *root, guint64 *bytes, FUCount *fc, GError **error)
{
	GFileEnumerator	*fe;
	GFileInfo	*fi;
	GFile		*child;
	gboolean	ok = TRUE;

	if((fe = g_file_enumerate_children((GFile *) root, "standard::*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error)) == NULL)
		return FALSE;
	while(ok && ((fi = g_file_enumerator_next_file(fe, NULL, error)) != NULL))
	{
		if((child = g_file_get_child_for_display_name((GFile *) root, g_file_info_get_display_name(fi), NULL)) != NULL)
		{
			ok = do_size_gfile_info(min, child, fi, bytes, fc, error);
			g_object_unref(child);
		}
		else
			ok = FALSE;
		g_object_unref(fi);
	}
	if(ok && fc != NULL)
		fc->num_total = fc->num_dirs + fc->num_files + fc->num_links + fc->num_specials;

	g_object_unref(fe);

	return ok;
}

static gboolean do_size_gfile_info(MainInfo *min, const GFile *object, const GFileInfo *fi, guint64 *bytes, FUCount *fc, GError **error)
{
	gboolean	ret = TRUE;

	if(bytes != NULL)
		*bytes += g_file_info_get_size((GFileInfo *) fi);
	if(fc != NULL)
	{
		fc->num_bytes += g_file_info_get_size((GFileInfo *) fi);
		fc->num_blocks += g_file_info_get_attribute_uint64((GFileInfo *) fi, G_FILE_ATTRIBUTE_UNIX_BLOCKS);
	}
	switch(g_file_info_get_file_type((GFileInfo *) fi))
	{
	case G_FILE_TYPE_REGULAR:
		if(fc != NULL)
			fc->num_files++;
		break;
	case G_FILE_TYPE_DIRECTORY:
		if(fc != NULL)
			fc->num_dirs++;
		ret = do_size_gfile_dir(min, object, bytes, fc, error);
		break;
	case G_FILE_TYPE_SYMBOLIC_LINK:
		if(fc != NULL)
			fc->num_links++;
		break;
	case G_FILE_TYPE_SPECIAL:
		/* FIXME: This is not supported in the GIO world ... Has to go. */
		break;
	default:
		g_warning("Can't handle irregular file type in do_size_gfile_info()");
	}
	return ret;
}

gboolean fut_size_gfile(MainInfo *min, const GFile *file, guint64 *bytes, FUCount *fc, GError **error)
{
	GFileInfo	*fi;
	gboolean	ret = TRUE;

	if(min == NULL || file == NULL)
		return FALSE;
	if((fi = g_file_query_info((GFile *) file, "standard::*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error)) == NULL)
		return FALSE;
	if(fc != NULL)		/* If there is an fc present, wipe it clean. */
		memset(fc, 0, sizeof *fc);
	ret = do_size_gfile_info(min, file, fi, bytes, fc, error);
	/* Adjust, don't consider a directory to contain itself. */
	if(g_file_info_get_file_type(fi) == G_FILE_TYPE_DIRECTORY && fc != NULL)
		fc->num_dirs--;
  	g_object_unref(fi);

	return ret;
}

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	GMutex		mutex;		/* Lock this before accessing counts. */
	gboolean	changed;	/* This is set if the data has changed. */
	FUCount		counts;
} FUCountSafe;

typedef struct {
	GFile		*root;
	GCancellable	*cancel;
	FUCountSafe	counts;		/* Here's where the thread updates. */
	guint		depth;
	SizeFunc	func;
	gpointer	func_user;
	guint		timeout;
	GThread		*thread;
} SizeGFileInfo;

static void thread_recurse_directory(GFile *root, SizeGFileInfo *info)
{
	GFileEnumerator	*fen;

	if((fen = g_file_enumerate_children(root, "*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, info->cancel, NULL)) != NULL)
	{
		GFileInfo	*fin;

		while(!g_cancellable_is_cancelled(info->cancel) && (fin = g_file_enumerator_next_file(fen, info->cancel, NULL)) != NULL)
		{
			const GFileType	type = g_file_info_get_file_type(fin);

			/* Grab the lock, and increase the proper counter based on type. */
			g_mutex_lock(&info->counts.mutex);
			info->counts.counts.num_total++;
			if(type == G_FILE_TYPE_REGULAR)
			{
				info->counts.counts.num_files++;
				info->counts.counts.num_bytes += g_file_info_get_size(fin);
			}
			else if(type == G_FILE_TYPE_DIRECTORY)
				info->counts.counts.num_dirs++;
			else if(type == G_FILE_TYPE_SYMBOLIC_LINK)
				info->counts.counts.num_links++;
			else if(type == G_FILE_TYPE_SPECIAL)
				info->counts.counts.num_specials++;
			info->counts.changed = TRUE;
			/* All done, release the lock for the rest. */
			g_mutex_unlock(&info->counts.mutex);

			/* Wait now, if that was a directory we need to enter it! */
			if(type == G_FILE_TYPE_DIRECTORY)
			{
				GFile	*child;

				if((child = g_file_get_child(root, g_file_info_get_name(fin))) != NULL)
				{
					info->depth++;
					thread_recurse_directory(child, info);
					info->depth--;
					g_object_unref(child);
				}
			}
		}
		g_file_enumerator_close(fen, info->cancel, NULL);
		g_object_unref(G_OBJECT(fen));
	}
}

static gpointer size_gfile_thread_func(gpointer data)
{
	SizeGFileInfo	*info = data;

	thread_recurse_directory(info->root, info);

	return NULL;
}

static gboolean cb_size_gfile_timeout(gpointer user)
{
	SizeGFileInfo	*info = user;

	/* Has the data changed? */
	if(info->counts.changed)
	{
		FUCount	local;

		/* Try to quickly create a local copy. */
		g_mutex_lock(&info->counts.mutex);
		local = info->counts.counts;
		info->counts.changed = FALSE;
		g_mutex_unlock(&info->counts.mutex);

		/* Then call callback using the local copy, leaving the true data free to
		 * update by the thread while the GUI-building code in the callback runs.
		 */
		info->func(&local, info->func_user);
	}
	return !g_cancellable_is_cancelled(info->cancel);
}

gpointer fut_dir_size_gfile_start(GFile *dir, SizeFunc func, gpointer user)
{
	SizeGFileInfo	*info;

	if(dir == NULL || func == NULL)
		return NULL;

	info = g_malloc(sizeof *info);
	info->root = dir;
	info->cancel = g_cancellable_new();
	g_mutex_init(&info->counts.mutex);
	info->counts.changed = FALSE;
	memset(&info->counts.counts, 0, sizeof info->counts.counts);
	info->depth = 0;
	info->func = func;
	info->func_user = user;

	info->timeout = g_timeout_add(350, cb_size_gfile_timeout, info);

	info->thread = g_thread_new("gentoo:dirsize", size_gfile_thread_func, info);

	return info;
}

void fut_dir_size_gfile_stop(gpointer handle)
{
	SizeGFileInfo	*info = handle;

	if(info == NULL)
		return;
	/* If we remove this first, and we know we're not in it now (main thread only), we can be
	 * pretty sure it's safe to tear down the rest since the timeout function won't run again.
	 */
	g_source_remove(info->timeout);

	/* Now, stop the thread and kill everything. */
	g_cancellable_cancel(info->cancel);
	g_thread_join(info->thread);
	if(info->counts.changed)
		info->func(&info->counts.counts, info->func_user);

	g_object_unref(G_OBJECT(info->cancel));
	g_free(info);
}

/* ----------------------------------------------------------------------------------------- */

/* 2003-11-29 -	Here's a strtok()-workalike. Almost. The arg <paths> should be either a string
**		of colon-separated path components (e.g. "/usr/local:/home/emil:/root"), or NULL.
**		The function fills in <component> with either the first component, or, when <paths>
**		is NULL, with the _next_ component (relative to the last one). If this sounds confusing,
**		read the man page for strtok(3) and associate freely from there. The main difference
**		between this and strtok(3) is that the latter modifies the string being tokenized,
**		while this function does not. The <component> buffer is assumed to have room for
**		PATH_MAX bytes. Returns 0 if no more components were found, 1 otherwise.
*/
int fut_path_component(const gchar *paths, gchar *component)
{
	static const gchar	*src = NULL;
	gchar			here, *dst = component;

	if(paths != NULL)
		src = paths;
	else if(src == NULL)
		return 0;

	/* First, find the component, if processing a full multi-directory path. */
	while(*src == ':')
		src++;
	for(; (here = *src) != '\0'; src++)
	{
		if(here == ':' && src[1] == G_DIR_SEPARATOR)	/* Colon followed by separator ends a path. */
		{
			src += 1;	/* Leave src at final separator for next time. */
			break;
		}
		*dst++ = here;
	}
	*dst = '\0';
	if(here == '\0')		/* End of path list reached? */
		src = NULL;		/* Makes us stop next time. */
	if(dst > component)
		fut_interpolate(component);

	return dst > component;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-22 -	Evaluate (look up) any environment variable(s) used in <component>. Replaces
**		the entire string with a new version, where all environment variable names
**		have been replaced by the corresponding variable's value. Undefined variables
**		are treated as if their value were "" (empty string). Considers anything with
**		letters, digits and underscores to be a valid variable name. You can also use
**		braces {around} any combination of characters to force a look-up on that.
*/
static void eval_env(gchar *component)
{
	GString	*temp = NULL;
	gchar	*ptr, vname[256], *vptr;

	for(ptr = component; *ptr != '\0'; ptr++)
	{
		if(*ptr == '$')		/* Environment variable? */
		{
			const gchar	*start = ptr, *vval;

			ptr++;
			if(*ptr == '{')		/* Braced name? */
			{
				for(vptr = vname, ptr++; *ptr && *ptr != '}' &&
					(gsize) (vptr - vname) < sizeof vname - 1;)
				{
					*vptr++ = *ptr++;
				}
				ptr++;
			}
			else
			{
				for(vptr = vname; *ptr && (isalnum((guchar) *ptr) || *ptr == '_') &&
					(gsize) (vptr - vname) < sizeof vname - 1;)
				{
					*vptr++ = *ptr++;
				}
			}
			*vptr = '\0';
			if((vval = g_getenv(vname)) != NULL)
			{
				if(temp == NULL)
				{
					temp = g_string_new(component);
					g_string_truncate(temp, start - component);
				}
				if(*vval == G_DIR_SEPARATOR)
					g_string_truncate(temp, 0);
				g_string_append(temp, vval);
			}
			ptr--;
		}
		else
		{
			if(temp)
				g_string_append_c(temp, *ptr);
		}
	}
	if(temp)
	{
		g_strlcpy(component, temp->str, PATH_MAX);
		g_string_free(temp, TRUE);
	}
}

static void eval_home(gchar *component)
{
	GString		*temp;
	gchar		ubuf[USER_NAME_SIZE], *uptr;
	const gchar	*ptr, *uname = ubuf, *dir;

	if((temp = g_string_new(NULL)) != NULL)
	{
		for(ptr = component + 1, uptr = ubuf; (*ptr && *ptr != G_DIR_SEPARATOR) &&
			(gsize) (uptr - ubuf) < sizeof ubuf - 1;)
		{
			*uptr++ = *ptr++;
		}
		*uptr = '\0';
		if((dir = usr_lookup_uhome(uname)) != NULL)
			g_string_assign(temp, dir);
		else
			g_string_assign(temp, "");
		g_string_append(temp, ptr);
		if(*temp->str)
			g_strlcpy(component, temp->str, PATH_MAX);
		else
			g_snprintf(component, PATH_MAX, "~%s", uname);
		g_string_free(temp, TRUE);
	}
}

/* 2003-11-29 -	Interpolate special things (like $VARIABLES and ~homedirs) into a path. */
void fut_interpolate(gchar *buffer)
{
	eval_env(buffer);
	if(buffer[0] == '~')
		eval_home(buffer);
#if 0	/* FIXME: This needs to become URL-aware, in a big way. */
	else if(buffer[0] != G_DIR_SEPARATOR && base_path[0] == G_DIR_SEPARATOR)
	{
		gint	len = strlen(buffer);

		/* Make relative path absolute, if buffer space allows it. */
		if(len + base_len + 2 < PATH_MAX)
		{
			memmove(buffer + base_len + 1, buffer, len + 1);
			memcpy(buffer, base_path, base_len);
			buffer[base_len] = G_DIR_SEPARATOR;
		}
	}
#endif
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-15 -	Attempt to locate file called <name> in any of the colon-separated locations
**		specified by <paths>. When found, returns a pointer to the complete name. If
**		nothing has been found when all paths have been tried, returns a sullen NULL.
**		Very useful for loading icons.
** 1998-08-23 -	Now uses the fut_path_component() function. Feels more robust.
** 1998-12-01 -	Now uses the fut_exists() call rather than a kludgy open().
** 1999-06-12 -	Now handles absolute <name>s, too.
** BUG BUG BUG	Calls to this function DO NOT nest very well, due to the static buffer pointer
**		being returned. Beware.
*/
const gchar * fut_locate(const gchar *paths, const gchar *name)
{
	static gchar	buf[PATH_MAX];
	const gchar	*ptr = paths;
	gsize		len;

	if(*name == G_DIR_SEPARATOR)		/* Absolute name? */
	{
		if(fut_exists(name))
			return name;
		return NULL;
	}

	while(fut_path_component(ptr, buf))	/* No, iterate paths. */
	{
		len = strlen(buf);
		g_snprintf(buf + len, sizeof buf - len, "%s%s", G_DIR_SEPARATOR_S, name);
		if(fut_exists(buf))
			return buf;
		ptr = NULL;
	}
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* Just free a path. */
static gboolean kill_path(gpointer key, gpointer data, gpointer user)
{
	g_free(key);

	return TRUE;
}

/* 1998-08-23 -	Scan through a list of colon-separated paths, and return a (possibly HUGE)
**		sorted list of all _files_ found. Very useful for selecting icon pixmaps... The
**		scan is non-recursive; we don't want to go nuts here.
** 1998-09-05 -	Now actually sorts the list, too. That bit had been mysteriously forgotten.
** 1998-12-23 -	Now ignores directories already visited. Uses a string comparison rather than a
**		file-system level check, so you can still fool it. It's a bit harder, though.
*/
GList * fut_scan_path(const gchar *paths, gint (*filter)(const gchar *path, const gchar *name))
{
	gchar		buf[PATH_MAX], *item;
	const gchar	*ptr = paths;
	DIR		*dir;
	struct dirent	*de;
	GList		*list = NULL;
	GHashTable	*set;
	GString		*temp;

	if((paths == NULL) || (filter == NULL))
		return NULL;

	if((temp = g_string_new("")) == NULL)
		return NULL;

	if((set = g_hash_table_new(g_str_hash, g_str_equal)) == NULL)
	{
		g_string_free(temp, TRUE);
		return NULL;
	}

	for(ptr = paths; fut_path_component(ptr, buf); ptr = NULL)
	{
		if(g_hash_table_lookup(set, buf))
			continue;
		g_hash_table_insert(set, g_strdup(buf), GINT_TO_POINTER(TRUE));
		if((dir = opendir(buf)) != NULL)
		{
			while((de = readdir(dir)) != NULL)
			{
				if(filter(buf, de->d_name))
				{
					item = g_strdup(de->d_name);
					list = g_list_insert_sorted(list, item, (GCompareFunc) strcmp);
				}
			}
			closedir(dir);
		}
	}
	g_hash_table_foreach_remove(set, kill_path, NULL);
	g_hash_table_destroy(set);
	g_string_free(temp, TRUE);

	return list;
}

/* 1998-08-23 -	Free the list created during a previous call to fut_scan_paths(). */
void fut_free_path(GList *list)
{
	GList	*head = list;

	if(list != NULL)
	{
		for(; list != NULL; list = g_list_next(list))
		{
			if(list->data != NULL)
				g_free(list->data);		/* Free the file name. */
		}
		g_list_free(head);		/* Free all list items. */
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-07 -	Check if the user described by (<uid>,<gid>), which is the user currently running
**		gentoo, is allowed to READ from a file described by <stat>. Note that this is a
**		complex check.
** 1998-09-22 -	Moved this function (and friends) into the fileutil module.
*/
gboolean fut_can_read(const struct stat *stat, uid_t uid, gid_t gid)
{
	if(uid == 0)			/* Root rules! */
		return TRUE;

	if(stat->st_uid == uid)		/* Current user's file? */
		return (stat->st_mode & S_IRUSR) ? TRUE : FALSE;
	if(stat->st_gid == gid)		/* User belongs to file's group? */
		return (stat->st_mode & S_IRGRP) ? TRUE : FALSE;

	/* User doesn't own file or belong to file owner's group. Check others. */
	return (stat->st_mode & S_IROTH) ? TRUE : FALSE;
}

/* 1998-09-23 -	Just a convenience interface for the above. Name must be absolute, or you
**		must be very lucky (the current dir while running gentoo is a highly
**		stochastic variable).
*/
gboolean fut_can_read_named(const gchar *name)
{
	struct stat	s;

	if(stat(name, &s) == 0)
		return fut_can_read(&s, geteuid(), getegid());
	return FALSE;
}

/* 1998-09-07 -	Do complex WRITE permission check. */
gboolean fut_can_write(const struct stat *stat, uid_t uid, gid_t gid)
{
	if(uid == 0)
		return TRUE;

	if(stat->st_uid == uid)
		return (stat->st_mode & S_IWUSR) ? TRUE : FALSE;
	if(stat->st_gid == gid)
		return (stat->st_mode & S_IWGRP) ? TRUE : FALSE;

	return (stat->st_mode & S_IWOTH) ? TRUE : FALSE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-10-26 -	Check if the user doesn't want to see <name>. Returns TRUE if the file is
**		NOT hidden, FALSE if it is. If <name> is NULL, we free any allocated resources
**		and then return TRUE. This is very useful when the RE source is changing.
** 1998-12-14 -	The check is now done with respect to pane <dp>. This has relevance, since
**		hiding can now be enabled/disabled on a per-pane basis. If <name> is NULL,
**		any pane can be used (but it must not be NULL!).
*/
gboolean fut_check_hide(DirPane *dp, const gchar *name)
{
	MainInfo	*min = dp->main;
	HideInfo	*hi = &min->cfg.path.hideinfo;
	guint		cflags = REG_EXTENDED | REG_NOSUB;

	if(name == NULL)	/* The NULL-name special case is pane-independant. */
	{
		if(hi->hide_re != NULL)
		{
			g_free(hi->hide_re);
			hi->hide_re = NULL;
		}
		return TRUE;
	}
	if(!(min->cfg.dp_format[dp->index].hide_allowed))	/* Hiding disabled? */
		return TRUE;
	switch(hi->mode)
	{
		case HIDE_NONE:
			return TRUE;
		case HIDE_DOT:
			return name[0] != '.';
		case HIDE_REGEXP:
			if(hi->hide_re == NULL)			/* RE not compiled yet? */
			{
				if((hi->hide_re = g_malloc(sizeof *hi->hide_re)) != NULL)
				{
					if(hi->no_case)
						cflags |= REG_ICASE;
					if(regcomp(hi->hide_re, hi->hide_re_src, cflags) != 0)
					{
						g_free(hi->hide_re);
						hi->hide_re = NULL;
					}
				}
			}
			if(hi->hide_re != NULL)
				return regexec(hi->hide_re, name, 0, NULL, 0) == REG_NOMATCH;
			break;
	}
	return TRUE;					/* When in doubt, hide nothing. */
}
