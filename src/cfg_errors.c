/*
** 2001-08-18 -	Configuration handling for the error reporting stuff. Small.
*/

#include "gentoo.h"

#include "cfg_module.h"
#include "configure.h"

#include "cfg_errors.h"

#define	NODE	"Errors"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	GtkWidget	*vbox;
	GtkWidget	*standard, *main_title, *dialog;
	GtkWidget	*beep;

	MainInfo	*min;
	ErrInfo		edit;
	gboolean	modified;
} P_Errors;

static P_Errors	the_page;

/* ----------------------------------------------------------------------------------------- */

static void evt_dialog_toggled(GtkWidget *wid, gpointer user)
{
	((P_Errors *) user)->modified = TRUE;
}

static void evt_beep_toggled(GtkWidget *wid, gpointer user)
{
	((P_Errors *) user)->modified = TRUE;
}

static GtkWidget * cer_init(MainInfo *min, gchar **name)
{
	P_Errors	*page = &the_page;
	GtkWidget	*vbox, *frame;

	if(name == NULL)
		return NULL;

	*name = _("Errors");

	page->min = min;
	page->modified = FALSE;

	page->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	frame = gtk_frame_new(_("Error and Status Message Display"));
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	page->standard = gtk_radio_button_new_with_label(NULL, _("Status Bar, Above Panes"));
	g_signal_connect(G_OBJECT(page->standard), "toggled", G_CALLBACK(evt_dialog_toggled), page);
	gtk_box_pack_start(GTK_BOX(vbox), page->standard, FALSE, FALSE, 0);
	page->main_title = gtk_radio_button_new_with_label(gtk_radio_button_get_group(GTK_RADIO_BUTTON(page->standard)), _("Main Window's Title Bar"));
	g_signal_connect(G_OBJECT(page->main_title), "toggled", G_CALLBACK(evt_dialog_toggled), page);
	gtk_box_pack_start(GTK_BOX(vbox), page->main_title, FALSE, FALSE, 0);
	page->dialog = gtk_radio_button_new_with_label(gtk_radio_button_get_group(GTK_RADIO_BUTTON(page->standard)), _("Separate Dialog"));
	g_signal_connect(G_OBJECT(page->dialog), "toggled", G_CALLBACK(evt_dialog_toggled), page);
	gtk_box_pack_start(GTK_BOX(vbox), page->dialog, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(frame), vbox);
	gtk_box_pack_start(GTK_BOX(page->vbox), frame, FALSE, FALSE, 0);

	page->beep = gtk_check_button_new_with_label(_("Cause Console Beep on Error?"));
	g_signal_connect(G_OBJECT(page->beep), "toggled", G_CALLBACK(evt_beep_toggled), page);
	gtk_box_pack_start(GTK_BOX(page->vbox), page->beep, FALSE, FALSE, 0);

	gtk_widget_show_all(page->vbox);
	return page->vbox;
}

/* ----------------------------------------------------------------------------------------- */

static void cer_update(MainInfo *min)
{
	P_Errors	*page = &the_page;

	page->edit = min->cfg.errors;
	if(page->edit.display == ERR_DISPLAY_STATUSBAR)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->standard), TRUE);
	else if(page->edit.display == ERR_DISPLAY_TITLEBAR)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->main_title), TRUE);
	else
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->dialog), TRUE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->beep),   page->edit.beep);
	page->modified = FALSE;
}

/* ----------------------------------------------------------------------------------------- */

static void cer_accept(MainInfo *min)
{
	P_Errors	*page = &the_page;

	if(!page->modified)
		return;
	/* We have very little widget state, so just grab it right here. */
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(page->standard)))
		page->edit.display = ERR_DISPLAY_STATUSBAR;
	else if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(page->main_title)))
		page->edit.display = ERR_DISPLAY_TITLEBAR;
	else
		page->edit.display = ERR_DISPLAY_DIALOG;
	page->edit.beep = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(page->beep));
	if(min->cfg.errors.display != page->edit.display)
		cfg_set_flags(CFLG_REBUILD_TOP);
	min->cfg.errors = page->edit;
	page->modified = FALSE;
}

/* ----------------------------------------------------------------------------------------- */

static gint cer_save(MainInfo *min, FILE *out)
{
	xml_put_node_open(out, NODE);
	xml_put_integer(out, "display", min->cfg.errors.display);
	xml_put_boolean(out, "beep",   min->cfg.errors.beep);
	xml_put_node_close(out, NODE);

	return TRUE;
}

static void cer_load(MainInfo *min, const XmlNode *node)
{
	const XmlNode	*root;

	if((root = xml_tree_search(node, NODE)) != NULL)
	{
		gboolean	tmp;
		gint		itmp;

		if(xml_get_integer(root, "display", &itmp))
			min->cfg.errors.display = itmp;
		else if(xml_get_boolean(root, "dialog", &tmp))	/* Legacy "dialog" boolean support. */
		{
			if(tmp)
				min->cfg.errors.display = ERR_DISPLAY_DIALOG;
			else
				min->cfg.errors.display = ERR_DISPLAY_STATUSBAR;
		}
		if(xml_get_boolean(root, "beep", &tmp))
			min->cfg.errors.beep = tmp;
	}
}

/* ----------------------------------------------------------------------------------------- */

const CfgModule * cer_describe(MainInfo *min)
{
	static const CfgModule	desc = { NODE, cer_init, cer_update, cer_accept, cer_save, cer_load, NULL };

	return &desc;
}
