/*
** 1998-05-29 -	Header file for the generic command interface. A little more meat than other
**		command headers.
*/

#if !defined CMD_GENERIC_H
#define	CMD_GENERIC_H

enum {	CGF_NOALL = 1<<0,	/* Skip the "All" button, useful for e.g. cmd_rename. */
	CGF_NODST = 1<<1,	/* Don't rescan the destination after command completes. */
	CGF_NODIRS = 1<<2,	/* Never call the body or action function on a directory. */
	CGF_NORETURN = 1<<3,	/* Don't bind Return to OK button. */
	CGF_NOESC = 1<<4,	/* Don't bind Escape to Cancel-button. */
	CGF_SRC = 1<<5,		/* DO rescan the SOURCE after completion (great for Clone). */
	CGF_LINKSONLY = 1 << 6	/* Exclude all entries that are not symbolic links. Kind of specialized. */
	};

typedef void	(*GenBodyFunc)(MainInfo *min, DirPane *src, DirRow2 *row, gpointer generic, gpointer user);
typedef gint	(*GenActionFunc)(MainInfo *min, DirPane *src, DirPane *dst, DirRow2 *row, GError **error, gpointer user);
typedef void	(*GenFreeFunc)(gpointer user);

extern gint	cmd_generic(MainInfo *min, const gchar *title, guint32 flags, GenBodyFunc bf, GenActionFunc af,
				GenFreeFunc ff, gpointer user);

extern void	cmd_generic_track_entry(gpointer gen, GtkWidget *entry);

#endif		/* CMD_GENERIC_H */
