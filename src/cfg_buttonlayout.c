/*
** 2002-05-31 -	A little configuration module for the button layout. This should hopefully
**		grow into something complex in the future, but for now, it's very basic
**		since it all it does is keep some stuff from the Shortcuts config.
*/

#include "gentoo.h"

#include "configure.h"
#include "cfg_module.h"

#include "cfg_buttonlayout.h"

#define	NODE	"ButtonLayout"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	GtkWidget	*vbox;

	GtkWidget	*pos[2];
	GtkWidget	*sep[3];

	ButtonLayout	*edit;

	MainInfo	*min;
	gboolean	modified;
} P_ButtonLayout;

static P_ButtonLayout	the_page;

/* ----------------------------------------------------------------------------------------- */

static void set_widgets(P_ButtonLayout *page)
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->pos[btl_buttonlayout_get_right(page->edit) == TRUE]), TRUE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->sep[btl_buttonlayout_get_separation(page->edit)]), TRUE);
}

static void evt_pos_toggle(GtkWidget *wid, gpointer user)
{
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)))
	{
		P_ButtonLayout	*page = user;

		btl_buttonlayout_set_right(page->edit, g_object_get_data(G_OBJECT(wid), "right") != NULL);
		page->modified = TRUE;
	}
}

static void evt_sep_toggle(GtkWidget *wid, gpointer user)
{
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)))
	{
		P_ButtonLayout	*page = user;

		btl_buttonlayout_set_separation(page->edit, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "sep")));
		page->modified = TRUE;
	}
}

static GtkWidget * init(MainInfo *min, gchar **name)
{
	P_ButtonLayout	*page = &the_page;
	const gchar	*plab[] = { N_("Left of Command Buttons"), N_("Right of Command Buttons") },
			*slab[] = { N_("No Padding"), N_("Static"), N_("Paned") };
	const BtlSep	sep[] = { BTL_SEP_NONE, BTL_SEP_SIMPLE, BTL_SEP_PANED };
	GtkWidget	*label, *frame, *vbox;
	guint		i;
	GSList		*group = NULL;

	page->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	page->edit = NULL;

	label = gtk_label_new(_("This page lets you control how the Shortcuts button sheet is positioned relative to\n"
				"the one holding the main Command Buttons. It is more or less a place-holder; the plan\n"
				"is to provide a lot more flexibility in the button management, and also to support the\n"
				"creation of more than these two built-in sheets of buttons. But that has yet to happen.\n"
				"\n"
				"In the meantime, this provides the functionality that was present when the Shortcuts\n"
				"were a special feature with their own configuration page (up to and including version\n"
				"0.11.24 of gentoo), for your convenience.\n"
				"\n"
				"To find the Shortcut sheet, switch to the Definitions page, and use the option menu widget\n"
				"in the top left corner of the page."));
	gtk_box_pack_start(GTK_BOX(page->vbox), label, FALSE, FALSE, 5);

	frame = gtk_frame_new(_("Shortcut Sheet Position"));
	vbox  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	for(i = 0; i < sizeof plab / sizeof *plab; i++)
	{
		page->pos[i] = gtk_radio_button_new_with_label(group, _(plab[i]));
		if(i == 1)
			g_object_set_data(G_OBJECT(page->pos[i]), "right", GINT_TO_POINTER(TRUE));
		g_signal_connect(G_OBJECT(page->pos[i]), "toggled", G_CALLBACK(evt_pos_toggle), page);
		gtk_box_pack_start(GTK_BOX(vbox), page->pos[i], FALSE, FALSE, 0);
		group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(page->pos[i]));
	}
	gtk_container_add(GTK_CONTAINER(frame), vbox);
	gtk_box_pack_start(GTK_BOX(page->vbox), frame, FALSE, FALSE, 0);

	frame = gtk_frame_new(_("Separation Style"));
	vbox  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	for(i = 0, group = NULL; i < sizeof slab / sizeof *slab; i++)
	{
		page->sep[i] = gtk_radio_button_new_with_label(group, _(slab[i]));
		g_object_set_data(G_OBJECT(page->sep[i]), "sep", GINT_TO_POINTER(sep[i]));
		g_signal_connect(G_OBJECT(page->sep[i]), "toggled", G_CALLBACK(evt_sep_toggle), page);
		gtk_box_pack_start(GTK_BOX(vbox), page->sep[i], FALSE, FALSE, 0);
		group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(page->sep[i]));
	}
	gtk_container_add(GTK_CONTAINER(frame), vbox);
	gtk_box_pack_start(GTK_BOX(page->vbox), frame, FALSE, FALSE, 0);

	gtk_widget_show_all(page->vbox);
	cfg_tree_level_append(_("Layout"), page->vbox);
	cfg_tree_level_end();
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

static void update(MainInfo *min)
{
	P_ButtonLayout	*page = &the_page;

	if(page->edit != NULL)
		btl_buttonlayout_destroy(page->edit);
	page->edit = btl_buttonlayout_new_copy(min->cfg.buttonlayout);
	page->modified = FALSE;
	set_widgets(page);
}

static void accept(MainInfo *min)
{
	P_ButtonLayout	*page = &the_page;

	if(page->modified)
	{
		btl_buttonlayout_destroy(min->cfg.buttonlayout);
		min->cfg.buttonlayout = page->edit;
		page->edit = NULL;
		cfg_set_flags(CFLG_REBUILD_BOTTOM);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 2002-05-31 -	Save button layout data. Simple, just call the buttonlayout module. */
static gint save(MainInfo *min, FILE *out)
{
	btl_buttonlayout_save(min->cfg.buttonlayout, out);
	return TRUE;
}

/* 2002-05-31 -	Load button layout settings. Again, pass it to the layout module. */
static void load(MainInfo *min, const XmlNode *node)
{
	min->cfg.buttonlayout = btl_buttonlayout_load(min, node);
}

/* ----------------------------------------------------------------------------------------- */

const CfgModule * cbl_describe(MainInfo *min)
{
	static const CfgModule	desc = { NODE, init, update, accept, save, load, NULL };

	return &desc;
}
