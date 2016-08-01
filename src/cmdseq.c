/*
** 1998-09-25 -	Here's a complete rewrite of the command subsystem, with a far more
**		flexible and fun architecture behind it. Some parts will undboubtely
**		be copied from the old code, but much is brand new. Fun.
** 1998-10-21 -	Moved the huge native command initialization function in here.
** 1999-04-04 -	Completely redid handling of builtin ("native") commands. Now accomodates
**		support for command-specific configuration data, and stuff.
** 1999-06-20 -	Adapted for new dialog module.
*/

#include "gentoo.h"

#include <ctype.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "commands.h"
#include "errors.h"
#include "strutil.h"
#include "dirpane.h"
#include "dialog.h"
#include "fileutil.h"

#include "children.h"
#include "cmdparse.h"
#include "cmdgrab.h"
#include "cmdarg.h"
#include "cmdseq.h"

/* ----------------------------------------------------------------------------------------- */

static const gchar	*row_type_name[] = { "Built-In", "External" };

/* This describes a single built-in command. The 'cmd' field is the actual command execution
** function, like cmd_mkdir or cmd_activateother. The 'cfg' field is the corresponding commands's
** configuration function, it is called to establish the connection between the command's
** internal configuration data and the cmdseq_config module which handles loading / saving.
** Most built-in commands don't have any config data; they will have a NULL 'cfg' pointer.
*/
typedef struct {
	Command		cmd;
	CommandCfg	cfg;	/* Another function pointer. */
} CmdDesc;

/* ----------------------------------------------------------------------------------------- */

