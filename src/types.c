/*
** 1998-08-02 -	This module deals with file types. It provides services to initialize the
**		default types, add/delete/move previously created types, and even using all
**		this type information to analyze files. This is useful stuff.
** 1998-08-15 -	Mild redesign; all functions now take a "generic" GList rather than a CfgInfo.
**		This allows use of these functions on styles _not_ sitting on the global
**		CfgInfo.style list. Very handy when editing.
** 1998-08-26 -	Massive hacking to implement support for 'file' RE matching. Got a lot nicer
**		than I had first expected, actually. If used, 'file' is always envoked
**		exactly once for each dirpane. This cuts down on the overhead of 'file'
**		reading and parsing its rather hefty (~120 KB on my system) config file.
** 1998-09-15 -	Added support for case-insensitive regular expressions.
** 1998-09-16 -	Added priorities to file types, controlling the order in which they are
**		checked (and listed, of course). Priorities are in 0..254, which I really
**		think should be enough. If I'm wrong, I'll just square that number. :) 0 is
**		the highest priority (which should explain why 255 is reserved for "Unknown").
** 1998-09-18 -	Regular expressions are now handled by the POSIX code in the standard C library.
**		No longer any need for Henry Spencer's code. Feels good.
** 1998-12-13 -	Priorities removed. Types now explicitly ordered by user in config.
*/

#include "gentoo.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <stddef.h>

#include "dirpane.h"
#include "errors.h"
#include "strutil.h"
#include "styles.h"
#include "fileutil.h"
#include "types.h"

/* ----------------------------------------------------------------------------------------- */

/* Collected variables that deal with 'file' into a struct, for clarity. */
static struct
{
	gboolean	file_used;		/* Any types using 'file' active? */
	GSList		*files;			/* Current list of files to inspect. */
	gboolean	sigpipe_installed;	/* SIGPIPE handler installed? */
	volatile gint	sigpipe_occured;	/* Did we just catch SIGPIPE? */
} file_info;

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-02 -	Create a new type, with the given <name> and identification strings. Use
**		NULL for an identifier that should not be used. Returns a pointer to a new
**		FType structure, or NULL on failure.
** 1998-08-11 -	Now takes the name of a <style> to apply to files of this type, too.
** 1998-09-07 -	Added another argument, for the new permissions support. That's eight
**		arguments; pretty close to my personal limit. :)
** 1999-05-29 -	Removed the <style> argument again, since it was too complex. Use typ_type_set_style().
*/
FType * typ_type_new(CfgInfo *cfg, const gchar *name, mode_t mode, gint perm, const gchar *suffix, const gchar *name_re, const gchar *file_re)
{
	FType	*type;

	if((type = g_malloc(sizeof *type)) != NULL)
	{
		g_strlcpy(type->name, name, sizeof type->name);
		type->mode  = mode;
		type->perm  = perm;
		type->flags = 0UL;
		type->suffix[0] = '\0';
		type->name_re_src[0] = '\0';
		type->name_re = NULL;
		type->file_re_src[0] = '\0';
		type->file_re = NULL;
		if(perm != 0)
			type->flags |= FTFL_REQPERM;

		if(suffix != NULL)
		{
			g_strlcpy(type->suffix, suffix, sizeof type->suffix);
			type->flags |= FTFL_REQSUFFIX;
		}
		if(name_re != NULL)
		{
			g_strlcpy(type->name_re_src, name_re, sizeof type->name_re_src);
			type->flags |= FTFL_NAMEMATCH;
		}
		if(file_re != NULL)
		{
			g_strlcpy(type->file_re_src, file_re, sizeof type->file_re_src);
			type->flags |= FTFL_FILEMATCH;
		}
		type->style = NULL;
	}
	return type;
}

