/*
** 2009-12-28 -	Implementation of the nagging configuration. This mainly gives the user an option to forget
**		which dialogs have been suppressed, and hooks into the save/load of configure data.
*/

#include "gentoo.h"

#include "configure.h"
#include "xmlutil.h"

#include "cfg_nag.h"

#define	NODE	"Nagging"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	GtkWidget	*vbox;
	GtkWidget	*label;
	GtkWidget	*reset;
	gboolean	do_reset;

	MainInfo	*min;
	gboolean	modified;
} P_Nag;

static P_Nag	the_page;

/* ----------------------------------------------------------------------------------------- */

void cng_initialize(NagInfo *ni)
{
	ni->ignored = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
}

gsize cng_num_ignored(const NagInfo *ni)
{
	return ni != NULL ? g_hash_table_size(ni->ignored) : 0u;
}

gboolean cng_is_ignored(const NagInfo *ni, const gchar *tag)
{
	if(ni == NULL || tag == NULL)
		return FALSE;
	if(g_hash_table_lookup_extended(ni->ignored, tag, NULL, NULL))
		return TRUE;
	return FALSE;
}

/* 2009-12-29 -	Adds the given tag to the set of ignored tags. The tag is copied. */
void cng_ignore(NagInfo *ni, const gchar *tag)
{
	gpointer	key;

	if(ni == NULL || tag == NULL)
		return;
	/* Protect against multiple insertions, since that would leak memory. */
	if(g_hash_table_lookup_extended(ni->ignored, (gpointer) tag, NULL, NULL))
		return;
	if((key = g_strdup(tag)) != NULL)
	{
		g_hash_table_insert(ni->ignored, key, NULL);
	}
}

void cng_reset(NagInfo *ni)
{
	if(ni != NULL)
		g_hash_table_remove_all(ni->ignored);
}

/* ----------------------------------------------------------------------------------------- */

static void set_label(GtkWidget *label, gsize num)
{
	gchar	buf[256];

	g_snprintf(buf, sizeof buf, _("Click the button below to reset the %zu stored 'Don't show this dialog again' responses."), num);
	gtk_label_set_markup(GTK_LABEL(label), buf);
}

static void evt_reset_clicked(GtkWidget *wid, gpointer user)
{
	P_Nag	*page = user;

	page->do_reset = TRUE;
	page->modified = TRUE;
	set_label(page->label, 0);
	gtk_widget_set_sensitive(page->reset, FALSE);
}

static GtkWidget * cng_init(MainInfo *min, gchar **name)
{
	P_Nag	*page = &the_page;
	gsize	num;

	if(name == NULL)
		return NULL;

	*name = _("Nagging");

	page->min = min;
	page->modified = FALSE;

	page->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	page->label = gtk_label_new(NULL);
	num = cng_num_ignored(&min->cfg.nag);
	set_label(page->label, num);
	gtk_box_pack_start(GTK_BOX(page->vbox), page->label, FALSE, FALSE, 5);
	page->reset = gtk_button_new_with_label(_("Reset All"));
	gtk_widget_set_sensitive(page->reset, num > 0);
	g_signal_connect(G_OBJECT(page->reset), "clicked", G_CALLBACK(evt_reset_clicked), page);
	gtk_box_pack_start(GTK_BOX(page->vbox), page->reset, FALSE, FALSE, 0);

	gtk_widget_show_all(page->vbox);

	return page->vbox;
}

/* ----------------------------------------------------------------------------------------- */

static void cng_update(MainInfo *min)
{
	P_Nag	*page = &the_page;

	page->do_reset = FALSE;
	page->modified = FALSE;
}

/* ----------------------------------------------------------------------------------------- */

static void cng_accept(MainInfo *min)
{
	P_Nag	*page = &the_page;

	if(!page->modified)
		return;
	if(page->do_reset)
		cng_reset(&min->cfg.nag);
	page->modified = FALSE;
}

/* ----------------------------------------------------------------------------------------- */

static gint cng_save(MainInfo *min, FILE *out)
{
	GHashTableIter	iter;
	gpointer	key;

	xml_put_node_open(out, NODE);
	g_hash_table_iter_init(&iter, min->cfg.nag.ignored);
	while(g_hash_table_iter_next(&iter, &key, NULL))
	{
		xml_put_text(out, "ignore", key);
	}
	xml_put_node_close(out, NODE);

	return TRUE;
}

static void visit_ignore(const XmlNode *child, gpointer user)
{
	const gchar	*text;

	if(xml_get_text(child, "ignore", &text))
		cng_ignore(&((MainInfo *) user)->cfg.nag, text);
}

static void cng_load(MainInfo *min, const XmlNode *node)
{
	const XmlNode	*root;

	if((root = xml_tree_search(node, NODE)) != NULL)
		xml_node_visit_children(root, visit_ignore, min);
}

/* ----------------------------------------------------------------------------------------- */

const CfgModule * cng_describe(MainInfo *min)
{
	static const CfgModule	desc = { NODE, cng_init, cng_update, cng_accept, cng_save, cng_load, NULL };

	return &desc;
}
