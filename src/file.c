/*
** 1998-09-18 -	Maintain an instance of the 'file' external command as a child process, and
**		feed it typing requests when necessary. We will only start 'file' ONCE per
**		entire gentoo session (typically), thus amortizing its startup costs over
**		a very (?) long time. The aim of all this is hopefully to make typing using
**		the file rules cheaper.
** 2002-01-03 -	It's a while later, the file command on your system might now have the -n
**		option which causes it to flush its output, and enables this module to do
**		it's thing in the way intended. Used by cmd_info.c.
*/

#include "gentoo.h"

#include <fcntl.h>
#include <ctype.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "dialog.h"
#include "errors.h"
#include "children.h"
#include "file.h"

/* ----------------------------------------------------------------------------------------- */

static struct {
	gint	file_in;		/* Writing end of pipe connected to command's stdin. */
	gint	file_out;		/* Reading end of pipe connected to command's stdout. */
} file_info = { -1, -1 };

/* ----------------------------------------------------------------------------------------- */

static void start_file(MainInfo *min, const gchar *cmd)
{
	gchar	*argv[] = { "file", "file", "-n", "-f", "-", NULL };
	GPid	child;
	GError	*err = NULL;

	if(g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &child, &file_info.file_in, &file_info.file_out, NULL, &err))
	{
		chd_register("file", child, CGF_RUNINBG, 0);
	}
	else
	{
		gchar	buf[1024];

		g_snprintf(buf, sizeof buf, "Couldn't run the 'file' command:\n%s", err->message);
		dlg_dialog_async_new_error(buf);
		g_error_free(err);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-18 -	Run 'file' on the supplied file name, and return a pointer to its result line.
**		The returned string will be the result of 'file', minus the header and the
**		trailing newline. The returned string is static, and only valid up until the
**		next call to this function. If the execution fails, NULL is returned.
*/
const gchar * fle_file(MainInfo *min, const gchar *name)
{
	if(file_info.file_in < 0)
		start_file(min, "file");
	if(file_info.file_in > 0)		/* File command running? */
	{
		static gchar	resp[PATH_MAX + 256];
		gchar		line[PATH_MAX + 32];
		gint		len, got;
		fd_set		fds_read;
		struct timeval	to;

		len = g_snprintf(line, sizeof line, "%s\n", name);
		if(write(file_info.file_in, line, len) != len)
		{
			perror("Write to pipe");
			return NULL;
		}
		FD_ZERO(&fds_read);
		FD_SET(file_info.file_out, &fds_read);
		to.tv_sec  = 1U;
		to.tv_usec = 0U;
		if((select(file_info.file_out + 1, &fds_read, NULL, NULL, &to)) > 0)
		{
			if(FD_ISSET(file_info.file_out, &fds_read))
			{
				if((got = read(file_info.file_out, resp, sizeof resp - 1)) > 0)
				{
					const gchar	*cp;

					resp[got - 1] = '\0';
					if((cp = strrchr(resp, ':')) != NULL)
					{
						cp++;
						while(*cp && isspace((guchar) *cp))
							cp++;
						return cp;
					}
				}
			}
		}
	}
	return NULL;
}