/* 1998-08-14 -	Create a copy of the <old> type. Has the side-effect of clearing all compiled
**		regular expressions in the original (and the copy).
*/
FType * typ_type_copy(FType *old)
{
	FType	*nt;

	if((nt = g_malloc(sizeof *nt)) != NULL)
	{
		if(old->name_re != NULL)
		{
			regfree(old->name_re);
			g_free(old->name_re);
			old->name_re = NULL;
		}
		if(old->file_re != NULL)
		{
			regfree(old->file_re);
			g_free(old->file_re);
			old->file_re = NULL;
		}
		*nt = *old;
	}
	return nt;
}

void typ_type_destroy(FType *type)
{
	if(type->name_re != NULL)
	{
		regfree(type->name_re);
		g_free(type->name_re);
	}
	if(type->file_re != NULL)
	{
		regfree(type->file_re);
		g_free(type->file_re);
	}
	g_free(type);
}

/* ----------------------------------------------------------------------------------------- */

/* 2009-03-13 -	Return the Unknown type. */
FType * typ_type_get_unknown(const CfgInfo *cfg)
{
	const GList	*iter;

	for(iter = cfg->type; iter != NULL; iter = g_list_next(iter))
	{
		if(TYP_IS_UNKNOWN((const FType *) iter->data))
			return iter->data;
	}
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-07 -	Check if the file described by <line> matches the permissions requirements
**		in <type>. Returns 1 if so, otherwise 0.
** 1999-03-14 -	Made the access to the DirRow (previously DirLine) a lot more abstract.
** 2010-03-27 -	It's been a while, I guess. Porting to GIO and DirRow2.
*/
static gint check_perm(const FType *type, GtkTreeModel *model, const DirRow2 *row)
{
	mode_t	mode = 0;

	/* First build mode mask, for SetUID, SetGID and sticky. */
	if(type->perm & FTPM_SETUID)
		mode |= S_ISUID;
	if(type->perm & FTPM_SETGID)
		mode |= S_ISGID;
	if(type->perm & FTPM_STICKY)
		mode |= S_ISVTX;

	/* Now we know the mode requirements - check if fulfilled. */
	if((mode != 0) && ((dp_row_get_mode(model, row) & mode) != mode))
		return 0;

	if(type->perm & FTPM_READ)
	{
		if(!dp_row_get_can_read(model, row))
			return 0;
	}
	if(type->perm & FTPM_WRITE)
	{
		if(!dp_row_get_can_write(model, row))
			return 0;
	}
	if(type->perm & FTPM_EXECUTE)
	{
		if(!dp_row_get_can_execute(model, row))
			return 0;
	}
	return 1;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-15 -	Check the RE in <re>, which has source <re_src>, against <string>.
**		Returns 1 on match, 0 on failure.
*/
static gint check_re(const gchar *re_src, regex_t **re, gboolean glob, gboolean nocase, const gchar *string)
{
	gchar	*glob_re = NULL;

	if(*re == NULL)					/* RE not compiled? */
	{
		if(glob)
		{
			glob_re = stu_glob_to_re(re_src);
			re_src = glob_re;
		}
		*re = g_malloc(sizeof **re);
		regcomp(*re, re_src, REG_EXTENDED | REG_NOSUB | (nocase ? REG_ICASE : 0));
		if(glob_re)
			g_free(glob_re);			/* Free the globbed version. */
	}
	if(*re != NULL)
		return regexec(*re, string, 0, NULL, 0) == 0;
	return 0;
}

static gboolean check_type(DirPane *dp, const DirRow2 *row, FType *type, const gchar *fout)
{
	gint		tries = 0, hits = 0;
	const gchar	*name;
	GFileType	tt;
	GtkTreeModel	*model;

	/* Catch-all: all files match the unknown type. */
	if(TYP_IS_UNKNOWN(type))
		return TRUE;

	/* FIXME: Translate type's legacy type-information into simple GFileType value. */
	if(S_ISREG(type->mode))
		tt = G_FILE_TYPE_REGULAR;
	else if(S_ISDIR(type->mode))
		tt = G_FILE_TYPE_DIRECTORY;
	else if(S_ISLNK(type->mode))
		tt = G_FILE_TYPE_SYMBOLIC_LINK;
	else if(S_ISFIFO(type->mode) || S_ISSOCK(type->mode))
		tt = G_FILE_TYPE_SPECIAL;
	else
		tt = G_FILE_TYPE_UNKNOWN;

	model = dp_get_tree_model(dp);

	/* Apply the type test first, since it's the fastest. */
	if(tt != dp_row_get_file_type(model, row, TRUE))
		return FALSE;
	name = dp_row_get_name(model, row);
	/* Now do the rest, and count the number of matches. */
	if(type->flags & FTFL_REQPERM)
	{
		tries++;
		hits += check_perm(type, model, row);
	}
	if(type->flags & FTFL_REQSUFFIX)
	{
		tries++;
		hits += g_str_has_suffix(name, type->suffix);
	}
	if(type->flags & FTFL_NAMEMATCH)
	{
		tries++;
		hits += check_re(type->name_re_src, &type->name_re, type->flags & FTFL_NAMEGLOB, type->flags & FTFL_NAMENOCASE, name);
	}
	if(type->flags & FTFL_FILEMATCH)
	{
		tries++;
		if(fout != NULL)
			hits += check_re(type->file_re_src, &type->file_re, type->flags & FTFL_FILEGLOB, type->flags & FTFL_FILENOCASE, fout);
	}
	return (tries == hits);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-02 -	Initialize the file typing subsystem with some simple default types.
** 1998-09-02 -	Painlessly diked out all but "Unknown" and "Directory", since I think these
**		two are the only ones that are going to be built-in.
*/
void typ_init(CfgInfo *cfg)
{
	FType	*type;

	cfg->type = NULL;
	if((type = typ_type_new(cfg, _("Unknown"), 0, 0, NULL, NULL, NULL)) != NULL)
	{
		cfg->type = typ_type_insert(cfg->type, NULL, type);
		cfg->type = typ_type_set_style(cfg->type, type, cfg->style, NULL);
	}
	if((type = typ_type_new(cfg, _("Directory"), S_IFDIR, 0, NULL, NULL, NULL)) != NULL)
	{
		cfg->type = typ_type_insert(cfg->type, NULL, type);
		cfg->type = typ_type_set_style(cfg->type, type, cfg->style, _("Directory"));
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-12-14 -	Rewritten another time. Now never inserts anything after the "Unknown" type.
**		Pretty lazily implemented, but so?
** 1999-01-09 -	Rewritten again. Now takes an additional <after> argument, and inserts <typ>
**		right after it. If <after> is the "Unknown", we insert before it. If it's
**		NULL, the same thing happens.
*/
GList * typ_type_insert(GList *list, FType *after, FType *type)
{
	gint	li;

	if(after == NULL || TYP_IS_UNKNOWN(after))		/* No reference element given, or "Unknown" ref? */
	{
		GList	*last;

		if((last = g_list_last(list)) != NULL)
		{
			if(TYP_IS_UNKNOWN((FType *) last->data))
			{
				li = g_list_index(list, last->data);
				return g_list_insert(list, type, li);
			}
		}
		return g_list_append(list, type);
	}
	li = g_list_index(list, after);
	return g_list_insert(list, type, li + 1);
}

/* 1998-12-14 -	Remove a type from the list, and return the new list. Not strictly
**		necessary (only called at one place), but makes me feel good. :^)
*/
GList * typ_type_remove(GList *list, FType *type)
{
	if(list == NULL || type == NULL)
		return NULL;

	if(TYP_IS_UNKNOWN(type))		/* Can't remove the "Unknown" type. */
		return list;

	list = g_list_remove(list, type);
	typ_type_destroy(type);

	return list;
}

/* 2002-03-17 -	Do a globbed lookup of a named type. Returns a list of matching FTypes, which
**		will be a sublist of the grand input <list> of all types. The list, of course,
**		will need to be freed when you're done. The data in it, however, will not.
*/
GList * typ_type_lookup_glob(const GList *list, const gchar *glob)
{
	GList		*ret = NULL;
	gchar		*re_src;
	regex_t		re;

	re_src = stu_glob_to_re(glob);
	if(regcomp(&re, re_src, REG_EXTENDED | REG_NOSUB | REG_ICASE) == 0)
	{
		for(; list != NULL; list = g_list_next(list))
		{
			if(regexec(&re, ((FType *) list->data)->name, 0, NULL, 0) == 0)
				ret = g_list_append(ret, list->data);
		}
		regfree(&re);
	}
	g_free(re_src);
	return ret;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-17 -	Change the name of given type. Since the name is used as a fall-back
**		when sorting types of equal priority, this calls for a resort.
** 1998-12-13 -	Removing the priorities also removed the sorting dependency on names,
**		so this became a lot simpler. I could remove the entire function,
**		but I'll keep it. You never know...
*/
GList * typ_type_set_name(GList *list, FType *type, const gchar *name)
{
	g_strlcpy(type->name, name, sizeof type->name);
	return list;
}

/* 1999-05-29 -	Set the 'style' field of <type> to the style whose name is <name>. */
GList * typ_type_set_style(GList *list, FType *type, StyleInfo *si, const gchar *name)
{
	if((type != NULL) && (si != NULL))
		type->style = stl_styleinfo_style_find(si, name);
	return list;
}

/* 1998-12-13 -	Move given <type> either up (<delta> == -1) or down (1). Returns
**		new version of <list>. Other <delta> values are illegal.
*/
GList * typ_type_move(GList *list, FType *type, gint delta)
{
	gint	pos, np;

	if(delta != -1 && delta != 1)
		return list;

	pos  = g_list_index(list,  type);
	list = g_list_remove(list, type);
	np   = pos + delta;
	if(np < 0)
		np = 0;
	else if(np > (gint) g_list_length(list) - 1)
		np = (gint) g_list_length(list) - 1;

	return g_list_insert(list, type, np);
}

/* ----------------------------------------------------------------------------------------- */

void typ_identify_begin(DirPane *dp)
{
	GList	*here;

	if(file_info.files != NULL)
		g_warning("**TYPES: Attempted to nest calls to typ_identify_begin()!");

	for(here = dp->main->cfg.type; here != NULL; here = g_list_next(here))
	{
		if(((FType *) here->data)->flags & FTFL_FILEMATCH)
			break;
	}
	file_info.file_used = (here != NULL);
}

static FType * identify(DirPane *dp, const DirRow2 *row)
{
	const GList	*here;

	for(here = dp->main->cfg.type; here != NULL; here = g_list_next(here))
	{
		if(check_type(dp, row, here->data, NULL))
			return here->data;
	}
	return NULL;
}

void typ_identify(DirPane *dp, const DirRow2 *row)
{
	FType	*ft;

	ft = identify(dp, row);
	if(ft != NULL)
		dp_row_set_ftype(dp_get_tree_model(dp), row, ft);
	if(TYP_IS_UNKNOWN(ft) && file_info.file_used)
	{
		DirRow2	*sr;

		sr = g_slice_alloc(sizeof *sr);
		memcpy(sr, row, sizeof *sr);
		file_info.files = g_slist_prepend(file_info.files, sr);
	}
}

/* 2010-03-13 -	Attempt to match all types in list against <line>, knowing that 'file' said
**		<fout>. Only types that include a 'file'-matching RE are checked, of course.
**		Returns the matching type if any, or NULL if there's no match.
*/
static FType * match_file(DirPane *dp, GList *list, DirRow2 *row, const gchar *fout)
{
	FType	*type;

	for(; (list != NULL) && (type = (FType *) list->data); list = g_list_next(list))
	{
		if(check_type(dp, row, list->data, fout))
			return type;
	}
	return NULL;
}

/* 2009-03-11 -	Trivial SIGPIPE handler, that just sets a global flag. */
static void sigpipe_handler(int sig)
{
	if(sig != SIGPIPE)
		return;
	g_atomic_int_set(&file_info.sigpipe_occured, 1);
}

/* 2009-03-11 -	Installs a signal handler for SIGPIPE, so we can catch broken 'file' runs. */
static gboolean install_handler(void)
{
	struct sigaction	act;

	if(file_info.sigpipe_installed)
		return TRUE;

	act.sa_handler = sigpipe_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags	= 0;

	return file_info.sigpipe_installed = (sigaction(SIGPIPE, &act, NULL) == 0);
}

gboolean typ_identify_end(DirPane *dp)
{
	gchar		*argv[] = { "file", "-n", "-f", "-", NULL }, *path;
	GPid		file_pid;
	gint		file_stdin, file_stdout;
	GError		*error = NULL;
	GtkTreeModel	*tm = dp_get_tree_model(dp);
	GSList		*here;
	gboolean	ret = TRUE;

	if(file_info.files == NULL || !dp->dir.is_local)
		return TRUE;
	if((path = g_file_get_path(dp->dir.root)) == NULL)
		return TRUE;

	install_handler();

	if(g_spawn_async_with_pipes(path, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
				    NULL, NULL, &file_pid, &file_stdin, &file_stdout, NULL, &error))
	{
		FILE	*in;

		if((in = fdopen(file_stdout, "rt")) != NULL)
		{
			for(here = file_info.files; here != NULL; here = g_slist_next(here))
			{
				gchar	buf[URI_MAX + 256], *path;
				size_t	len, to_go;
				ssize_t	wrote;

				/* Handle missing path by skipping, just like we do below. */
				if((path = g_file_get_path(dp_get_file_from_row(dp, here->data))) == NULL)
					continue;
				len = g_snprintf(buf, sizeof buf, "%s\n", path);
				g_free(path);
				if(len >= sizeof buf)	/* Handle overflow by just skipping that file. */
					continue;
				g_atomic_int_set(&file_info.sigpipe_occured, 0);
				for(to_go = len; to_go > 0; to_go -= wrote)
				{
					wrote = write(file_stdin, buf + (len - to_go), to_go);
					if(wrote < 0)
					{
						if(errno == EPIPE)
							g_atomic_int_set(&file_info.sigpipe_occured, 1);
						break;
					}
				}
				if(g_atomic_int_get(&file_info.sigpipe_occured))
					break;
				if(fgets(buf, sizeof buf, in) != NULL)
				{
					const char	*fout;
					char		*lf;
					FType		*type;

					if((fout = strrchr(buf, ':')) == NULL)
						continue;
					for(fout++; isspace(*fout); fout++)
						;
					if((lf = strchr(fout + 1, '\n')) != NULL)
						*lf = '\0';
					if((type = match_file(dp, dp->main->cfg.type, here->data, fout)) != NULL)
						dp_row_set_ftype(tm, here->data, type);
				}
			}
			fclose(in);
		}
		close(file_stdin);
		close(file_stdout);
		g_spawn_close_pid(file_pid);
		if(g_atomic_int_get(&file_info.sigpipe_occured))
		{
			/* FIXME: This is problematic; the nicely formatted text is often lost due to free space printing. */
			err_printf(dp->main, _("Got SIGPIPE when writing to 'file' process (%s), it seems to have terminated prematurely."), argv[0]);
			ret = FALSE;
		}
	}
	else
	{
		g_prefix_error(&error, _("Unable to run spawn 'file' command: "));
		err_printf(dp->main, "%s", error->message);
		g_error_free(error);
		ret = FALSE;
	}
	/* Walk list once more, freeing all data. */
	for(here = file_info.files; here != NULL; here = g_slist_next(here))
		g_slice_free1(sizeof (DirRow2), here->data);
	g_slist_free(file_info.files);
	file_info.files = NULL;
	g_free(path);

	return ret;
}
