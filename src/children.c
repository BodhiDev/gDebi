/*
** 1998-09-25 -	This module deals with child processes. Cut out from the old commands module,
**		which really was kind of bloated.
** 1998-12-15 -	Rewritten. Now uses the POSIX sigaction() API, which perhaps is more portable.
**		I've been noticing some odd problems on Solaris, with "infinite signals" etc.
**		I found my (virtual) copy of the Linux Programmer's Guide, and it makes me
**		believe that POSIX signals are reliable by default (i.e. the handler need not
**		be reinstalled). New problem: possible interruption of system calls. :(
** 1999-03-31 -	Made this module a lot more self-contained.
** 1999-04-08 -	Now waitpid()s for the child after chd_kill_child(), which really is better.
*/

#include "gentoo.h"

#if !defined _POSIX_SOURCE
 #define _POSIX_SOURCE
#endif

#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "dialog.h"
#include "strutil.h"
#include "queue.h"
#include "children.h"

/* ----------------------------------------------------------------------------------------- */

typedef struct {			/* Information about a single child process. */
	GPid	pid;
	guint	watch;				/* Glib source associated with this child. */
	gchar	prog[MAXNAMLEN];		/* Name of program running as this child (argv[0]). */
	guint32	gflags;				/* The general flags for the command. */
	guint32	aflags;				/* After-flags to associate with this child. */
} Child;

typedef struct {			/* Holds data about child processes we have spawned. */
	MainInfo	*min;			/* Very handy to have around. Keep first! */
	GSList		*child_list;		/* List of running processes. */
	GPid		lock_pid;		/* Pid of a command that we want to wait for. */
	CmdSeq		*running;		/* If non-null, we're currently running this sequence. */
	guint		index;			/* When 'running' is set, this is the next position to run. */
} ChildInfo;

static ChildInfo	the_chi = { NULL };

/* ----------------------------------------------------------------------------------------- */

static gboolean	chd_unregister(const gchar *name);

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-08 -	Rewritten, now simply initializes our private ChildInfo. */
gboolean chd_initialize(MainInfo *min)
{
	if(the_chi.min == NULL)
	{
		the_chi.min	   = min;
		the_chi.child_list = NULL;
		the_chi.lock_pid   = -1;
		the_chi.running    = NULL;
		the_chi.index	   = 0;

		return TRUE;
	}
	return FALSE;
}

/* ----------------------------------------------------------------------------------------- */

