/*
** 1998-11-26 -	A neat little module to manage directory histories in the panes. Initially, this
**		will just remember the actual paths, but future versions might include storing
**		info about selected files, sizes, and so on...
** 1998-12-06 -	Rewritten after a suggestion from J. Hanson (<johan@tiq.com>), to simply hash on
**		the inode numbers rather than the file names. Should reduce time and memory complex-
**		ities by a whole lot.
** 1998-12-15 -	Now stores the vertical position in a relative way, rather than using absolute pixel
**		coordinates. Not good, but better.
** 1998-12-23 -	Eh. Inode numbers are _not_ unique across devices (which might be why there's
**		a st_dev field in the stat structure). This of course makes them a bad choice
**		for unique file identifiers - when buffering a directory containing inodes from
**		various devices (such as / typically does), things went really haywire. To fix
**		this, we now store the device identifier too. The pair (device,inode) really should
**		be unique.
** 1999-09-05 -	Cut away the fancy browser-esque history system implemented for 0.11.8, since it wasn't
**		complete, and I didn't like it much. Going back to a simple combo as before.
** 1999-11-13 -	Prettied up handling of vertical position remembering somewhat, exported it separately.
** 2002-08-09 -	DHEntries can't be keyed on inode, since that can be fooled by deleting a directory,
**		and then creating a new (differently named) one. The filesystem can re-use the inode.
** 2008-01-26 -	Adapted to keep a separate UTF-8 copy of the path names, for GTK+ 2. Ugly, needs to
**		be way better. Later? Heh.
** 2010-02-09 -	Again, adapted to work with GIO. Assume less, use thicker gloves. Smooth.
*/

#include "gentoo.h"

#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "cmdseq.h"
#include "dirpane.h"
#include "errors.h"
#include "fileutil.h"
#include "strutil.h"
#include "xmlutil.h"

#include "dirhistory.h"

/* ----------------------------------------------------------------------------------------- */

#define	HISTORY_SIZE	(16)	/* Maximum number of items in history. Should this be dynamic? */

/* ----------------------------------------------------------------------------------------- */

/* This is used to store the selected files in a pane, and nothing else. */
struct DHSel {
	GHashTable	*hash;		/* It's a hash of display file names, for fast apply(). */
	GStringChunk	*chunk;		/* All keys in the hash are stored here. */
};

/* Info about a single remembered directory. The 'hist' field of dirpanes stores a list of these. */
typedef struct {
	gchar	*parse_name;		/* The (VFs-friendly) "parse name" of the remembered directory. */
	gfloat	vpos;			/* Vertical position. */
	DHSel	sel;			/* Selection, inlined but empty by default. */
} DHEntry;

struct DirHistory {
	GList	*history;		/* Visited directories. Recent are close to head. */
};

/* ----------------------------------------------------------------------------------------- */

/* This is just a ballpark, for the average name of a GIO "parse name". Not critical. */
static const gsize	NAME_LENGTH_AVERAGE = 64;

static void	dirsel_new_in_place(DHSel *sel);
static void	dirsel_set(DHSel *sel, const DirPane *dp);
static void	dirsel_clear(DHSel *sel);

/* ----------------------------------------------------------------------------------------- */

/* 1999-06-08 -	Create a new dirpane history structure. */
DirHistory * dph_dirhistory_new(void)
{
	DirHistory	*dh;

	dh = g_malloc(sizeof *dh);
	dh->history = NULL;

	return dh;
}

