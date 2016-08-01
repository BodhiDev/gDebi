/*
** 1999-05-10 -	A module to deal with the command group DpFocus, the purpose of which is
**		to (finally!) make keyboard navigation possible in gentoo. Should at least
**		give people something else to mail me about. :^)
*/

#include "gentoo.h"

#include "dirpane.h"
#include "cmdarg.h"
#include "cmdseq_config.h"
#include "nag_dialog.h"
#include "cmd_dpfocus.h"

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-10 -	Do some focusing. */
gint cmd_dpfocus(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	/* We no longer support this command, so nag to the user about its demise. */
	ndl_dialog_sync_new_wait(min, "dpfocus-deprecated", _("DpFocus Command is Deprecated"),
			_("The <tt>DpFocus</tt> command has been deprecated and is no longer "
			  "supported. Please remove any keyboard or mouse bindings that use it "
			  "and look into using the default GTK+ list view's cursor controls."));

	return 1;
}