/* 2010-03-08 -	A child process has tragically died, and needs to be reaped. */
static void cb_watch_child(GPid pid, gint status, gpointer user)
{
	ChildInfo	*chi = &the_chi;
	Child		*ch = user;

	/* Unlink from rest of GTK+, as soon as possible, to be safe. */
	chi->child_list = g_slist_remove(chi->child_list, ch);
	g_source_remove(ch->watch);

	/* Now handle exit status. */
	if(WIFEXITED(status) && WEXITSTATUS(status) == CHD_EXIT_FAILURE)
	{
		gchar	buf[1024];

		g_snprintf(buf, sizeof buf, _("Execution of \"%s\" Failed"), ch->prog);
		dlg_dialog_sync_new_simple_wait(buf, _("Error"), _("_OK"));
	}
	if(chi->lock_pid == ch->pid)		/* Running in foreground? */
		que_enqueue(chi->min, QEVT_CONTINUE_CMD);
	que_enqueue(chi->min, QEVT_END_CMD, ch->aflags);

	g_free(ch);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-09 -	Register a child process started by a user command. Keeps not only
**		the <pid>. This information is used to kill previous instance(s).
** 1998-09-10 -	Now takes an additional parameter, <lock>. If set, the GUI is locked.
** 1998-09-18 -	This routine is now exported for use by other code (the new "file" module).
*/
void chd_register(const gchar *prog, GPid pid, guint32 gflags, guint32 aflags)
{
	ChildInfo	*chi = &the_chi;
	Child		*ch;

	ch = g_malloc(sizeof *ch);
	g_strlcpy(ch->prog, prog, sizeof ch->prog);
	ch->pid	   = pid;
	ch->gflags = gflags;
	ch->aflags = aflags;
	chi->child_list = g_slist_append(chi->child_list, ch);
	if(!(gflags & CGF_RUNINBG))
	{
		chi->lock_pid = pid;
		gtk_widget_set_sensitive(the_chi.min->gui->window, FALSE);
	}
	ch->watch = g_child_watch_add(ch->pid, cb_watch_child, ch);
}

/* 1999-04-08 -	Unregister the child named <name>. This is not meant for public consumption,
**		hence it's static. Returns TRUE if a child was found, else FALSE.
** NOTE NOTE	This function doesn't kill any processes or otherwise affect the system's
**		process table: it just removes the internal data.
*/
static gboolean chd_unregister(const gchar *name)
{
	GSList		*iter;
	Child		*ch;
	ChildInfo	*chi = &the_chi;

	for(iter = chi->child_list; iter != NULL; iter = g_slist_next(iter))
	{
		ch = iter->data;
		if(strcmp(ch->prog, name) == 0)
		{
			chi->child_list = g_slist_remove_link(chi->child_list, iter);
			g_slist_free_1(iter);
			g_source_remove(ch->watch);
			g_free(ch);
			return TRUE;
		}
	}
	return FALSE;
}

/* 1999-03-31 -	Set the continuation information. */
void chd_set_running(CmdSeq *cs, guint index)
{
	the_chi.running	= cs;
	the_chi.index	= index;
}

/* 1999-03-31 -	Get the currently running command sequence. If non-NULL, <index> will
**		receive the index of the next row to run.
*/
CmdSeq * chd_get_running(guint *index)
{
	if(index != NULL)
		*index = the_chi.index;
	return the_chi.running;
}

/* 1999-03-31 -	Clear continuation info (since the sequence has finished). */
void chd_clear_running(void)
{
	the_chi.running	= NULL;
	the_chi.index	= 0;
}

/* 1999-03-31 -	Just clear the locking child PID (since it just died, typically). */
void chd_clear_lock(void)
{
	the_chi.lock_pid = -1;
}

/* 1998-09-28 -	Kill (all) running instances of command named <name>. */
void chd_kill_child(const gchar *name)
{
	GSList	*iter, *next;
	Child	*ch;
	guint	rcount = 0U;
	pid_t	pid;

	for(iter = the_chi.child_list; iter != NULL; iter = next)
	{
		next = g_slist_next(iter);
		ch = iter->data;
		if(strcmp(ch->prog, name) == 0)
		{
			pid = ch->pid;		/* Buffer in case sighandler g_free()s it. */
			if(kill(pid, SIGTERM) == 0)
			{
				int	ret;

				if((ret = waitpid(pid, NULL, 0)) == pid)
					rcount++;
			}
			else
				perror("CHILDREN: kill() failed");
		}
	}
	while(rcount && chd_unregister(name))
		rcount--;
}

/* 1998-05-26 -	Kill any child processes (left over from running asynchronous commands).
** 1998-09-09 -	Rewritten due a more advanced child data format.
** 1998-10-11 -	Moved into the children module, where it belongs.
*/
void chd_kill_children(void)
{
	GSList	*iter, *next;
	Child	*ch;

	for(iter = the_chi.child_list; iter != NULL; iter = next)
	{
		next = g_slist_next(iter);
		ch   = iter->data;
		if(ch->gflags & CGF_SURVIVE)
			continue;
		g_source_remove(ch->watch);
		if(kill(ch->pid, SIGTERM) != 0)
			g_warning(_("Couldn't terminate child \"%s\" (pid=%d)--zombie alert"), ch->prog, (gint) ch->pid);
		else
		{
			waitpid(ch->pid, NULL, 0);
			the_chi.child_list = g_slist_remove(the_chi.child_list, ch);
			g_free(ch);
		}
	}
}
