/*
** 1998-06-22 -	A generic header file to be included by all the modules that
**		implement gdtool's GUI config system. Loads of code.
*/

#if !defined CFG_MODULE_H
#define	CFG_MODULE_H

#include "xmlutil.h"

/* This gets called when the main config system wants this page to create
** itself. Should return a "root" widget for the page (some GTK+ container).
*/
typedef	GtkWidget *	(*CMInitFunc)(MainInfo *min, gchar **name);

/* Kindly update yourself, since we are about to redisplay the config GUI. */
typedef void		(*CMUpdateFunc)(MainInfo *min);

/* This gets called as the user clicks the OK button in the config GUI. It
** should make the necessary changes to stuff in min->cfg. Optimally, the
** only thing done is a memcpy() or so... Dream on.
*/
typedef void		(*CMAcceptFunc)(MainInfo *min);

/* This gives the page a chance to save itself out on the (open) stream
** <out>. It gets called as the user clicks the Save button in the GUI,
** _before_ any calls to cfg_accept() and cfg_hide().
*/
typedef gint		(*CMSaveFunc)(MainInfo *min, FILE *out);

/* Make the page parse its data from the XML tree node <node>, which has
** been identified as belonging to this page by a string match between
** the node's name and the page's node identifier.
*/
typedef void		(*CMLoadFunc)(MainInfo *min, const XmlNode *node);

/* This gets called as the config window "closes". Modules should NOT use
** this opportunity to destroy their GUI, since that should not be
** necessary. It can, however, be useful to get a chance to free any
** extraneous data used by the module during config editing.
*/
typedef void		(*CMHideFunc)(MainInfo *min);

/* A collection of function pointers that define the interface to a
** configuration page.
*/
typedef struct {
	const gchar	*node;		/* Name of this module's root config node. */
	CMInitFunc	init;
	CMUpdateFunc	update;
	CMAcceptFunc	accept;
	CMSaveFunc	save;
	CMLoadFunc	load;
	CMHideFunc	hide;
} CfgModule;

/* This gets called by the main config GUI code every time it opens. The
** module should just return a pointer to an instance of the above struct,
** whose fields the main cfg can then access at will.
*/
typedef const CfgModule *	(*CMDescribeFunc)(MainInfo *min);

#endif			/* CFG_MODULE_H */
