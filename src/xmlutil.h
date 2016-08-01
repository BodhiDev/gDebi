/*
** 1998-07-25 -	The obligatory header file for the XML utility module. The module provides
**		a way of reading in an XML file (um, at least I think it's XML) into a tree,
**		and then do various operations on that tree. Useful for configuration data.
*/

#if !defined XMLUTIL_H
#define	XMLUTIL_H

/* This is one of gentoo's few opaque datatypes. */
typedef struct XmlNode	XmlNode;

/* ----------------------------------------------------------------------------------------- */

extern FILE *		xml_put_open(const gchar *name);
extern void		xml_put_close(FILE *out);
extern void		xml_put_node_open(FILE *out, const gchar *node);
extern void		xml_put_node_close(FILE *out, const gchar *node);

extern void		xml_put_integer(FILE *out, const gchar *name, gint value);
extern void		xml_put_uinteger(FILE *out, const gchar *name, guint value);
extern void		xml_put_real(FILE *out, const gchar *name, gfloat value);
extern void		xml_put_boolean(FILE *out, const gchar *name, gboolean value);
extern void		xml_put_text(FILE *out, const gchar *name, const gchar *value);
extern void		xml_put_color(FILE *out, const gchar *name, const GdkColor *color);

extern gboolean		xml_get_integer(const XmlNode *node, const gchar *name, gint *n);
extern gboolean		xml_get_uinteger(const XmlNode *node, const gchar *name, guint *n);
extern gboolean		xml_get_real(const XmlNode *node, const gchar *name, gfloat *x);		/* ;^) */
extern gboolean		xml_get_boolean(const XmlNode *node, const gchar *name, gboolean *x);
extern gboolean		xml_get_text(const XmlNode *node, const gchar *name, const gchar **str);
extern gboolean		xml_get_text_copy(const XmlNode *node, const gchar *name, gchar *str, gsize max);
extern gboolean		xml_get_color(const XmlNode *node, const gchar *name, GdkColor *col);

extern XmlNode *	xml_node_new(const gchar *name);
extern gint		xml_node_has_name(const XmlNode *node, const gchar *name);
extern void		xml_node_add_child(XmlNode *node, XmlNode *child);
extern void		xml_node_visit_children(const XmlNode *node, void (*func)(const XmlNode *child, gpointer user_data),
						gpointer user_data);
extern void		xml_node_destroy(XmlNode *node);

extern XmlNode *	xml_tree_load(const gchar *name);
extern const XmlNode *	xml_tree_search(const XmlNode *root, const gchar *name);
extern void		xml_tree_destroy(XmlNode *node);

#endif		/* XMLUTIL_H */
