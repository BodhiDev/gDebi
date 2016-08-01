/*
** 1999-05-19 -	Started a complete rewrite of this module. It became twice the size of the old
**		code, but adds a *lot* of flexibility. And, the config module shrunk, too. :)
*/

#include "gentoo.h"

#include <stdlib.h>

#include "cmdseq.h"
#include "strutil.h"
#include "styles.h"

/* ----------------------------------------------------------------------------------------- */

struct StyleInfo {
	guint		freeze_count;			/* When non-zero, changes to styles don't propagate downwards. */
	gboolean	update_pending;			/* Set when something changes during freeze. */
	GNode		*styles;			/* A N-ary tree holding the style hierarchy. Neat. */
};

typedef enum { SPT_COLOR = 0, SPT_ICON, SPT_ACTION } StlPType;

typedef struct {
	StlPType	type;
	union {
	GdkColor	color;
	gchar		icon[FILENAME_MAX];
	GString		*action;
	}		data;
} StlPVal;

/* These are the old (L for legacy) integer identifiers for style properties. They are
** used in the old_load_property() function, when converting to the more modern string-
** based property names used by this module.
*/
enum {
	L_TYPE_UNSEL_BGCOL	= 0,
	L_TYPE_UNSEL_FGCOL	= 1,
	L_TYPE_UNSEL_ICON	= 2,
	L_TYPE_ACTN_DEFAULT	= 6,
	L_TYPE_ACTN_VIEW	= 7,
	L_TYPE_ACTN_EDIT	= 8,
	L_TYPE_ACTN_PRINT	= 9,
	L_TYPE_ACTN_PLAY	= 10
};

typedef struct {
	gchar		name[STL_PROPERTY_NAME_SIZE];	/* Unique property identifier. */
	gboolean	override;			/* Is the value really here? */
	StlPVal		*value;
} StlProp;

struct Style {
	StyleInfo	*styleinfo;			/* Which styleinfo does this style belong to? */
	gchar		name[STL_STYLE_NAME_SIZE];	/* Name of this style (e.g. "WAV", "HTML"). */
	GHashTable	*properties;			/* A hash of properties for this style. */
	gboolean	expand;				/* Is the style shown expanded in the config? */
};

/* This is used internally by the stl_styleinfo_style_find() function. */
struct style_find_data {
	const gchar	*name;
	Style		*stl;
};

/* This is used by stl_styleinfo_widget_find(). */
struct widget_find_data {
	Style		*style;
	GtkWidget	*widget;
};

struct property_rename_data {
	Style		*style;
	gchar		name_old[STL_PROPERTY_NAME_SIZE],
			name_new[STL_PROPERTY_NAME_SIZE];
};

/* This is used when looking for a style in a GtkTreeModel. */
struct tree_find_data {
	const Style	*style;
	GtkTreeIter	iter;
};

/* This is used to avoid magic numbers all over the place. */
enum {
	TREE_COLUMN_NAME = 0, TREE_COLUMN_STYLE
};

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-20 -	Create a new, empty, styleinfo. */
StyleInfo * stl_styleinfo_new(void)
{
	StyleInfo	*si;

	si = g_malloc(sizeof *si);
	si->freeze_count   = 0U;
	si->update_pending = FALSE;
	si->styles = NULL;

	return si;
}

