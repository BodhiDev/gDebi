/*
** 1998-08-15 -	A small utility module that deals with icons. Serves two main purposes:
**		1) Provides name-to-pixmap lookups, and thus a form of caching, and
**		2) Allows the search paths used to locate icon pixmaps to be abstracted
**		   somewhat. Nice.
**		This module is mainly used by e.g. dpformat, when it needs to render
**		icon pixmaps.
** 1998-08-30 -	Added function to seek out all icons in a path. Also added a built-in
**		inline pixmap which is used when a named pixmap can't be found.
** 1999-08-28 -	Started using the gdk_pixmap_colormap_create_from_xpm_d() function (whose
**		name really kind of rolls off your tongue, doesn't it?). Simplified things.
** 1999-11-21 -	Rewrote the ico_flush() function, to use g_hash_table_foreach_remove(),
**		which made it a lot shorter and cleaner. Good.
** 2010-02-22 -	Time flies. Removed old-style pixmap+mask representations, now always uses
**		the more 2.0-y GdkPixbufs all over the place.
*/

#include "gentoo.h"

#include <stdlib.h>

#include "fileutil.h"
#include "strutil.h"
#include "iconutil.h"

/* ----------------------------------------------------------------------------------------- */

/* This name is reserved to mean "a blank icon". It is very easy
** to think of it as specifying that NO icon should be shown.
*/
#define	ICON_NONE	_("(None)")

#include "graphics/icon_empty.xpm"
#include "graphics/icon_failed.xpm"

/* ----------------------------------------------------------------------------------------- */

struct IconInfo {
	GHashTable	*icon;			/* Provides name-to-pixmap lookup. */
	const gchar	*path;			/* A local copy of the PTID_ICONS path. */
	GdkPixbuf	*empty;
	GdkPixbuf	*fail;
};

typedef struct {
	gchar		name[PATH_MAX];		/* This is real big. */
	GdkPixbuf	*pixbuf;
} IconGfx;

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-15 -	IconGfx comparison function for hash table. */
static gint cmp_icon(gconstpointer a, gconstpointer b)
{
	return strcmp(((const IconGfx *) a)->name, ((const IconGfx *) b)->name) == 0;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-31 -	Initialize icon utility module. */
IconInfo * ico_initialize(MainInfo *min)
{
	IconInfo	*ico;

	ico = g_malloc(sizeof *ico);

	ico->icon = g_hash_table_new(g_str_hash, cmp_icon);

	ico->empty = gdk_pixbuf_new_from_xpm_data((const char **) icon_empty_xpm);
	ico->fail = gdk_pixbuf_new_from_xpm_data((const char **) icon_failed_xpm);

	ico->path = min->cfg.path.path[PTID_ICON]->str;

	return ico;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-30 -	Filter out stuff not ending in ".xpm".
** 1998-09-02 -	Eased up the implementation, using the string utility module. I like one-liners.
*/
static gboolean icon_filter(const gchar *path, const gchar *name)
{
	return g_str_has_suffix(name, ".xpm") || g_str_has_suffix(name, ".png");
}

/* 1998-08-30 -	This returns a (possibly giant) list of all icon names in the given
**		icon <path>. If <path> is NULL, the current icon path (PTID_ICON) is used.
**		All path components are scanned for files ending in ".xpm"; these are assumed
**		to be icon pixmaps. The returned list has each element's <data> pointer pointing
**		at a dynamically allocated string which is the icon name. This memory tied up
**		by these strings is reclaimed by calling ico_free_all() on the list once you're
**		done with it.
*/
GList * ico_get_all(MainInfo *min, const gchar *path)
{
	GList	*list = NULL;
	gchar	*head;

	if(path == NULL)
		path = min->cfg.path.path[PTID_ICON]->str;

	if(path != NULL)
	{
		list = fut_scan_path(path, icon_filter);
		if((head = g_strdup(ICON_NONE)) != NULL)
			list = g_list_prepend(list, head);
	}
	return list;
}

gboolean ico_no_icon(const gchar *name)
{
	return strcmp(name, ICON_NONE) == 0;
}

/* 1998-08-30 -	Free a list of icon names as returned by ico_get_all() above. */
void ico_free_all(GList *list)
{
	if(list != NULL)
		fut_free_path(list);
}

/* ----------------------------------------------------------------------------------------- */

static IconGfx * load_pixbuf(IconInfo *ico, const gchar *name)
{
	const gchar	*fname;
	IconGfx		*ig;

	ig = g_malloc(sizeof *ig);
	g_strlcpy(ig->name, name, sizeof ig->name);
	if(strcmp(name, ICON_NONE) == 0)		/* Is it the special, reserved, blank icon? */
	{
		ig->pixbuf = ico->empty;
		return ig;
	}
	if((fname = fut_locate(ico->path, name)) != NULL)
	{
		if((ig->pixbuf = gdk_pixbuf_new_from_file(fname, NULL)) != NULL)
			return ig;
	}
	return NULL;
}

static GdkPixbuf * get_or_load_pixbuf(IconInfo *ico, const gchar *name)
{
	IconGfx	*ig;

	if(ico->icon == NULL)
		ico->icon = g_hash_table_new(g_str_hash, cmp_icon);
	if((ig = g_hash_table_lookup(ico->icon, name)) != NULL)
		return ig->pixbuf;
	if((ig = load_pixbuf(ico, name)) != NULL)
	{
		g_hash_table_insert(ico->icon, ig->name, ig);
		return ig->pixbuf;
	}
	return ico->fail;
}

/* ----------------------------------------------------------------------------------------- */

/* 2008-07-29 -	New interface, this returns a GDK Pixbuf which is far more useful in these
 *		times of GTK+ 2 than the old GdkPixmap/GdkBitmap combo.
*/
GdkPixbuf * ico_icon_get_pixbuf(MainInfo *min, const gchar *name)
{
	if(min == NULL || name == NULL || *name == '\0')
		return NULL;
	min->ico->path = min->cfg.path.path[PTID_ICON]->str;

	return get_or_load_pixbuf(min->ico, name);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-11-21 -	Kill given icon. This is a g_hash_table_foreach_remove() callback.
** 2001-01-01 -	Happy new year! :) Um, now frees the memory, too...
*/
static gboolean kill_icon(gpointer key, gpointer value, gpointer user)
{
	IconGfx	*gfx = value;

	g_object_unref(gfx->pixbuf);
	g_free(gfx);

	return TRUE;			/* Tells glib to actually remove hash entry. */
}

/* 1998-09-02 -	Flush all loaded icons out, thus making the next reference a load for sure. */
void ico_flush(MainInfo *min)
{
	if(min->ico->icon != NULL)
		g_hash_table_foreach_remove(min->ico->icon, kill_icon, NULL);
}