static void csq_add_builtin(MainInfo *min, const gchar *name, Command cmd, CommandCfg cmd_cfg)
{
	CfgInfo	*cfg = &min->cfg;

	if(cfg->commands.builtin == NULL)
		cfg->commands.builtin = g_hash_table_new(g_str_hash, g_str_equal);
	if(cfg->commands.builtin != NULL)
	{
		CmdDesc	*cdesc;

		if((cdesc = g_slice_alloc(sizeof *cdesc)) != NULL)
		{
			cdesc->cmd = cmd;
			cdesc->cfg = cmd_cfg;
			g_hash_table_insert(cfg->commands.builtin, (gpointer) name, cdesc);
			if(cmd_cfg != NULL)
				cmd_cfg(min);
		}
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-26 -	Copy characters one by one from <src> to <dst>, while doing some special
**		operations to make the result "more suitable" as a command sequence name.
**		These operations include stripping out whitespace and almost all other
**		non-alphanumerical letters.
*/
static void csq_cmdseq_filter_name(gchar *dst, const gchar *src)
{
	gchar	*od = dst;

	if((dst != NULL) && (src != NULL))
	{
		for(; *src != '\0' && dst - od < CSQ_NAME_SIZE - 1; src++)
		{
			if(isalnum((guchar) *src) || *src == '_')
				*dst++ = *src;
		}
		*dst = '\0';
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-27 -	A callback for g_hash_table; find maximum X in all UnnamedX-keys. OK? */
static void check_name(gpointer key, gpointer data, gpointer user)
{
	gint	here, *iptr = user;

	if((strcmp(key, "Unnamed") == 0) && iptr != NULL && *iptr == -1)
		*iptr = 0;
	else if(sscanf(key, "Unnamed_%d", &here) == 1)
	{
		if(iptr != NULL && here > *iptr)
			*iptr = here;
	}
}

/* 1998-09-27 -	Construct (and return a pointer to) a somewhat neutral name, with the
**		interesting property that it is unique among all keys of <hash>.
*/
const gchar * csq_cmdseq_unique_name(GHashTable *hash)
{
	static gchar	buf[CSQ_NAME_SIZE];
	gint		index = -1;

	if(hash != NULL)
	{
		g_hash_table_foreach(hash, check_name, &index);
		if(index == -1)
			return "Unnamed";
		g_snprintf(buf, sizeof buf, "Unnamed_%d", index + 1);
		return buf;
	}
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-22 -	Emit a warning message, and return <to>. */
static const gchar * map_name(const gchar *from, const gchar *to, const gchar *context)
{
	fprintf(stderr, "**Notice: Reference to obsolete command '%s' replaced by '%s' in %s\n", from, to, context);

	return to;
}

/* 1999-05-22 -	Map a command sequence name, if necessary. This should be called by all code
**		that loads cmdseq names, from config files and stuff. It should not be used
**		on input directly from the user, since that might be very annoying. :) The
**		point of the function is to create a central location where old obsolete
**		command names can be translated into their modern equivalents.
**		  This function is not written for speed.
*/
const gchar * csq_cmdseq_map_name(const gchar *name, const gchar *context)
{
	/* Ignore NULL and names beginning with a lower case letter (user-defined). */
	if((name == NULL) || (islower((guchar) *name)))
		return name;

	if(strcmp(name, "FileDefault") == 0)
		return map_name(name, "FileAction", context);
	else if(strcmp(name, "FileView") == 0)
		return map_name(name, "FileAction action=View", context);
	else if(strcmp(name, "FileEdit") == 0)
		return map_name(name, "FileAction action=Edit", context);
	else if(strcmp(name, "FilePrint") == 0)
		return map_name(name, "FileAction action=Print", context);
	else if(strcmp(name, "FilePlay") == 0)
		return map_name(name, "FileAction action=Play", context);
	else if(strcmp(name, "ViewTextHex") == 0)
		return map_name(name, "ViewText mode=Hex", context);
	else if(strcmp(name, "ViewTextOrHex") == 0)
		return map_name(name, "ViewText mode=Auto", context);
	else if(strcmp(name, "DirPrevious") == 0)				/* Gone in 0.11.8. */
		return map_name(name, "DirBackward", context);

	return name;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-25 -	Create a new command sequence of the given name. The sequence is created
**		empty, i.e. with no rows attached.
*/
CmdSeq * csq_cmdseq_new(const gchar *name, guint32 flags)
{
	CmdSeq	*cs;

	if((cs = g_malloc(sizeof *cs)) != NULL)
	{
		g_strlcpy(cs->name, name, sizeof cs->name);
		cs->flags = flags;
		cs->rows  = NULL;
	}
	return cs;
}

/* 1998-09-26 -	Create a "deep" (non-memory-sharing) copy of <src>, and return it. */
CmdSeq * csq_cmdseq_copy(const CmdSeq *src)
{
	CmdSeq	*dst = NULL;
	CmdRow	*row;
	GList	*iter;

	if((dst = g_malloc(sizeof *dst)) != NULL)
	{
		g_strlcpy(dst->name, src->name, sizeof dst->name);
		dst->flags = src->flags;
		dst->rows = NULL;
		for(iter = src->rows; iter != NULL; iter = g_list_next(iter))
		{
			if((row = csq_cmdrow_copy(iter->data)) != NULL)
				dst->rows = g_list_append(dst->rows, row);
			else
			{
				csq_cmdseq_destroy(dst);
				return NULL;
			}
		}
	}
	return dst;
}

/* 1998-09-25 -	Set the name of a command sequence. Takes care to filter it first. This
**		is somewhat more complex than it might seem, since it needs to rehash
**		the sequence.
*/
void csq_cmdseq_set_name(GHashTable *hash, CmdSeq *cs, const gchar *name)
{
	if(hash != NULL && cs != NULL && name != NULL)
	{
		g_hash_table_remove(hash, cs->name);
		csq_cmdseq_filter_name(cs->name, name);
		if(cs->name[0] == '\0' || g_hash_table_lookup(hash, cs->name) != NULL)
			g_strlcpy(cs->name, csq_cmdseq_unique_name(hash), sizeof cs->name);
		csq_cmdseq_hash(&hash, cs);
	}
}

/* 1998-09-25 -	Insert given command <cs> into hash table at <*hash>. The hash table is
**		created if it doesn't already exist.
*/
void csq_cmdseq_hash(GHashTable **hash, CmdSeq *cs)
{
	if(hash != NULL)
	{
		if(*hash == NULL)
			*hash = g_hash_table_new(g_str_hash, g_str_equal);
		if(*hash != NULL)
			g_hash_table_insert(*hash, cs->name, cs);
	}
}

/* 1998-09-25 -	Just a callback for g_list_foreach(). */
static void row_destroy(gpointer d, gpointer u)
{
	csq_cmdrow_destroy((CmdRow *) d);
}

/* 1998-09-25 -	Destroy a command sequence, freing all memory used by it (that includes
**		the memory occupied by the rows, of course.
*/
void csq_cmdseq_destroy(CmdSeq *cs)
{
	if(cs != NULL)
	{
		g_list_foreach(cs->rows, row_destroy, NULL);
		g_free(cs);
	}
}

/* 1998-09-25 -	Append a row to the given command sequence. Returns the index of the
**		newly appended row (i.e. the length of the list minus one).
*/
gint csq_cmdseq_row_append(CmdSeq *cs, CmdRow *row)
{
	if(cs != NULL && row != NULL)
	{
		cs->rows = g_list_append(cs->rows, row);
		return g_list_length(cs->rows) - 1;
	}
	return -1;
}

/* 2009-03-20 -	Reorder the rows of the given sequence, to match the given new list.
**		The new list is assumed to re-use the actual CmdRow pointers that are
**		in our current list. All we need to do is free the GList part of our
**		old list, and then replace the list with the new, taking ownership.
*/
void csq_cmdseq_rows_relink(CmdSeq *cs, GList *rows)
{
	if(cs == NULL || rows == NULL)
		return;
	g_list_free(cs->rows);	/* This doesn't touch the data, glib doesn't know how. */
	cs->rows = rows;
}

/* 1998-09-25 -	Remove given row from command sequence. Returns the index of the
**		item following the deleted one (or the one before if deleting the
**		last).
*/
gint csq_cmdseq_row_delete(CmdSeq *cs, CmdRow *row)
{
	gint	pos;

	if(cs != NULL && row != NULL)
	{
		pos = g_list_index(cs->rows, row);
		cs->rows = g_list_remove(cs->rows, row);
		if(pos >= (gint) g_list_length(cs->rows))
			return pos - 1;
		return pos;
	}
	return -1;
}

/* 2009-03-18 -	Clears the command sequence of all rows, freeing them while doing so. */
void csq_cmdseq_row_delete_all(CmdSeq *cs)
{
	GList	*iter;

	if(cs == NULL)
		return;

	/* Don't disassemble the list link by link. Walk through and destroy the rows,
	** then free the entire list in one ... you-know-what. With a swoop.
	*/
	for(iter = cs->rows; iter != NULL; iter = g_list_next(iter))
		csq_cmdrow_destroy(iter->data);
	g_list_free(cs->rows);
	cs->rows = NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-25 -	Execute a built-in command. Currently very simple.
** 1999-05-06 -	Rewritten, now handles command arguments. Very nice.
*/
static gint execute_builtin(MainInfo *min, const gchar *cmd)
{
	gchar	msg[CSQ_NAME_SIZE + 64];
	gchar	**argv;
	CmdDesc	*cdesc;

	if(strchr(cmd, ' ') != NULL)		/* Does command string contain space, and thus arguments? */
	{
		if((argv = cpr_parse(min, cmd)) != NULL)
		{
			if((cdesc = g_hash_table_lookup(min->cfg.commands.builtin, argv[0])) != NULL)
			{
				CmdArg	*ca;
				DirPane	*src = min->gui->cur_pane;
				gint	ret;

				ca  = car_create(argv);
				ret = cdesc->cmd(min, src, dp_mirror(min, src), ca);
				car_destroy(ca);
				g_free(argv);	/* Don't forget to free the parsed arguments. */
				return ret;
			}
			g_snprintf(msg, sizeof msg, _("Unable to execute unknown\ncommand \"%s\"."), cmd);
			dlg_dialog_async_new_error(msg);
			g_free(argv);
		}
		return 1;
	}
	else
	{
		if((cdesc = g_hash_table_lookup(min->cfg.commands.builtin, cmd)) != NULL)
		{
			DirPane	*src = min->gui->cur_pane;

			return cdesc->cmd(min, src, dp_mirror(min, src), NULL);
		}
	}
	g_snprintf(msg, sizeof msg, _("Unable to execute unknown\ncommand \"%s\"."), cmd);
	dlg_dialog_async_new_error(msg);

	return 0;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-26 -	Handle a set of before or after flags. */
gboolean csq_handle_ba_flags(MainInfo *min, guint32 flags)
{
	DirPane	*src = min->gui->cur_pane, *dst = dp_mirror(min, src);

	if(flags & CBAF_REQSEL_SOURCE)
	{
		if(!dp_has_selection(src))
			return FALSE;
	}
	if(flags & CBAF_REQSEL_DEST)
	{
		if(!dp_has_selection(dst))
			return FALSE;
	}

	if(flags & CBAF_RESCAN_SOURCE)
		dp_rescan_post_cmd(src);
	if(flags & CBAF_RESCAN_DEST)
		dp_rescan_post_cmd(dst);

	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-05-27 -	Do the actual fork() call, and execution of external command. Put in a separate routine
**		to make handling of pre-command dialogues easier (GTK+ - the library that calls back).
** 1998-05-28 -	Major bug fix: when waiting for a synchronous child, we really DO want to hang (so, no WNOHANG).
** 1998-05-30 -	Fixed up the error-checking after waitpid(), which was completely broken. If command fails
**		(i.e. returns non-zero exit status), we now detect that and inform the user. In a perfect
**		world, we would like to have access to the child's 'errno' at its time of failure... Also
**		shut down the child's file descriptors, to avoid it flooding the original shell with output.
**		This is a temporary solution; I would like the stdin/out/err behaviour to be configurable.
** 1998-07-04 -	Um... It seems that I'm not exactly king at programming with processes and stuff, since the
**		exit status check was completely broken. I blame the man-page for waitpid(2), which uses
**		language not compatible with my brain. :) It seems WIFEXITED() returns non-zero (meaning the
**		child exited normally), even if the child failed. I mis-parsed that one.
** 1998-09-10 -	Massively extended and rewritten. Now supports output capturing, and always runs stuff asynch.
** 1998-09-11 -	Detected a race condition here; if the child terminates before we register its pid, there's
**		a chance that the SIGCHLD signal handler hasn't got the data it needs to properly handle
**		the child's death. To fix this, we generate a SIGCHLD when we know that the data is there.
** 1998-09-25 -	Moved into the new cmdseq module, and adapted accordingly.
** 1999-05-29 -	Added pre-fork() protection through new call in fileutil module. Very nice.
*/
static gint fork_and_execute(MainInfo *min, CmdRow *row, gchar **argv)
{
	GPid		child;
	gchar		*working_directory = NULL;
	gboolean	capture, ok;
	gint		fd_out, fd_err, *fd_out_ptr = NULL, *fd_err_ptr = NULL;
	CX_Ext		*ext = &row->extra.external;
	GError		*err = NULL;

	if(ext->gflags & CGF_KILLPREV)
		chd_kill_child(argv[0]);

	/* Figure out where to CD, if requested and sensible. */
	if(ext->baflags[0] & CBAF_CD_SOURCE)
	{
		if(g_file_is_native(min->gui->cur_pane->dir.root))
			working_directory = g_file_get_path(min->gui->cur_pane->dir.root);
	}
	else if(ext->baflags[0] & CBAF_CD_DEST)
	{
		GFile	*droot = min->gui->pane[1 - min->gui->cur_pane->index].dir.root;
		if(g_file_is_native(droot))
			working_directory = g_file_get_path(droot);
	}

	capture = (row->type == CRTP_EXTERNAL && row->extra.external.gflags & CGF_GRABOUTPUT);
	if(capture)
	{
		fd_out_ptr = &fd_out;
		fd_err_ptr = &fd_err;
	}
	if((ok = g_spawn_async_with_pipes(working_directory, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH, NULL, NULL, &child, NULL, fd_out_ptr, fd_err_ptr, &err)))
	{
		chd_register(argv[0], child, ext->gflags, ext->baflags[1]);
		if(capture)
			cgr_grab_output(min, argv[0], child, fd_out, fd_err);
	}
	if(working_directory != NULL)
		g_free(working_directory);
	if(!ok)
	{
		if(err)
			err_set_gerror(min, &err, argv[0], NULL);
		else
			err_set(min, -1, argv[0], NULL);
	}
	return ok;
}

/* 1998-09-25 -	Execute an external command. */
static gint execute_external(MainInfo *min, CmdRow *row)
{
	gchar	**argv;
	gint	ok = 0;

	if(!csq_handle_ba_flags(min, row->extra.external.baflags[0]))
		return 0;
	if((argv = cpr_parse(min, row->def->str)) != NULL)
	{
		ok = fork_and_execute(min, row, argv);
		cpr_free(argv);
	}
	return ok;
}

/* 1998-09-25 -	Execute a single row, returning success or failure. */
static gint execute_row(MainInfo *min, CmdRow *row)
{
	switch(row->type)
	{
		case CRTP_BUILTIN:
			return execute_builtin(min, row->def->str);
		case CRTP_EXTERNAL:
			return execute_external(min, row);
		default:
			fprintf(stderr, "**CMDSEQ: Unknown type for row!\n");
	}
	return 1;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-25 -	Figure out whether running <row> results in a block (i.e. no further rows are
**		executed until <row> has completed, or not.
*/
static gint row_is_blocking(MainInfo *min, CmdRow *row)
{
	switch(row->type)
	{
		case CRTP_BUILTIN:		/* Built-ins never block. */
			return 0;
		case CRTP_EXTERNAL:		/* Externals may or may not block. */
			return (row->extra.external.gflags & CGF_RUNINBG) ? 0 : 1;
		default:
			;
	}
	return 0;
}

/* 1998-09-25 -	Execute a command sequence. This, of course, is sort of the most important
**		functionality in this module, the core that makes the rest worth having.
*/
static gint cmdseq_execute(MainInfo *min, CmdSeq *cs)
{
	GList	*iter;
	gint	ok = 0, block, i;

	if(cs == NULL)
		return 0;

	err_clear(min);
	for(iter = cs->rows, i = 0; iter != NULL; iter = g_list_next(iter), i++)
	{
		block = row_is_blocking(min, iter->data);
		if((ok = execute_row(min, iter->data)) != 0)
		{
			if(block)
			{
				chd_set_running(cs, i + 1);
				break;
			}
		}
		else
			break;
	}
	return ok;
}

/* 1998-09-25 -	Execute a command. The <name> parameter is somewhat magic: if it is the name of
**		an existing sequence (multi-row aggregate command), that sequence is executed.
**		Otherwise, if the name is that of a built-in command, the built-in is run!
**		This duality provides a nice, clean way of running a known built-in without the
**		overhead of a named sequence just to contain that single built-in.
** 1998-10-04 -	Now ignores empty (and NULL) command names.
*/
gint csq_execute(MainInfo *min, const gchar *name)
{
	CmdSeq	*cs;
	gint	ok;

	if((name == NULL) || (*name == '\0'))
		return 0;

	if((min->cfg.commands.cmdseq != NULL) && ((cs = g_hash_table_lookup(min->cfg.commands.cmdseq, name)) != NULL))
		ok = cmdseq_execute(min, cs);
	else
		ok = execute_builtin(min, name);
	if(!ok)
		err_show(min);

	return ok;
}

/* 1999-06-06 -	Build a command from a printf()-style format string and suitable arguments, then
**		execute the command. Very handy.
*/
gint csq_execute_format(MainInfo *min, const gchar *fmt, ...)
{
	gchar	buf[4 * PATH_MAX];	/* Hm, should perhaps be dynamic. */
	va_list	va;

	va_start(va, fmt);
	g_vsnprintf(buf, sizeof buf, fmt, va);
	va_end(va);

	return csq_execute(min, buf);
}

/* 1998-09-25 -	Continue execution of a command.
** 1999-03-08 -	Added support for the repeat flag.
*/
void csq_continue(MainInfo *min)
{
	CmdSeq	*cs;
	CmdRow	*row;
	GList	*iter = NULL;
	guint	index;

	if((cs = chd_get_running(&index)) != NULL)
	{
		for(iter = g_list_nth(cs->rows, index); iter != NULL; iter = g_list_next(iter))
		{
			chd_set_running(cs, ++index);
			row = iter->data;
			if(row_is_blocking(min, row))
			{
				if(execute_row(min, row))
					return;
				else
					break;
			}
			if(!execute_row(min, row))
				break;
		}
	}
	chd_clear_running();

	/* Determine if the sequence should repeat. */
	if((cs != NULL) && (iter == NULL) && (cs->flags & CSFLG_REPEAT) && dp_has_selection(min->gui->cur_pane))
		csq_execute(min, cs->name);

	min->gui->pane[0].dbclk_row = min->gui->pane[1].dbclk_row = -1;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-27 -	Convert a command row type identifier to a string. */
const gchar * csq_cmdrow_type_to_string(CRType type)
{
	if((type >= CRTP_BUILTIN) && (type < CRTP_NUM_TYPES))
		return row_type_name[type];
	return NULL;
}

/* 1998-09-27 -	Convert a string to a type identifier, if possible. */
CRType csq_cmdrow_string_to_type(const gchar *type)
{
	guint	i;

	for(i = 0; i < sizeof row_type_name / sizeof row_type_name[0]; i++)
	{
		if(strcmp(row_type_name[i], type) == 0)
			return (CRType) i;
	}
	return -1;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-25 -	Create a new command sequence row with the given type, definition, and flags. */
CmdRow * csq_cmdrow_new(CRType type, const gchar *def, guint32 flags)
{
	CmdRow		*row;
	const gchar	*def2;

	row = g_malloc(sizeof *row);
	row->type  = type;
	row->flags = flags;
	memset(&row->extra, 0, sizeof row->extra);
	if(row->type == CRTP_BUILTIN)
		def2 = csq_cmdseq_map_name(def, "command row");
	else
		def2 = def;
	if((row->def = g_string_new(def2)) != NULL)
		return row;
	g_free(row);
	return NULL;
}

/* 1998-09-26 -	Create and return a verbatim copy of <src>. The copy is "deep"; i.e. there is
**		zero sharing of memory between the original and the copy.
*/
CmdRow * csq_cmdrow_copy(const CmdRow *src)
{
	CmdRow	*dst;

	if((dst = g_malloc(sizeof *dst)) != NULL)
	{
		dst->type  = src->type;
		dst->flags = src->flags;
		dst->extra = src->extra;
		if((dst->def = g_string_new(src->def->str)) != NULL)
			return dst;
		g_free(dst);
	}
	return NULL;
}

/* 1998-09-27 -	Set a new command type for the given command row. */
void csq_cmdrow_set_type(CmdRow *row, CRType type)
{
	if(row != NULL)
	{
		row->type = type;
		memset(&row->extra, 0, sizeof row->extra);
	}
}

/* 1998-09-27 -	Change the definition of the given row. */
void csq_cmdrow_set_def(CmdRow *row, const gchar *def)
{
	if(row != NULL && def != NULL)
		g_string_assign(row->def, def);
}

/* 1998-09-25 -	Destroy a command sequence row. */
void csq_cmdrow_destroy(CmdRow *row)
{
	if(row != NULL)
	{
		if(row->def != NULL)
			g_string_free(row->def, TRUE);
		g_free(row);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-10-21 -	Init built-in commands. Any new commands written must be added to the
**		hash table by adding a line in here. This might not be elegant, but it works.
** 1999-04-04 -	Completely rewritten, since we now handle config data for commands.
*/
void csq_init_commands(MainInfo *min)
{
	min->cfg.commands.builtin = NULL;

	csq_add_builtin(min, "DirEnter",	cmd_direnter,		NULL);
	csq_add_builtin(min, "DirFromOther", 	cmd_fromother,		NULL);
	csq_add_builtin(min, "DirToOther", 	cmd_toother,		NULL);
	csq_add_builtin(min, "DirParent",	cmd_parent,		NULL);
	csq_add_builtin(min, "DirRescan",	cmd_dirrescan,		NULL);
	csq_add_builtin(min, "DirSwap",		cmd_swap,		NULL);

	csq_add_builtin(min, "DpHide",		cmd_dphide,		NULL);
	csq_add_builtin(min, "DpRecenter",	cmd_dprecenter,		NULL);
	csq_add_builtin(min, "DpReorient",	cmd_dpreorient,		NULL);
	csq_add_builtin(min, "DpFocus",		cmd_dpfocus,		NULL);
	csq_add_builtin(min, "DpFocusPath",	cmd_dpfocuspath,	NULL);
	csq_add_builtin(min, "DpFocusISrch",	cmd_dpfocusisrch,	cfg_dpfocusisrch);
	csq_add_builtin(min, "DpGotoRow",	cmd_dpgotorow,		NULL);
	csq_add_builtin(min, "DpMaximize",	cmd_dpmaximize,		NULL);

	csq_add_builtin(min, "ActivateOther",	cmd_activateother,	NULL);
	csq_add_builtin(min, "ActivateLeft",	cmd_activateleft,	NULL);
	csq_add_builtin(min, "ActivateRight",	cmd_activateright,	NULL);
	csq_add_builtin(min, "ActivatePush",	cmd_activatepush,	NULL);
	csq_add_builtin(min, "ActivatePop",	cmd_activatepop,	NULL);

	csq_add_builtin(min, "Copy",		cmd_copy,		cfg_copy);
	csq_add_builtin(min, "CopyAs", 		cmd_copyas,		NULL);
	csq_add_builtin(min, "Clone",		cmd_clone,		NULL);
	csq_add_builtin(min, "Move",		cmd_move,		NULL);
	csq_add_builtin(min, "MoveAs",		cmd_moveas,		NULL);
	csq_add_builtin(min, "Delete",		cmd_delete,		cfg_delete);
	csq_add_builtin(min, "Rename",		cmd_rename,		cfg_rename);
	csq_add_builtin(min, "RenameRE",	cmd_renamere,		NULL);
	csq_add_builtin(min, "RenameSeq",	cmd_renameseq,		NULL);
	csq_add_builtin(min, "ChMod",		cmd_chmod,		NULL);
	csq_add_builtin(min, "ChOwn",		cmd_chown,		NULL);
	csq_add_builtin(min, "Split",		cmd_split,		NULL);
	csq_add_builtin(min, "Join",		cmd_join,		NULL);
	csq_add_builtin(min, "MkDir",		cmd_mkdir,		cfg_mkdir);
	csq_add_builtin(min, "GetSize",		cmd_getsize,		cfg_getsize);
	csq_add_builtin(min, "ClearSize",	cmd_clearsize,		NULL);
	csq_add_builtin(min, "SymLink",		cmd_symlink,		NULL);
	csq_add_builtin(min, "SymLinkAs",	cmd_symlinkas,		NULL);
	csq_add_builtin(min, "SymLinkClone",	cmd_symlinkclone,	NULL);
	csq_add_builtin(min, "SymLinkEdit",	cmd_symlinkedit,	NULL);

	csq_add_builtin(min, "ViewText",	cmd_viewtext,		cfg_viewtext);

	csq_add_builtin(min, "FileAction",	cmd_fileaction,		NULL);

	csq_add_builtin(min, "MenuPopup",	cmd_menupopup,		NULL);

	csq_add_builtin(min, "SelectRow",	cmd_selectrow,		NULL);
	csq_add_builtin(min, "SelectAll",	cmd_selectall,		NULL);
	csq_add_builtin(min, "SelectNone",	cmd_selectnone,		NULL);
	csq_add_builtin(min, "SelectToggle",	cmd_selecttoggle,	NULL);
	csq_add_builtin(min, "SelectRE",	cmd_selectre,		NULL);
	csq_add_builtin(min, "SelectExt",	cmd_selectext,		NULL);
	csq_add_builtin(min, "SelectType",	cmd_selecttype,		NULL);
	csq_add_builtin(min, "SelectSuffix",	cmd_selectsuffix,	NULL);
/*	csq_add_builtin(min, "SelectCmp",	cmd_selectcmp,		NULL);*/
	csq_add_builtin(min, "UnselectFirst",	cmd_unselectfirst,	NULL);
	csq_add_builtin(min, "SelectShell",	cmd_selectshell,	NULL);

	csq_add_builtin(min, "Information",	cmd_information,	cfg_information);

	csq_add_builtin(min, "About",		cmd_about,		NULL);
	csq_add_builtin(min, "Run",		cmd_run,		NULL);
	csq_add_builtin(min, "Rerun",		cmd_rerun,		NULL);
	csq_add_builtin(min, "Configure",	cmd_configure,		cfg_configurecmd);
	csq_add_builtin(min, "ConfigureSave",	cmd_configuresave,	NULL);
	csq_add_builtin(min, "Quit", 		cmd_quit,		NULL);
}
