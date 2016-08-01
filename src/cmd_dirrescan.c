/*
** 1998-06-04 -	An encapsulation of the DirPane's rescan function as a native command.
**		Highly useful, especially in menus.
*/

#include "gentoo.h"

#include "cmdseq.h"
#include "dirpane.h"
#include "dirhistory.h"
#include "errors.h"

#include "cmd_dirrescan.h"

#define	CMD_ID	"dirrescan"

/* ----------------------------------------------------------------------------------------- */

/* 2002-07-19 -	Rescan source directory.
** 2010-03-04 -	Removed all pretense that this does error detection or reporting.
*/
gint cmd_dirrescan(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	dp_rescan(src);		/* FIXME: There ought to be a way for this to report failure. */
	dp_show_stats(src);
	return 1;
}
