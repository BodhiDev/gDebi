/*
** 1998-08-11 -	Header for the styles module. Surprise.
*/

#if !defined STYLES_H
#define	STYLES_H

#include "xmlutil.h"

/* Maximum number of guaranteed unique style name characters, plus 1. */
#define	STL_STYLE_NAME_SIZE	(32)
#define	STL_PROPERTY_NAME_SIZE	(32)

/* A string of characters NOT legal in style names. Not enforced by style module,
** but recommended nevertheless.
*/
#define	STL_STYLE_NAME_REJECT	"\t\n\\,.;:!\"#$¤%&/()=+*/<>[]{}'´~^"

typedef struct StyleInfo	StyleInfo;
typedef struct Style		Style;

/* A couple of standard property names. Has no real importance, but useful to minimize
** pain in going from the old hard-coded vector-based system to the new hash-based one.
*/
#define	SPN_COL_UNSEL_BG	"uBG"
#define	SPN_COL_UNSEL_FG	"uFG"
#define	SPN_ICON_UNSEL		"uIcon"
#define	SPN_ACTN_DEFAULT	"Default"
#define	SPN_ACTN_VIEW		"View"
#define	SPN_ACTN_EDIT		"Edit"
#define	SPN_ACTN_PRINT		"Print"
#define	SPN_ACTN_PLAY		"Play"

/* ----------------------------------------------------------------------------------------- */

StyleInfo *	stl_styleinfo_new(void);
StyleInfo *	stl_styleinfo_default(void);
StyleInfo *	stl_styleinfo_copy(StyleInfo *si);
void		stl_styleinfo_destroy(StyleInfo *si);
GtkTreeStore *	stl_styleinfo_build_partial(const StyleInfo *si, const Style *ignore);

Style *		stl_styleinfo_get_style_iter(const StyleInfo *si, GtkTreeStore *store, GtkTreeIter *iter);
gboolean	stl_styleinfo_tree_find_style(const StyleInfo *si, GtkTreeStore *store, const Style *style, GtkTreeIter *iter);
void		stl_styleinfo_set_name_iter(const StyleInfo *si, GtkTreeStore *store, GtkTreeIter *iter, const gchar *name);

void		stl_styleinfo_freeze(StyleInfo *si);
void		stl_styleinfo_thaw(StyleInfo *si);

void		stl_styleinfo_dump(StyleInfo *si);

void		stl_styleinfo_style_add(StyleInfo *si, Style *parent, Style *stl);
void		stl_styleinfo_style_remove(StyleInfo *si, Style *stl);
Style *		stl_styleinfo_style_find(const StyleInfo *si, const gchar *name);
Style *		stl_styleinfo_style_get_parent(StyleInfo *si, Style *stl);
void		stl_styleinfo_style_set_parent(StyleInfo *si, Style *stl, Style *new_parent);
Style *		stl_styleinfo_style_get_parent(StyleInfo *si, Style *stl);

gboolean	stl_styleinfo_style_siblings(StyleInfo *si, Style *stla, Style *stlb);

Style *		stl_styleinfo_style_root(const StyleInfo *si);
gboolean	stl_styleinfo_style_has_children(StyleInfo *si, Style *stl);
GList *		stl_styleinfo_style_get_children(StyleInfo *si, Style *stl, gboolean include_root);

void		stl_styleinfo_save(MainInfo *min, StyleInfo *si, FILE *out, const gchar *tag);
StyleInfo *	stl_styleinfo_load(const XmlNode *node);

Style *		stl_style_new(const gchar *name);
Style *		stl_style_new_unique_name(StyleInfo *si);
Style *		stl_style_copy(Style *stl);
Style *		stl_style_get(GtkWidget *wid);

void		stl_style_set_expand(Style *stl, gboolean expand);
gboolean	stl_style_get_expand(Style *stl);

void		stl_style_set_name(Style *stl, const gchar *name);
void		stl_style_set_name_widget(GtkWidget *wid, const gchar *name);
const gchar *	stl_style_get_name(Style *stl);
GString *	stl_style_get_name_full(Style *stl);

gint		stl_style_compare_hierarchy(const Style *stla, const Style *stlb);

/* Setting a non-existent property adds it to the style. Setting a property with
** the wrong kind of value will not do anything, but is stupid, so don't do that.
** The way to change a property's type is to first remove it, then set it again.
*/
void		stl_style_property_set_color(Style *stl, const gchar *property, const GdkColor *value);
void		stl_style_property_set_color_rgb(Style *stl, const gchar *property, guint16 red, guint16 green, guint16 blue);
void		stl_style_property_set_icon(Style *stl, const gchar *property, const gchar *value);
void		stl_style_property_set_action(Style *stl, const gchar *property, const gchar *value);
const GdkColor*	stl_style_property_get_color(const Style *stl, const gchar *property);
const gchar *	stl_style_property_get_icon(const Style *stl, const gchar *property);
const gchar *	stl_style_property_get_action(const Style *stl, const gchar *property);
gboolean	stl_style_property_get_override(const Style *stl, const gchar *property);
GList *		stl_style_property_get_actions(const Style *stl);
gboolean	stl_style_property_has_action(const Style *stl, const gchar *action);

gboolean	stl_style_property_overrides(Style *stl);
gboolean	stl_style_property_is_unique(Style *stl, const gchar *property);
gboolean	stl_style_property_rename(Style *stl, const gchar *property, const gchar *new_name);
void		stl_style_property_remove(Style *stl, const gchar *property);

void		stl_style_destroy(Style *stl, gboolean unlink);

#endif		/* STYLES_H */
