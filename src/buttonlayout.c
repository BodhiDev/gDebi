/*
** 2002-05-31 -	This is the beginning for something long overdue; layout for buttons.
**		In this initial version, there are only two "sheets" of buttons that
**		need to be laid out: the classic commands ("Default") and the one with
**		the shortcuts ("Shortcuts", really). In the future, I hope this will
**		expand a bit.
*/

#include "gentoo.h"

#include "xmlutil.h"

#include "buttonlayout.h"

/* ----------------------------------------------------------------------------------------- */

/* Hold information about how the buttons are to be laid out. This will probably
** be completely replaced by the "real" version, sometime in the future.
*/
struct ButtonLayout {
	gboolean	sc_right;	/* Shortcuts to the right of default buttons? */
	BtlSep		sep_mode;
};

/* ----------------------------------------------------------------------------------------- */

ButtonLayout * btl_buttonlayout_new(void)
{
	ButtonLayout	*btl;

	btl = g_malloc(sizeof *btl);
	btl->sc_right = FALSE;
	btl->sep_mode = BTL_SEP_PANED;

	return btl;
}

ButtonLayout * btl_buttonlayout_new_copy(const ButtonLayout *original)
{
	ButtonLayout	*btl;

	btl = btl_buttonlayout_new();
	btl_buttonlayout_copy(btl, original);
	return btl;
}

void btl_buttonlayout_copy(ButtonLayout *dest, const ButtonLayout *src)
{
	if(dest != NULL && src != NULL)
		*dest = *src;
}

void btl_buttonlayout_destroy(ButtonLayout *btl)
{
	g_free(btl);
}

/* ----------------------------------------------------------------------------------------- */

void btl_buttonlayout_set_right(ButtonLayout *btl, gboolean right)
{
	btl->sc_right = right;
}

gboolean btl_buttonlayout_get_right(const ButtonLayout *btl)
{
	return btl->sc_right;
}

void btl_buttonlayout_set_separation(ButtonLayout *btl, BtlSep sep)
{
	btl->sep_mode = sep;
}

BtlSep btl_buttonlayout_get_separation(const ButtonLayout *btl)
{
	return btl->sep_mode;
}

/* ----------------------------------------------------------------------------------------- */

GtkWidget * btl_buttonlayout_pack(const ButtonLayout *btl, GtkWidget *sheet_default, GtkWidget *sheet_shortcuts)
{
	GtkWidget	*hbox, *left, *right, *sep, *pane;

	/* If one of the two sheets is empty, return the other one, no fancy layout needed. */
	if(sheet_default && !sheet_shortcuts)
		return sheet_default;
	else if(!sheet_default && sheet_shortcuts)
		return sheet_shortcuts;
	else if(!sheet_default && !sheet_shortcuts)	/* Both sheets empty? */
		return NULL;

	left  = btl->sc_right ? sheet_default : sheet_shortcuts;
	right = btl->sc_right ? sheet_shortcuts : sheet_default;

	switch(btl->sep_mode)
	{
		case BTL_SEP_NONE:
			hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			gtk_box_pack_start(GTK_BOX(hbox), left,  TRUE, TRUE, 0);
			gtk_box_pack_start(GTK_BOX(hbox), right, TRUE, TRUE, 0);
			return hbox;
		case BTL_SEP_SIMPLE:
			hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			gtk_box_pack_start(GTK_BOX(hbox), left, TRUE, TRUE, 0);
			sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
			gtk_box_pack_start(GTK_BOX(hbox), sep, FALSE, FALSE, 5);
			gtk_box_pack_start(GTK_BOX(hbox), right, TRUE, TRUE, 0);
			return hbox;
		case BTL_SEP_PANED:
			pane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
			gtk_paned_add1(GTK_PANED(pane), left);
			gtk_paned_add2(GTK_PANED(pane), right);
			return pane;
	}
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

void btl_buttonlayout_save(const ButtonLayout *btl, FILE *out)
{
	xml_put_node_open(out, "ButtonLayout");
	xml_put_node_open(out, "ShortcutSheet");
	xml_put_boolean(out, "right", btl->sc_right);
	xml_put_integer(out, "separation", btl->sep_mode);
	xml_put_node_close(out, "ShortcutSheet");
	xml_put_node_close(out, "ButtonLayout");
}

ButtonLayout * btl_buttonlayout_load(MainInfo *min, const XmlNode *node)
{
	gboolean	tmp;
	gint		sep;

	if((node = xml_tree_search(node, "ShortcutSheet")) == NULL)
		return min->cfg.buttonlayout;

	if(xml_get_boolean(node, "right", &tmp))
		min->cfg.buttonlayout->sc_right = tmp;
	if(xml_get_integer(node, "separation", &sep))
		min->cfg.buttonlayout->sep_mode = sep;

	return min->cfg.buttonlayout;
}