/* 2004-02-25 -	Get the entry with the given <index>, where 0 is the index of the most recent one. */
const gchar * dph_dirhistory_get_entry(const DirHistory *dh, guint index)
{
	if(dh != NULL)
	{
		const DHEntry   *dhe = g_list_nth_data(dh->history, index);
 		if(dhe != NULL)
			return dhe->parse_name;
	}
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-05 -	Create a new empty DHEntry structure. */
static DHEntry * dhentry_new(void)
{
	DHEntry	*data;

	data = g_malloc(sizeof *data);
	data->parse_name = NULL;
	data->vpos = 0.f;
	/* Lazily assume there will be no actual selection to store. */
	data->sel.hash = NULL;
	data->sel.chunk = NULL;

	return data;
}

static void dhentry_root_set(DHEntry *dhe, GFile *root)
{
	if(dhe == NULL || root == NULL)
		return;
	if(dhe->parse_name != NULL)
		g_free(dhe->parse_name);
	dhe->parse_name = g_file_get_parse_name(root);
}

/* 1999-06-09 -	Update <dhe> to mimic state of <dp>. */
static void dhentry_update(DHEntry *dhe, const DirPane *dp)
{
	if(dp->main->cfg.dp_history.select)
	{
		if(dhe->sel.hash == NULL)
			dirsel_new_in_place(&dhe->sel);
		dirsel_set(&dhe->sel, dp);
	}
	else
		dirsel_clear(&dhe->sel);
	dhe->vpos = dph_vpos_get(dp);
}

/* 1999-06-09 -	Apply state conserved in <dhe> to the rows of <dp>. */
static void dhentry_apply(const DHEntry *dhe, DirPane *dp)
{
	dph_dirsel_apply(dp, &dhe->sel);
	dph_vpos_set(dp, dhe->vpos);
}

/* 1999-06-06 -	Destroy a directory history entry. */
static void dhentry_destroy(DHEntry *dh)
{
	if(dh->sel.hash != NULL)
	{
		g_hash_table_destroy(dh->sel.hash);
		g_string_chunk_free(dh->sel.chunk);
	}
	g_free(dh);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-05 -	Create a new empty selection. */
static void dirsel_new_in_place(DHSel *sel)
{
	if(sel == NULL)
		return;

	sel->hash = g_hash_table_new(g_str_hash, g_str_equal);
	sel->chunk = g_string_chunk_new(NAME_LENGTH_AVERAGE);
}

static void dirsel_clear(DHSel *sel)
{
	if((sel == NULL) || (sel->hash == NULL))
		return;
	g_hash_table_remove_all(sel->hash);
	g_string_chunk_clear(sel->chunk);
}

/* 1999-03-05 -	Given an <sel> structure, replace selection with that of <dp>.
** 2010-02-09 -	Significant edits for GIO-adapted data structures.
*/
static void dirsel_set(DHSel *sel, const DirPane *dp)
{
	GSList	*slist;

	/* Always clear; if there is no selection clearing is what we want. */
	dirsel_clear(sel);
	if((slist = dp_get_selection_full(dp)) != NULL)
	{
		const GSList	*iter;
		GtkTreeModel	*model = dp_get_tree_model(dp);
		gsize		count = 0, length = 0;

		for(iter = slist; iter != NULL; iter = g_slist_next(iter))
		{
			const gchar	*name = dp_row_get_name_display(model, iter->data);
			gchar		*key;

			key = g_string_chunk_insert(sel->chunk, name);
			g_hash_table_insert(sel->hash, key, key);	/* *Value* is returned on lookup!! */
			length += strlen(key);
			count++;
		}
		dp_free_selection(slist);
	}
}

/* 1999-03-05 -	This returns an opaque representation of all selected rows of <dp>. The
**		selection is not related to the order in which these rows are displayed
**		in the pane, so it's handy to use before e.g. resorting the pane.
** 2010-02-09 -	Changed around a bit to match the new definition of DHSel. No longer OK
**		to return NULL to represent "no selection".
*/
DHSel * dph_dirsel_new(DirPane *dp)
{
	DHSel	*sel;

	sel = g_malloc(sizeof *sel);
	dirsel_new_in_place(sel);
	dirsel_set(sel, dp);

	return sel;
}

/* 1999-03-05 -	Apply given given <sel> selection to <dp>, making those rows selected
**		again. <dp> need not be the same as when the selection was created,
**		and it need not have the same contents. This is not terribly efficient,
**		but I think it'll be OK.
*/
void dph_dirsel_apply(DirPane *dp, const DHSel *sel)
{
	GtkTreeModel	*model;
	DirRow2		iter;

	if((sel == NULL) || (sel->hash == NULL))
		return;

	model = dp_get_tree_model(dp);
	if(gtk_tree_model_get_iter_first(model, &iter))
	{
		do
		{
			const gchar	*name;

			name = dp_row_get_name(model, &iter);
			if(g_hash_table_lookup(sel->hash, name) != NULL)
				dp_select(dp, &iter);
		} while(gtk_tree_model_iter_next(model, &iter));
	}
}

/* 1999-03-05 -	Destroy a selection, this is handy when you're done with it (like after
**		having applied it).
*/
void dph_dirsel_destroy(DHSel *sel)
{
	if(sel == NULL)
		return;

	if(sel->hash != NULL)
	{
		g_hash_table_destroy(sel->hash);
		g_string_chunk_free(sel->chunk);
	}
	g_free(sel);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-11-13 -	Return a floating-point number that somehow encodes the vertical position of
**		the given pane. The only use for that number is to pass it to dph_vpos_set().
*/
gfloat dph_vpos_get(const DirPane *dp)
{
	GtkAdjustment	*adj;

	if((adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(dp->scwin))) != NULL)
		return gtk_adjustment_get_value(adj);
	return -1.0f;
}

/* 1999-11-13 -	Set the given pane's vertical position to resemble what is was when <vpos>
**		was returned by dph_vpos_get().
*/
void dph_vpos_set(DirPane *dp, gfloat vpos)
{
	GtkAdjustment	*adj;

	if((vpos >= 0.0f) && (adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(dp->scwin))) != NULL)
		gtk_adjustment_set_value(adj, vpos);
}

/* ----------------------------------------------------------------------------------------- */

/* 2002-06-13 -	A new way to look at history handling. For the, like, third time or so. Anyway,
**		this should just save the state of <dp>. This means the pane should already be
**		displaying a directory.
*/
void dph_state_save(DirPane *dp)
{
	gchar	*name;

	if(dp->dir.root == NULL)
		return;

	name = g_file_get_parse_name(dp->dir.root);
	if(name == NULL)
		return;

	if((dp->hist->history == NULL) || strcmp(name, ((DHEntry *) dp->hist->history->data)->parse_name))
	{
		dp->hist->history = g_list_prepend(dp->hist->history, dhentry_new());
		dhentry_root_set(dp->hist->history->data, dp->dir.root);
	}
	dhentry_update(dp->hist->history->data, dp);
	g_free(name);
}

/* 2002-06-13 -	Other half of the third-generation history interface. This restores any saved
**		state for the directory currently shown by <dp>.
*/
void dph_state_restore(DirPane *dp)
{
	GList	*iter;
	gchar	*name;
	DHEntry	*entry;

	/* Search for key matching current dir in history. */
	if((name = g_file_get_parse_name(dp->dir.root)) == NULL)
		return;
	for(iter = dp->hist->history, entry = NULL; iter != NULL; iter = g_list_next(iter))
	{
		entry = iter->data;
		if(strcmp(name, entry->parse_name) == 0)
			break;
	}
	g_free(name);

	if(iter != NULL)
	{
		dhentry_apply(entry, dp);
		if(g_list_previous(iter) != NULL)
		{
			dp->hist->history = g_list_remove_link(dp->hist->history, iter);
			g_list_free_1(iter);
			dp->hist->history = g_list_prepend(dp->hist->history, entry);
		}
	}
	else
	{
		dp->hist->history = g_list_prepend(dp->hist->history, dhentry_new());
		dhentry_root_set(dp->hist->history->data, dp->dir.root);
		dhentry_apply(dp->hist->history->data, dp);
		/* Prune history list, if it has grown too long. */
		if(g_list_length(dp->hist->history) > HISTORY_SIZE)
		{
			GList	*tail, *next;

			tail = g_list_nth(dp->hist->history, HISTORY_SIZE);
			for(; tail != NULL; tail = next)
			{
				next = g_list_next(tail);
				dp->hist->history = g_list_remove_link(dp->hist->history, tail);
				dhentry_destroy(tail->data);
				g_list_free_1(tail);
			}
		}
	}
	/* Update path history widgets. Relies on <parse_name> member being first in DHEntry. */
	dp_history_set(dp, dp->hist->history);
}

/* ----------------------------------------------------------------------------------------- */

/* 2010-02-09 -	Gets the parse name from an indirect pointer, such as those handed (in a
**		list) to dp_history_set(), above.
*/
const gchar * dph_entry_get_parse_name(const void *entry)
{
	const DHEntry *	e = entry;

	if(e == NULL)
		return NULL;
	return e->parse_name;
}

/* 2002-02-15 -	Return first path from history for <dp>. */
const gchar * dph_history_get_first(const DirPane *dp)
{
	if(dp->hist->history != NULL)
		return dp->hist->history->data;
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

static const gchar * history_filename(MainInfo *min)
{
	static gchar	buffer[PATH_MAX];
	const gchar	*confdir;

	if((confdir = g_get_user_config_dir()) != NULL)
	{
		g_snprintf(buffer, sizeof buffer, "%s" G_DIR_SEPARATOR_S PACKAGE G_DIR_SEPARATOR_S "dirhistory", confdir);
		return buffer;
	}
	return NULL;
}

/* 2002-02-15 -	Save contents of history lists for <num> panes, arrayed at <dp>. Weird inter-
**		face, good code. In a blatant case of code reuse, we use our trusted XML module.
**		We do not, however, reuse the main config file, but keep the histories separate.
*/
void dph_history_save(MainInfo *min, const DirPane *dp, gsize num)
{
	FILE	*out;
	gsize	i;
	GList	*iter;

	if((out = xml_put_open(history_filename(min))) == NULL)
		return;

	xml_put_node_open(out, "DirHistory");
	xml_put_node_open(out, "Panes");
	for(i = 0; i < num; i++)
	{
		xml_put_node_open(out, "Pane");
		xml_put_uinteger(out, "index", i);
		xml_put_node_open(out, "History");
		for(iter = dp[i].hist->history; iter != NULL; iter = g_list_next(iter))
		{
			const DHEntry	*entry = iter->data;

			if(entry->parse_name[0] == '\0')
				continue;
			xml_put_text(out, "root", entry->parse_name);
		}
		xml_put_node_close(out, "History");
		xml_put_node_close(out, "Pane");
	}
	xml_put_node_close(out, "Panes");
	xml_put_node_close(out, "DirHistory");

	xml_put_close(out);
}

/* ----------------------------------------------------------------------------------------- */

static void load_dir(const XmlNode *node, gpointer user)
{
	DirPane		*dp = user;
	const gchar	*text;
	DHEntry		*ent;

	if(!xml_get_text(node, "root", &text))
		return;
	if(text && *text == '\0')
		return;
	ent = dhentry_new();
	ent->parse_name = g_strdup(text);
	dp->hist->history = g_list_append(dp->hist->history, ent);
}

static void load_pane(const XmlNode *node, gpointer user)
{
	MainInfo	*min = user;
	guint		index;

	if(!xml_get_uinteger(node, "index", &index))
		return;
	if((node = xml_tree_search(node, "History")) == NULL)
		return;
	xml_node_visit_children(node, load_dir, min->gui->pane + index);
}

void dph_history_load(MainInfo *min, DirPane *dp, gsize num)
{
	XmlNode		*tree;
	const XmlNode	*node;

	if((tree = xml_tree_load(history_filename(min))) == NULL)
		return;

	if((node = xml_tree_search(tree, "DirHistory")) == NULL)
		return;
	if((node = xml_tree_search(node, "Panes")) == NULL)
		return;

	xml_node_visit_children(node, load_pane, min);
	xml_tree_destroy(tree);
}
