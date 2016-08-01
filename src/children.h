/*
** 1998-09-25 -	Child process handling module's header file.
*/

#if !defined CHILDREN_H
#define	CHILDREN_H

/* This is the return status (set by _exit()) to use to signal failure to
** execvp() a child process. It's picked up and noticed by our SIGCHD signal
** handler, and triggers a dialog. This feels dirty, but...
*/
#define	CHD_EXIT_FAILURE	117

extern gboolean	chd_initialize(MainInfo *min);

extern void	chd_register(const gchar *prog, GPid pid, guint32 gflags, guint32 aflags);

extern void	chd_set_running(CmdSeq *cs, guint index);
extern CmdSeq *	chd_get_running(guint *index);
extern void	chd_clear_running(void);
extern void	chd_clear_lock(void);

extern void	chd_kill_child(const gchar *name);
extern void	chd_kill_children(void);

#endif		/* CHILDREN_H */
