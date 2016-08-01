/*
** 2009-12-28 -	This is the implementation of the brand-new "nag" dialog. The first intended use is to
**		allow a bit friendlier indication of deprecated/removed commands, by keeping them listed
**		and replacing the body with a call here.
*/

#include "gentoo.h"

#include "cfg_nag.h"
#include "configure.h"
#include "dialog.h"

#include "nag_dialog.h"

/* ----------------------------------------------------------------------------------------- */

gboolean ndl_dialog_sync_new_wait(MainInfo *min, const gchar *tag, const gchar *title, const gchar *body)
{
	GtkWidget	*vbox, *wid, *ignore;
	gboolean	ret = FALSE;
	Dialog		*dlg;

	if(cng_is_ignored(&min->cfg.nag, tag))
		return TRUE;

	/* Not already ignored, so build and present dialog. */
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	wid = gtk_label_new(NULL);
	gtk_label_set_width_chars(GTK_LABEL(wid), 60);
	gtk_label_set_justify(GTK_LABEL(wid), GTK_JUSTIFY_LEFT);
	gtk_label_set_line_wrap(GTK_LABEL(wid), TRUE);
	gtk_label_set_line_wrap_mode(GTK_LABEL(wid), PANGO_WRAP_WORD);
	gtk_label_set_markup(GTK_LABEL(wid), body);
	gtk_box_pack_start(GTK_BOX(vbox), wid, FALSE, FALSE, 0);
	ignore = gtk_check_button_new_with_mnemonic(_("_Don't show this dialog again"));
	gtk_box_pack_start(GTK_BOX(vbox), ignore, FALSE, FALSE, 5);

	dlg = dlg_dialog_sync_new(vbox, title, _("OK"));
	dlg_dialog_sync_wait(dlg);

	/* The widgets are still around, so check directly if check button was checked. */
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ignore)))
	{
		cng_ignore(&min->cfg.nag, tag);
		cfg_modified_set(min);
		ret = TRUE;
	}
	dlg_dialog_sync_destroy(dlg);

	return ret;
}