/* 1999-05-19 -	Create a new styleinfo containing the built-in default styles. There are few. */
StyleInfo * stl_styleinfo_default(void)
{
	StyleInfo	*si;
	Style		*stl, *root;

	si = stl_styleinfo_new();
	stl_styleinfo_freeze(si);

	root = stl_style_new(_("Root"));
	stl_style_property_set_color_rgb(root, SPN_COL_UNSEL_BG, 56540U, 56540U, 56540U);
	stl_style_property_set_color_rgb(root, SPN_COL_UNSEL_FG, 0U, 0U, 0U);
	stl_style_property_set_icon(root, SPN_ICON_UNSEL, "Document.xpm");
	stl_styleinfo_style_add(si, NULL, root);

	stl = stl_style_new(_("Directory"));
	stl_style_property_set_color_rgb(stl, SPN_COL_UNSEL_FG, 65535U, 16384U, 8192U);
	stl_style_property_set_icon(stl, SPN_ICON_UNSEL, "Directory2.xpm");
	stl_style_property_set_action(stl, SPN_ACTN_DEFAULT, "DirEnter");
	stl_styleinfo_style_add(si, root, stl);

	stl_styleinfo_thaw(si);
	return si;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-20 -	Recursively copy styles and add them to the parent style pointed at by <user>. */
static void styleinfo_style_copy(GNode *node, gpointer user)
{
	Style	*stl, *parent = user;

	stl = stl_style_copy(node->data);
	stl_styleinfo_style_add(parent->styleinfo, parent, stl);
	g_node_children_foreach(node, G_TRAVERSE_ALL, styleinfo_style_copy, stl);
}

/* 1999-05-20 -	Create an identical copy of <si>, sharing no memory between the two. */
StyleInfo * stl_styleinfo_copy(StyleInfo *si)
{
	StyleInfo	*nsi = NULL;
	Style		*root;

	if(si != NULL)
	{
		nsi = stl_styleinfo_new();
		if(si->styles != NULL)
		{
			stl_styleinfo_freeze(nsi);
			root = stl_style_copy(si->styles->data);
			stl_styleinfo_style_add(nsi, NULL, root);
			g_node_children_foreach(si->styles, G_TRAVERSE_ALL, styleinfo_style_copy, root);
			stl_styleinfo_thaw(nsi);
		}
	}
	return nsi;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-20 -	If the style in <user> doesn't override the property <value>, create
**		a non-copying link to it, thus inheriting the value. This is it.
*/
static void property_update(gpointer key, gpointer value, gpointer user)
{
	Style	*stl = user;
	StlProp	*pr, *ppr = value;

	/* Is there a property with the same name? */
	if((pr = g_hash_table_lookup(stl->properties, ppr->name)) != NULL)
	{
		if(!pr->override)
			pr->value = ppr->value;		/* Establish the copy link. */
	}
	else	/* No property found, so create a fresh linking one. */
	{
		pr = g_malloc(sizeof *pr);
		g_strlcpy(pr->name, ppr->name, sizeof pr->name);
		pr->override = FALSE;
		pr->value = ppr->value;
		g_hash_table_insert(stl->properties, pr->name, pr);
	}
}

/* 1999-05-20 -	Update a single style, and then recurse to do its children. Heavily recursive. */
static void style_update(GNode *node, gpointer user)
{
	Style	*stl = node->data, *parent = user;

	g_hash_table_foreach(parent->properties, property_update, stl);
	g_node_children_foreach(node, G_TRAVERSE_ALL, style_update, stl);
}

/* 1999-05-19 -	Update style tree in <si>; causes all non-overridden properties in all styles to
**		acquire the value of the nearest overriding parent. Simple.
*/
static void styleinfo_update(StyleInfo *si)
{
	if(si != NULL)
	{
		if(si->freeze_count == 0)
		{
			g_node_children_foreach(si->styles, G_TRAVERSE_ALL, style_update, si->styles->data);
			si->update_pending = FALSE;
		}
		else
			si->update_pending = TRUE;
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-19 -	Freeze given styleinfo. When a styleinfo is frozen, you can add, alter, and
**		remove styles without causing the entire inheritance tree to be traversed
**		on each operation. Handy when doing loads of such operations.
*/
void stl_styleinfo_freeze(StyleInfo *si)
{
	if(si != NULL)
		si->freeze_count++;
}

/* 1999-05-19 -	Thaw a styleinfo. If anything was changed during freeze, update tree now. */
void stl_styleinfo_thaw(StyleInfo *si)
{
	if(si != NULL)
	{
		if(si->freeze_count == 0)
			fprintf(stderr, "STYLES: Mismatched call to stl_styleinfo_thaw() detected!\n");
		else if(--si->freeze_count == 0)
		{
			if(si->update_pending)
				styleinfo_update(si);
		}
	}
}

/* ----------------------------------------------------------------------------------------- */

#if 0
/* 1999-05-20 -	A g_hash_table_foreach() callback. */
static void dump_property(gpointer key, gpointer value, gpointer user)
{
	StlProp	*pr = value;
	guint	i, d = *(guint *) user;
	gchar	*tname[] = { "Color", "Icon", "Action" };

	for(i = 0; i < d; i++)
		putchar(' ');
	printf("(%c) %s [%s: ", pr->override ? 'X' : ' ', pr->name, tname[pr->value->type]);

	switch(pr->value->type)
	{
		case SPT_COLOR:
			printf("%04X,%04X,%04X", pr->value->data.color.red, pr->value->data.color.green, pr->value->data.color.blue);
			break;
		case SPT_ICON:
			printf("\"%s\"", pr->value->data.icon);
			break;
		case SPT_ACTION:
			printf("'%s'", pr->value->data.action->str);
			break;
	}
	printf("]\n");
}

/* 1999-05-20 -	A g_node_traverse() callback. */
static gboolean dump_style(GNode *node, gpointer user)
{
	guint	i, d = g_node_depth(node);

	for(i = 0; i < d - 1; i++)
		putchar('-');
	printf(" '%s'\n", ((Style *) node->data)->name);
	g_hash_table_foreach(((Style *) node->data)->properties, dump_property, &d);
	return FALSE;
}

/* 1999-05-20 -	Dump the contents of given styleinfo's style tree. Mainly useful during
**		development and debugging of this module. Should not be included in release
**		builds of gentoo, since it's not really useful there.
*/
void stl_styleinfo_dump(StyleInfo *si)
{
	guint	level;

	printf("There are %u styles in given styleinfo:\n", g_node_n_nodes(si->styles, G_TRAVERSE_ALL));
	g_node_traverse(si->styles, G_PRE_ORDER, G_TRAVERSE_ALL, -1, dump_style, &level);
}
#endif

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-19 -	Get the GNode that holds the given <stl>. */
static GNode * styleinfo_style_get_node(const StyleInfo *si, const Style *stl)
{
	return g_node_find(si->styles, G_PRE_ORDER, G_TRAVERSE_ALL, (gpointer) stl);
}

/* 1999-05-20 -	Find the sibling of <parent> that should come next after <node>,
**		if it were inserted. The implied ordering is simply alphabetical.
**		If there is no suitable sibling (<=> stl goes last), return NULL.
*/
static GNode * find_next_sibling(GNode *parent, GNode *node)
{
	guint	i;
	GNode	*here;

	for(i = 0; (here = g_node_nth_child(parent, i)) != NULL; i++)
	{
		if(strcmp(((Style *) here->data)->name, ((Style *) node->data)->name) > 0)
			return here;
	}
	return NULL;
}

/* 1999-05-19 -	Add a style to <si>, linking it in so <parent> is its immediate parent.
**		Call with <parent> == NULL to set the root style (moving the existing
**		styles, if any, down one level).
*/
void stl_styleinfo_style_add(StyleInfo *si, Style *parent, Style *stl)
{
	if((si != NULL) && (stl != NULL))
	{
		GNode	*node, *pnode;

		stl->styleinfo = si;
		node = g_node_new(stl);

		if(parent == NULL)		/* Insert as root? */
		{
			if(si->styles != NULL)	/* Is there a parent to replace? */
				g_node_insert(node, -1, si->styles);
			si->styles = node;
		}
		else
		{
			if((pnode = styleinfo_style_get_node(si, parent)) != NULL)
				g_node_insert_before(pnode, find_next_sibling(pnode, node), node);
			else
				fprintf(stderr, "STYLES: Unknown parent style '%s' referenced\n", parent->name);
		}
		styleinfo_update(si);
	}
}

/* 1999-05-20 -	A g_node_traverse() callback, to free the memory used by a style. */
static gboolean remove_style(GNode *node, gpointer user)
{
	if(node != user)	/* Avoid recursing on root. */
		stl_style_destroy(node->data, FALSE);
	return FALSE;		/* Keep traversing. */
}

/* 1999-05-20 -	Remove a style from the styleinfo tree. Causes all child styles to be
**		removed and destroyed, as well.
*/
void stl_styleinfo_style_remove(StyleInfo *si, Style *stl)
{
	if((si != NULL) && (stl != NULL))
	{
		GNode	*node;

		if((node = styleinfo_style_get_node(si, stl)) != NULL)
		{
			g_node_unlink(node);
			g_node_traverse(node, G_POST_ORDER, G_TRAVERSE_ALL, -1, remove_style, node);
			g_node_destroy(node);
			styleinfo_update(si);
		}
	}
}

/* 1999-05-20 -	Check for a named node. */
static gboolean find_callback(GNode *node, gpointer user)
{
	struct style_find_data	*sfd = user;

	if(strcmp(((Style *) node->data)->name, sfd->name) == 0)
	{
		sfd->stl = node->data;
		return TRUE;	/* Wanted node has been found, so stop. */
	}
	return FALSE;		/* Keep traversing. */
}

/* 1999-05-20 -	Find the named style in given styleinfo's style tree. NULL can be used as
**		a shorthand for the name of the root style.
*/
Style * stl_styleinfo_style_find(const StyleInfo *si, const gchar *name)
{
	struct style_find_data	sfd;

	sfd.name = name;
	sfd.stl  = NULL;
	if(si != NULL)
	{
		if(name != NULL)
			g_node_traverse(si->styles, G_PRE_ORDER, G_TRAVERSE_ALL, -1, find_callback, &sfd);
		else
			return stl_styleinfo_style_root(si);
	}
	return sfd.stl;
}

/* 1999-05-24 -	Change the parent for <stl>. <new_parent> should really NOT be a child of
**		<stl>, or there will definitely be evil. Note that this operation can NOT
**		be done as a remove()/add() combo, since the remove is destructive.
*/
void stl_styleinfo_style_set_parent(StyleInfo *si, Style *stl, Style *new_parent)
{
	if((si != NULL) && (stl != NULL))
	{
		GNode	*node, *pnode;

		node = styleinfo_style_get_node(si, stl);
		g_node_unlink(node);
		pnode = styleinfo_style_get_node(si, new_parent);
		g_node_insert_before(pnode, find_next_sibling(pnode, node), node);
		styleinfo_update(si);
	}
}

/* 1999-05-24 -	Get the parent style of <stl>. If there is no parent (<stl> is root, or
**		perhaps has not yet been added to a styleinfo), NULL is returned.
*/
Style * stl_styleinfo_style_get_parent(StyleInfo *si, Style *stl)
{
	if((stl != NULL) && (stl->styleinfo == si))
	{
		GNode	*node;

		node = styleinfo_style_get_node(stl->styleinfo, stl);
		if(node->parent != NULL)
			return node->parent->data;
	}
	return NULL;
}

/* 1999-06-12 -	Return TRUE if <stla> and <stlb> are siblings (i.e., they have the same
**		immediate parent), FALSE otherwise.
*/
gboolean stl_styleinfo_style_siblings(StyleInfo *si, Style *stla, Style *stlb)
{
	if((si != NULL) && (stla != NULL) && (stlb != NULL))
	{
		GNode	*na, *nb;

		na = styleinfo_style_get_node(si, stla);
		nb = styleinfo_style_get_node(si, stlb);

		return na->parent == nb->parent;
	}
	return FALSE;
}

/* 1999-05-26 -	Return the root style of <si>. This is actually the only reliable way of getting
**		at it, since you don't know its name.
*/
Style * stl_styleinfo_style_root(const StyleInfo *si)
{
	if(si != NULL)
	{
		if(si->styles != NULL)
			return si->styles->data;
	}
	return NULL;
}

/* 1999-05-27 -	Return TRUE if <stl> has one or more children, otherwise FALSE. */
gboolean stl_styleinfo_style_has_children(StyleInfo *si, Style *stl)
{
	if((si != NULL) && (stl != NULL))
	{
		GNode	*node;

		if((node = styleinfo_style_get_node(si, stl)) != NULL)
			return g_node_n_children(node) > 0 ? TRUE : FALSE;
	}
	return FALSE;
}

/* 1999-05-29 -	Just a g_node_traverse() callback to build a list of child styles. */
static gboolean get_child(GNode *node, gpointer user)
{
	GList	**list = user;

	*list = g_list_append(*list, node->data);

	return FALSE;	/* And keep traversing. */
}

/* 1999-05-29 -	Return a list of all child styles of <stl>. If <include_root> is TRUE, then
**		the root style <stl> will be included in the list. If FALSE, it will not.
**		Don't forget to g_list_free() the returned list when you're done with it.
*/
GList * stl_styleinfo_style_get_children(StyleInfo *si, Style *stl, gboolean include_root)
{
	if((si != NULL) && (stl != NULL) && (stl->styleinfo == si))
	{
		GNode	*node;

		if((node = styleinfo_style_get_node(si, stl)) != NULL)
		{
			GList	*list = NULL;

			g_node_traverse(node, G_PRE_ORDER, G_TRAVERSE_ALL, -1, get_child, &list);
			if(!include_root)
			{
				GList	*first = list;

				list = g_list_remove_link(list, list);	/* Remove head of child list. */
				g_list_free(first);
			}
			return list;
		}
	}
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-27 -	A g_hash_table_foreach() callback that writes out a property definition. */
static void save_property(gpointer key, gpointer value, gpointer user)
{
	StlProp	*pr = value;
	FILE	*out = user;

	/* We don't save non-overridden properties, of course. */
	if(!pr->override)
		return;

	xml_put_node_open(out, "Property");
	xml_put_text(out, "name", pr->name);
	switch(pr->value->type)
	{
		case SPT_COLOR:
			xml_put_color(out, "color", &pr->value->data.color);
			break;
		case SPT_ICON:
			xml_put_text(out, "icon", pr->value->data.icon);
			break;
		case SPT_ACTION:
			xml_put_text(out, "action", pr->value->data.action->str);
			break;
	}
	xml_put_node_close(out, "Property");
}

/* 1999-05-27 -	A g_node_traverse() function to save out a single style. */
static gboolean save_style(GNode *node, gpointer user)
{
	Style	*stl = node->data;
	FILE	*out = user;

	xml_put_node_open(out, "Style");
	xml_put_text(out, "name", stl->name);
	if(node->parent != NULL)
		xml_put_text(out, "parent", ((Style *) node->parent->data)->name);
	xml_put_boolean(out, "expand", stl->expand);
	if(stl_style_property_overrides(stl))
	{
		xml_put_node_open(out, "Properties");
		g_hash_table_foreach(stl->properties, save_property, out);
		xml_put_node_close(out, "Properties");
	}
	xml_put_node_close(out, "Style");

	return FALSE;		/* Keep traversing, we relly want to save all styles. */
}

/* 1999-05-27 -	Save out contents of <si> into <out>, nicely bracketed by <tag> tags. */
void stl_styleinfo_save(MainInfo *min, StyleInfo *si, FILE *out, const gchar *tag)
{
	if((min == NULL) || (si == NULL) || (out == NULL) || (tag == NULL))
		return;

	xml_put_node_open(out, tag);
	g_node_traverse(si->styles, G_PRE_ORDER, G_TRAVERSE_ALL, -1, save_style, out);
	xml_put_node_close(out, tag);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-20 -	Load an old-styled property. Convert legacy properties, identified using
**		integers, into the newer named style. Add the property to the style pointed
**		to by <user>.
*/
static void old_load_property(const XmlNode *node, gpointer user)
{
	gint		type;
	gboolean	ok = FALSE;
	const gchar	*name = NULL, *text = NULL;
	GdkColor	col;
	StlPType	newtype = SPT_COLOR;
	Style		*stl = user;

	xml_get_integer(node, "type", &type);

	/* Map old integer-identified properties to new textually named ones. */
	switch(type)
	{
		case L_TYPE_UNSEL_BGCOL:
			name = SPN_COL_UNSEL_BG;
			if((ok = xml_get_color(node, "color", &col)))
				newtype = SPT_COLOR;
			break;
		case L_TYPE_UNSEL_FGCOL:
			name = SPN_COL_UNSEL_FG;
			if((ok = xml_get_color(node, "color", &col)))
				newtype = SPT_COLOR;
			break;
		case L_TYPE_UNSEL_ICON:
			name = SPN_ICON_UNSEL;
			if((ok = xml_get_text(node, "icon", &text)))
				newtype = SPT_ICON;
			break;
		case L_TYPE_ACTN_DEFAULT:
			name = SPN_ACTN_DEFAULT;
			if((ok = xml_get_text(node, "action", &text)))
				newtype = SPT_ACTION;
			break;
		case L_TYPE_ACTN_VIEW:
			name = SPN_ACTN_VIEW;
			if((ok = xml_get_text(node, "action", &text)))
				newtype = SPT_ACTION;
			break;
		case L_TYPE_ACTN_EDIT:
			name = SPN_ACTN_EDIT;
			if((ok = xml_get_text(node, "action", &text)))
				newtype = SPT_ACTION;
			break;
		case L_TYPE_ACTN_PRINT:
			name = SPN_ACTN_PRINT;
			if((ok = xml_get_text(node, "action", &text)))
				newtype = SPT_ACTION;
			break;
		case L_TYPE_ACTN_PLAY:
			name = SPN_ACTN_PLAY;
			if((ok = xml_get_text(node, "action", &text)))
				newtype = SPT_ACTION;
			break;
		default:
			g_warning("STYLES: Ignoring unknown legacy property type %d in style '%s'\n", type, stl->name);
	}
	if(ok)
	{
		switch(newtype)
		{
			case SPT_COLOR:
				stl_style_property_set_color(stl, name, &col);
				break;
			case SPT_ICON:
				stl_style_property_set_icon(stl, name, text);
				break;
			case SPT_ACTION:
				stl_style_property_set_action(stl, name, csq_cmdseq_map_name(text, stl->name));
				break;
		}
	}
}

/* 1999-05-27 -	Load modern-style property (has textual identifier). Pretty simple stuff. */
static void load_property(const XmlNode *data, gpointer user)
{
	Style		*stl = user;
	const gchar	*name;

	if(xml_get_text(data, "name", &name))
	{
		GdkColor	color;
		const gchar	*text;

		if(xml_get_color(data, "color", &color))
			stl_style_property_set_color(stl, name, &color);
		else if(xml_get_text(data, "icon", &text))
			stl_style_property_set_icon(stl, name, text);
		else if(xml_get_text(data, "action", &text))
			stl_style_property_set_action(stl, name, csq_cmdseq_map_name(text, stl_style_get_name(stl)));
		else
			g_warning("STYLES: Unknown value type for property '%s' (style '%s')\n", name, stl->name);
	}
}

/* 1999-05-20 -	Another xml_node_visit_children() callback. Load in all properties for
**		given style, and store them in the style. This is where the backwards-
**		compatibility really gets going.
*/
static void load_properties(const XmlNode *node, Style *stl)
{
	const XmlNode	*data;

	if((data = xml_tree_search(node, "Property")) != NULL)
	{
		if(xml_get_integer(data, "type", NULL))
			xml_node_visit_children(node, old_load_property, stl);
		else
			xml_node_visit_children(node, load_property, stl);
	}
}

/* 1999-05-20 -	A xml_node_visit_children() callback to load in a style. Deals with the
**		annoying complexities of backwards compatibility. :(
*/
static void load_style(const XmlNode *node, gpointer user)
{
	StyleInfo	*si = user;
	Style		*stl, *parent = NULL;
	const gchar	*name = "new style", *pname;
	const XmlNode	*prop;
	gboolean	btmp = FALSE;

	xml_get_text(node, "name", &name);
	stl = stl_style_new(name);

	if(xml_get_boolean(node, "expand", &btmp))
		stl_style_set_expand(stl, btmp);
	if((prop = xml_tree_search(node, "Properties")) != NULL)
		load_properties(prop, stl);
	if(xml_get_text(node, "parent", &pname))
		parent = stl_styleinfo_style_find(si, pname);
	stl_styleinfo_style_add(si, parent, stl);
}

/* 1999-05-20 -	Load an entire styleinfo tree from XML data rooted at <node>. Note that the
**		top-level tag identifier (the one passed to stl_styleinfo_save()) should already
**		have been used before calling this function, to find <node>. It is assumed
**		that the given node contains a bunch of children, each of which defines a style.
*/
StyleInfo * stl_styleinfo_load(const XmlNode *node)
{
	StyleInfo	*si;

	si = stl_styleinfo_new();
	stl_styleinfo_freeze(si);
	xml_node_visit_children(node, load_style, si);
	stl_styleinfo_thaw(si);

	return si;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-20 -	Destroy given styleinfo, freeing all memory used by it. */
void stl_styleinfo_destroy(StyleInfo *si)
{
	if(si != NULL)
	{
		stl_styleinfo_freeze(si);	/* There's no point in updating inherited stuff while destroying. */
		if(si->styles != NULL)
			stl_style_destroy(si->styles->data, TRUE);
		g_free(si);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 2006-03-02 -	Append a branch of the style tree to the given store. This does not collapse/expand it
 *		properly, since that's not doable with GTK+2 at this level (it's a view property).
*/
static void append_branch(GtkTreeStore *store, const GNode *node, GtkTreeIter *parent, const Style *ignore, gboolean *modify_flag)
{
	GtkTreeIter	child;

	for(; (node != NULL); node = node->next)
	{
		if((ignore != NULL) && (node->data == ignore))
			continue;
		gtk_tree_store_append(store, &child, parent);
		gtk_tree_store_set(store, &child, TREE_COLUMN_NAME, ((const Style *) node->data)->name,
						  TREE_COLUMN_STYLE, node->data,
						  -1);
		if(node->children != NULL)
			append_branch(store, node->children, &child, ignore, modify_flag);
	}
}

/* 2006-03-02 -	Build a partial style store, ignoring any branch rooted at <ignore>. */
GtkTreeStore * stl_styleinfo_build_partial(const StyleInfo *si, const Style *ignore)
{
	GtkTreeStore	*store;

	store = gtk_tree_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);
	append_branch(store, si->styles, NULL, ignore, NULL);

	return store;
}

/* ----------------------------------------------------------------------------------------- */

/* 2009-03-22 -	We need a way to read out a Style pointer from a constructed tree, in the config. */
Style * stl_styleinfo_get_style_iter(const StyleInfo *si, GtkTreeStore *store, GtkTreeIter *iter)
{
	Style	*style = NULL;

	if(si == NULL || store == NULL || iter == NULL)
		return NULL;
	gtk_tree_model_get(GTK_TREE_MODEL(store), iter, TREE_COLUMN_STYLE, &style, -1);

	return style;
}

/* Simple bookkeeping struct used by stl_styleinfo_tree_find_style(), below. */
struct find_info
{
	const Style	*style;
	gboolean	found;
	GtkTreeIter	iter;
};

static gboolean cb_foreach(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	struct find_info	*fi = data;
	Style		*here;

	gtk_tree_model_get(model, iter, TREE_COLUMN_STYLE, &here, -1);
	if(here == fi->style)
	{
		fi->iter = *iter;
		fi->found = TRUE;
	}
	return fi->found;
}

/* 2009-03-24 -	Find the given style in the given store, and set the iter accordingly.
**		This is a straight tree search, so it takes a while, but this really
**		shouldn't be a problem.
*/
gboolean stl_styleinfo_tree_find_style(const StyleInfo *si, GtkTreeStore *store, const Style *style, GtkTreeIter *iter)
{
	struct find_info	fi;

	fi.style = style;
	fi.found = FALSE;
	gtk_tree_model_foreach(GTK_TREE_MODEL(store), cb_foreach, &fi);
	if(fi.found)
	{
		if(iter != NULL)
			*iter = fi.iter;
	}
	return fi.found;
}

/* 2008-05-11 -	Set the name of a style, based on a GtkTreeIter. This will change not only
 *		the value in the tree model, but also the style itself.
*/
void stl_styleinfo_set_name_iter(const StyleInfo *si, GtkTreeStore *store, GtkTreeIter *iter, const gchar *name)
{
	Style	*style;

	if((style = stl_styleinfo_get_style_iter(si, store, iter)) == NULL)
		return;
	stl_style_set_name(style, name);
	gtk_tree_store_set(store, iter, TREE_COLUMN_NAME, name, -1);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-19 -	Create a new, empty style named <name>. */
Style * stl_style_new(const gchar *name)
{
	Style	*stl;

	stl = g_malloc(sizeof *stl);
	g_strlcpy(stl->name, name, sizeof stl->name);
	stl->styleinfo = NULL;
	stl->properties = g_hash_table_new(g_str_hash, g_str_equal);
	stl->expand = TRUE;

	return stl;
}

/* 1999-05-26 -	Create a new style, taking care to choose a name not used in <si>.
** BUG BUG BUG	If you have 9999 styles called "New Style N", this might fail... Big "oops".
*/
Style * stl_style_new_unique_name(StyleInfo *si)
{
	if(si != NULL)
	{
		gchar	buf[STL_STYLE_NAME_SIZE];

		g_strlcpy(buf, ("New Style"), sizeof buf);
		if(stl_styleinfo_style_find(si, buf) != NULL)
		{
			guint	i = 1;

			do
			{
				g_snprintf(buf, sizeof buf, _("New Style %u"), i + 1);
				i++;
			} while((stl_styleinfo_style_find(si, buf) != NULL) && (i < 10000U));	/* This might fail. Big deal. */
		}
		return stl_style_new(buf);
	}
	return NULL;
}

/* 1999-05-20 -	Copy a property, storing the copy in the style pointed to by <user>. */
static void property_copy(gpointer key, gpointer value, gpointer user)
{
	StlProp	*pr = value;
	Style	*stl = user;

	if(pr->override)
	{
		switch(pr->value->type)
		{
			case SPT_COLOR:
				stl_style_property_set_color(stl, pr->name, &pr->value->data.color);
				break;
			case SPT_ICON:
				stl_style_property_set_icon(stl, pr->name, pr->value->data.icon);
				break;
			case SPT_ACTION:
				stl_style_property_set_action(stl, pr->name, pr->value->data.action->str);
				break;
		}
	}
}

/* 1999-05-19 -	Copy a style, creating a non-memory-sharing duplicate. Note that we only
**		copy overriden properties, relying on the fact that if the copy is going
**		to be used (i.e., added to a styleinfo), its inheritance will be computed.
*/
Style * stl_style_copy(Style *stl)
{
	Style	*ns = NULL;

	if(stl != NULL)
	{
		ns = stl_style_new(stl->name);
		ns->expand = stl->expand;
		g_hash_table_foreach(stl->properties, property_copy, ns);
	}
	return ns;
}

/* 1999-05-24 -	Return style from a tree selection (<wid> should be the widget passed to
**		the select handler used by stl_styleinfo_build().
*/
Style * stl_style_get(GtkWidget *wid)
{
	if(wid != NULL)
		return g_object_get_data(G_OBJECT(wid), "user");
	return NULL;
}

/* 1999-05-22 -	Set the expand status of a style. Used when building GUI representation. */
void stl_style_set_expand(Style *stl, gboolean expand)
{
	if(stl != NULL)
		stl->expand = expand;
}

/* 1999-05-22 -	Get the current expand/collapse status of given style. */
gboolean stl_style_get_expand(Style *stl)
{
	if(stl != NULL)
		return stl->expand;
	return FALSE;
}

/* 1999-05-24 -	Rename a style. */
void stl_style_set_name(Style *stl, const gchar *name)
{
	if((stl != NULL) && (name != NULL))
		g_strlcpy(stl->name, name, sizeof stl->name);
}

/* 1999-05-20 -	Just get a pointer (read-only, as usual) to the style's name. */
const gchar * stl_style_get_name(Style *stl)
{
	if(stl != NULL)
		return stl->name;
	return NULL;
}

/* 1999-06-12 -	Get the branch that begins with the root style and ends with <stl>. */
static GSList * style_get_branch(const Style *stl)
{
	GNode	*node;

	if((node = styleinfo_style_get_node(stl->styleinfo, stl)) != NULL)
	{
		GSList	*l = NULL;

		for(; node != NULL; node = node->parent)
			l = g_slist_prepend(l, ((Style *) node->data)->name);
		return l;
	}
	return NULL;
}

/* 1999-05-20 -	Compare hierarchial names of <stla> and <stlb>. Used by dirpane module,
**		to sort on styles. Comparison is done so that styles with common parents
**		are grouped.
*/
gint stl_style_compare_hierarchy(const Style *stla, const Style *stlb)
{
	GSList	*la, *lb;

	if(stla == stlb)		/* Quickly determine if they're the same. */
		return 0;

	if((la = style_get_branch(stla)) != NULL)
	{
		gint	sd = -1;
		if((lb = style_get_branch(stlb)) != NULL)
		{
			GSList	*ia, *ib;

			for(ia = la, ib = lb; (ia != NULL) && (ib != NULL) && ((sd = strcmp(ia->data, ib->data)) == 0);)
			{
				ia = g_slist_next(ia);
				ib = g_slist_next(ib);
			}
			if((ia == NULL) || (ib == NULL))
				sd = (ia == NULL) ? -1 : 1;
			g_slist_free(lb);
		}
		g_slist_free(la);

		return sd;
	}
	return 0;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-19 -	Create a new value structure, init it as the given type, and return a pointer to it. */
static StlPVal * value_new(StlPType type)
{
	StlPVal	*val;

	val = g_malloc(sizeof *val);
	val->type = type;
	switch(type)
	{
		case SPT_COLOR:
			val->data.color.red   = 0U;
			val->data.color.green = 0U;
			val->data.color.blue  = 0U;
			val->data.color.pixel = 0UL;
			break;
		case SPT_ICON:
			val->data.icon[0] = '\0';
			break;
		case SPT_ACTION:
			val->data.action = g_string_new("");
			break;
	}
	return val;
}

/* 1999-05-19 -	Destroy a value. */
static void value_destroy(StlPVal *val)
{
	if(val != NULL)
	{
		switch(val->type)
		{
			case SPT_COLOR:
			case SPT_ICON:
				break;
			case SPT_ACTION:
				g_string_free(val->data.action, TRUE);
				break;
		}
		g_free(val);
	}
}

/* 1999-05-20 -	Get a pointer to an overriding property named <name> in <stl>, whose type
**		is <type>. If no such property exists, create it.
*/
static StlProp * style_get_property(Style *stl, const gchar *name, StlPType type)
{
	StlProp	*pr;

	if((pr = g_hash_table_lookup(stl->properties, name)) != NULL)
	{
		if(pr->override)
		{
			if(pr->value->type == type)
				return pr;
			/* Only destroy if overriden, else we shared pointer. */
			value_destroy(pr->value);
		}
	}
	else
	{
		pr = g_malloc(sizeof *pr);
		g_strlcpy(pr->name, name, sizeof pr->name);
		g_hash_table_insert(stl->properties, pr->name, pr);
	}
	pr->override = TRUE;
	pr->value    = value_new(type);

	return pr;
}

/* 1999-05-19 -	Destroy a property, freeing all memory occupied by it. When this is called,
**		the property better NOT be still held in a style's hash table!
*/
static void style_property_destroy(StlProp *pr)
{
	if(pr != NULL)
	{
		if(pr->override)
			value_destroy(pr->value);
		g_free(pr);
	}
}

/* 1999-05-19 -	Set a new color for a color property. */
void stl_style_property_set_color(Style *stl, const gchar *property, const GdkColor *value)
{
	StlProp	*pr;

	if((stl != NULL) && (value != NULL))
	{
		pr = style_get_property(stl, property, SPT_COLOR);
		pr->value->data.color = *value;
		styleinfo_update(stl->styleinfo);
	}
}

/* 1999-05-19 -	Set a color property, using an RGB triplet. Sometimes convenient. */
void stl_style_property_set_color_rgb(Style *stl, const gchar *property, guint16 red, guint16 green, guint16 blue)
{
	GdkColor	color;

	color.red = red;
	color.green = green;
	color.blue = blue;
	color.pixel = 0UL;
	stl_style_property_set_color(stl, property, &color);
}

/* 1999-05-19 -	Set the name of an icon-property. */
void stl_style_property_set_icon(Style *stl, const gchar *property, const gchar *value)
{
	StlProp	*pr;

	if((stl != NULL) && (value != NULL))
	{
		pr = style_get_property(stl, property, SPT_ICON);
		g_strlcpy(pr->value->data.icon, value, sizeof pr->value->data.icon);
		styleinfo_update(stl->styleinfo);
	}
}

/* 1999-05-19 -	Set an action property to a new string. Note that empty strings are not allowed. */
void stl_style_property_set_action(Style *stl, const gchar *property, const gchar *value)
{
	StlProp	*pr;

	if((stl != NULL) && (value != NULL))
	{
		pr = style_get_property(stl, property, SPT_ACTION);
		g_string_assign(pr->value->data.action, value);
		styleinfo_update(stl->styleinfo);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-19 -	Get a read-only pointer to the color held in a color-property. */
const GdkColor * stl_style_property_get_color(const Style *stl, const gchar *property)
{
	StlProp	*pr;

	if((stl != NULL) && (property != NULL) && ((pr = g_hash_table_lookup(stl->properties, property)) != NULL))
	{
		if(pr->value->type == SPT_COLOR)
			return &pr->value->data.color;
	}
	return NULL;
}

/* 1999-05-19 -	Get a (read-only) icon name of an appropriately typed property. */
const gchar * stl_style_property_get_icon(const Style *stl, const gchar *property)
{
	StlProp	*pr;

	if((stl != NULL) && (property != NULL) && ((pr = g_hash_table_lookup(stl->properties, property)) != NULL))
	{
		if(pr->value->type == SPT_ICON)
			return pr->value->data.icon;
	}
	return NULL;
}

/* 1999-05-19 -	Get a (read-only, dammit) pointer to an action property definition. */
const gchar * stl_style_property_get_action(const Style *stl, const gchar *property)
{
	StlProp	*pr;

	if((stl != NULL) && (property != NULL) && ((pr = g_hash_table_lookup(stl->properties, property)) != NULL))
	{
		if(pr->value->type == SPT_ACTION)
			return pr->value->data.action->str;
	}
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-24 -	Get the override status (which tells if the property is local or inherited) for
**		a named property.
*/
gboolean stl_style_property_get_override(const Style *stl, const gchar *property)
{
	StlProp	*pr;

	if((stl != NULL) && (property != NULL) && ((pr = g_hash_table_lookup(stl->properties, property)) != NULL))
		return pr->override;
	return FALSE;
}

/* ----------------------------------------------------------------------------------------- */

static void get_action(gpointer key, gpointer value, gpointer user)
{
	GList	**list = user;
	StlProp	*prop = value;

	if(prop->value->type == SPT_ACTION)
		*list = g_list_insert_sorted(*list, prop->name, (GCompareFunc) strcmp);
}

/* 1999-05-24 -	Return a list of (very read-only) action property names. The list is valid
**		until the next person sneezes, or someone calls property_set() or _remove().
**		The list can be freed with a simple call to g_list_free().
*/
GList * stl_style_property_get_actions(const Style *stl)
{
	GList	*list = NULL;

	if(stl != NULL)
		g_hash_table_foreach(stl->properties, get_action, &list);
	return list;
}

gboolean stl_style_property_has_action(const Style *stl, const gchar *action)
{
	StlProp	*p;

	if((p = g_hash_table_lookup(stl->properties, action)) != NULL)
	{
		if(p->value->type == SPT_ACTION)
			return TRUE;
	}
	return FALSE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-27 -	Another g_hash_table_foreach() callback. This one checks for overrides. */
static void check_override(gpointer key, gpointer value, gpointer user)
{
	gboolean	*overrides = user;

	if(((StlProp *) value)->override)
		*overrides = TRUE;
}

/* 1999-05-27 -	Return TRUE if the given style overrides any property, FALSE if all it does
**		is inherit them. Handy when saving, to avoid empty "<Properties></Properties>"
**		stuff.
*/
gboolean stl_style_property_overrides(Style *stl)
{
	gboolean	overrides = FALSE;

	if(stl != NULL)
		g_hash_table_foreach(stl->properties, check_override, &overrides);

	return overrides;
}

/* 1999-05-25 -	Answer whether the named <property> is unique in the style tree. If a property
**		is unique, deleting it will truly delete the property. If it's not, deleting
**		it will just replace the value with an inherited value. This distinction is not
**		very interesting in itself, but it helps in user interface design. :)
*/
gboolean stl_style_property_is_unique(Style *stl, const gchar *property)
{
	if((stl != NULL) && (property != NULL))
	{
		GNode	*node;

		if((node = styleinfo_style_get_node(stl->styleinfo, stl)) != NULL)
		{
			for(node = node->parent; node != NULL; node = node->parent)
			{
				if(g_hash_table_lookup(((Style *) node->data)->properties, property))
					return FALSE;
			}
			return TRUE;
		}
	}
	return FALSE;
}

/* 2009-04-22 -	Major rewrite. This doesn't actually do a rename; it does a delete of all non-overriden
**		properties of the correct name. These, by definition, have the same value as the property
**		in the parent, so no user-owned information is lost.
*/
static void traverse_property_rename(GNode *node, gpointer user)
{
	struct property_rename_data	*data = user;
	Style				*here = node->data;
	const StlProp			*pr;

	if((pr = g_hash_table_lookup(here->properties, data->name_old)) != NULL)
	{
		/* If this child overrides the property, abort. */
		if(pr->override)
			return;
		stl_style_property_remove(here, data->name_old);
		g_node_children_foreach(node, G_TRAVERSE_ALL, traverse_property_rename, data);
	}
}

/* 1999-05-25 -	Rename <property> to <new_name>. If there is already a property with that name
**		in the given <stl>, rename will fail and return FALSE. Else, TRUE is returned.
** 2004-02-24 -	This was broken. It must recurse all child Styles, and let the renaming take
**		effect in (all) child styles, or there will be "orphan" properties left about.
*/
gboolean stl_style_property_rename(Style *stl, const gchar *property, const gchar *new_name)
{
	StlProp	*pr;
	GNode	*me;

	if(stl == NULL || property == NULL || new_name == NULL)
		return FALSE;
	if(g_hash_table_lookup(stl->properties, new_name) != NULL)
		return FALSE;
	if((pr = g_hash_table_lookup(stl->properties, property)) == NULL)
		return FALSE;
	if(!pr->override)
	{
		fprintf(stderr, "**Styles: can't rename non-overriden property '%s'\n", property);
		return FALSE;
	}
	/* Remove from hash table, rename, and re-insert. */
	g_hash_table_remove(stl->properties, pr->name);
	g_strlcpy(pr->name, new_name, sizeof pr->name);
	g_hash_table_insert(stl->properties, pr->name, pr);
	/* Name changed. Now traverse children, and remove non-overridden instances. */
	if((me = g_node_find(stl->styleinfo->styles, G_PRE_ORDER, G_TRAVERSE_ALL, stl)) != NULL)
	{
		struct property_rename_data	data;

		data.style = stl;
		g_snprintf(data.name_old, sizeof data.name_old, "%s", property);
		g_snprintf(data.name_new, sizeof data.name_new, "%s", new_name);
		g_node_children_foreach(me, G_TRAVERSE_ALL, traverse_property_rename, &data);
	}
	/* Finally re-inherit stuff, if our override was of a parent's property we will now get it "back". */
	styleinfo_update(stl->styleinfo);

	return TRUE;
}

/* 1999-05-19 -	Remove (and destroy) a property from a style. */
void stl_style_property_remove(Style *stl, const gchar *property)
{
	StlProp	*pr;

	if((stl != NULL) && ((pr = g_hash_table_lookup(stl->properties, property)) != NULL))
	{
		g_hash_table_remove(stl->properties, property);
		style_property_destroy(pr);
		styleinfo_update(stl->styleinfo);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-20 -	A g_hash_table_foreach_remove() callback. Kill a property. */
static gboolean destroy_property(gpointer key, gpointer value, gpointer user)
{
	style_property_destroy(value);

	return TRUE;		/* Really remove. */
}

/* 1999-05-20 -	Destroy a style. If <unlink> is TRUE, it will also remove the style from
**		the styleinfo, thus causing any children to <stl> to be destroyed as well.
**		This is really recommended, as leaving a styleinfo referencing destroyed
**		styles hanging around is likely to be unhealthy.
*/
void stl_style_destroy(Style *stl, gboolean unlink)
{
	if(stl != NULL)
	{
		if(unlink)
			stl_styleinfo_style_remove(stl->styleinfo, stl);
		g_hash_table_foreach_remove(stl->properties, destroy_property, NULL);
		g_hash_table_destroy(stl->properties);
		g_free(stl);
	}
}
