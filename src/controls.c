/*
** 1999-03-16 -	A module to deal with various forms of user controls. Currently, this
**		will simply implement global keyboard shortcuts. In future, this might
**		be the place to add configuration of mouse buttons, and stuff.
** 1999-05-08 -	Minor changes due to the fact that command sequences are now best stored
**		as GStrings.
** 2000-02-18 -	Completed implementation of new NumLock-ignore support. Should help users.
*/

#include "gentoo.h"

#include "strutil.h"

#include "controls.h"

/* ----------------------------------------------------------------------------------------- */

struct CtrlInfo {
	MainInfo	*min;			/* So incredibly handy to have around. */

	gboolean	nonumlock;		/* Ignore num lock for both keyboard and mouse bindings? */

	GHashTable	*keys;			/* Contains all key-to-cmdseq bindings. */
	GSList		*keys_inst;		/* List of KbdContexts into which we're currently installed. */

	GList		*mouse;			/* List of CtrlMouse mouse button mappings. Why hurry? */

	struct {
	GString		*cmdseq;		/* Command assigned to Click-M-Click action gesture. */
	gfloat		delay;			/* Max delay in seconds to trigger cmc. */
	}		clickmclick;

	GHashTable	*general;		/* General bindings. */
};

struct CtrlKey {
	gchar	keyname[KEY_NAME_SIZE];
	GString	*cmdseq;
};

struct CtrlMouse {
	guint	button;			/* Which button this is for (in range 1..5). */
	guint	state;			/* Modifier bits that need to be set (see GdkModifierType). */
	GString	*cmdseq;		/* The command we run when triggerec. */
};

#define	CONTEXT_NAME_SIZE	(32)

typedef struct {
	gchar	context[CONTEXT_NAME_SIZE];
	GString	*cmdseq;
} CtrlGen;

/* ----------------------------------------------------------------------------------------- */

static CtrlKey *	key_new(const gchar *keyname, const gchar *cmdseq);
static void		key_destroy(CtrlKey *key);
static void		mouse_destroy(CtrlMouse *cm);

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-16 -	Create a new control info, the main representation of this module's work. */
CtrlInfo * ctrl_new(MainInfo *min)
{
	CtrlInfo	*ctrl;

	ctrl = g_malloc(sizeof *ctrl);
	ctrl->min = min;
	ctrl->nonumlock = TRUE;
	ctrl->keys = g_hash_table_new(g_str_hash, g_str_equal);
	ctrl->keys_inst = NULL;

	ctrl->mouse = NULL;

	ctrl->clickmclick.cmdseq = NULL;
	ctrl->clickmclick.delay  = 0.400f;

	ctrl->general = NULL;

	return ctrl;
}

/* 1999-03-16 -	Return a control info with the default controls already configured into it.
**		These are simply the few keys that have had hardcoded functions since more
**		or less the beginning of gentoo. It's nice to see them reborn like this. :)
*/
CtrlInfo * ctrl_new_default(MainInfo *min)
{
	CtrlInfo	*ctrl;

	ctrl = ctrl_new(min);

	ctrl_key_add(ctrl, "BackSpace",	"DirParent");
	ctrl_key_add(ctrl, "Delete",	"Delete");
	ctrl_key_add(ctrl, "F2",	"Rename");
	ctrl_key_add(ctrl, "F5",	"DirRescan");
	ctrl_key_add(ctrl, "h",		"DpHide");
	ctrl_key_add(ctrl, "r",		"DpRecenter");
	ctrl_key_add(ctrl, "space",	"ActivateOther");

	/* These are the "legacy" default actions for mouse button presses, that used to be hardcoded. */
	ctrl_mouse_add(ctrl, 1, GDK_SHIFT_MASK | GDK_CONTROL_MASK, "SelectSuffix action=toggle");
	ctrl_mouse_add(ctrl, 1, GDK_MOD1_MASK,			   "SelectType action=toggle");
	ctrl_mouse_add(ctrl, 1, GDK_MOD1_MASK  | GDK_SHIFT_MASK,   "SelectType action=select");
	ctrl_mouse_add(ctrl, 1, GDK_MOD1_MASK  | GDK_CONTROL_MASK, "SelectType action=unselect");
	ctrl_mouse_add(ctrl, 2, 0U, "DirParent");
	ctrl_mouse_add(ctrl, 3, 0U, "MenuPopup");

	return ctrl;
}

/* 1999-03-16 -	Remove a key; this is a g_hash_table_foreach_remove() callback. */
static gboolean key_remove(gpointer key, gpointer value, gpointer user)
{
	key_destroy(value);
	return TRUE;
}

/* 1999-03-16 -	Copy a key. A callback for ctrl_copy() below. */
static void key_copy(gpointer key, gpointer value, gpointer user)
{
	if(((CtrlKey *) value)->cmdseq != NULL)
		ctrl_key_add(user, ((CtrlKey *) value)->keyname, ((CtrlKey *) value)->cmdseq->str);
	else
		ctrl_key_add(user, ((CtrlKey *) value)->keyname, NULL);
}

/* 1999-06-13 -	A g_list_foreach() callback to destroy a mouse mapping. */
static void mouse_remove(gpointer data, gpointer user)
{
	mouse_destroy(data);
}

/* 1999-06-13 -	Copy a mouse button command mapping. */
static void mouse_copy(gpointer data, gpointer user)
{
	CtrlMouse	*cm = data;

	ctrl_mouse_add(user, ctrl_mouse_get_button(cm), ctrl_mouse_get_state(cm), ctrl_mouse_get_cmdseq(cm));
}

/* 2004-04-25 -	Copy a general binding. */
static void general_copy(gpointer key, gpointer value, gpointer user)
{
	if(value != NULL && ((CtrlGen *) value)->cmdseq != NULL)	/* Don't copy NULL cmdseq. */
		ctrl_general_set_cmdseq(user, key, ((CtrlGen *) value)->cmdseq->str);
}

/* 2004-04-25 -	Remove a general binding. */
static gboolean general_remove(gpointer key, gpointer value, gpointer user)
{
	CtrlGen	*cg = value;

	if(cg != NULL)
	{
		if(cg->cmdseq != NULL)
			g_string_free(cg->cmdseq, TRUE);
		g_free(cg);
	}
	return TRUE;
}

/* 1999-03-16 -	Copy the entire control info structure. Very useful for config. Note that
**		the copy will NOT retain the installation status of the original; in fact,
**		it will not be installed anywhere (which is generally what you (should) want).
*/
CtrlInfo * ctrl_copy(CtrlInfo *ctrl)
{
	CtrlInfo	*copy = NULL;

	if(ctrl != NULL)
	{
		copy = ctrl_new(ctrl->min);
		g_hash_table_foreach(ctrl->keys, key_copy, copy);
		g_list_foreach(ctrl->mouse, mouse_copy, copy);
		ctrl_clickmclick_set_cmdseq(copy, ctrl_clickmclick_get_cmdseq(ctrl));
		ctrl_clickmclick_set_delay(copy,  ctrl_clickmclick_get_delay(ctrl));
		if(ctrl->general != NULL)
			g_hash_table_foreach(ctrl->general, general_copy, copy);
		ctrl_numlock_ignore_set(copy, ctrl_numlock_ignore_get(ctrl));
	}
	return copy;
}

/* 1999-03-16 -	Destroy a control info. */
void ctrl_destroy(CtrlInfo *ctrl)
{
	if(ctrl != NULL)
	{
		ctrl_keys_uninstall_all(ctrl);
		g_hash_table_foreach_remove(ctrl->keys, key_remove, NULL);
		g_hash_table_destroy(ctrl->keys);
		g_list_foreach(ctrl->mouse, mouse_remove, NULL);
		if(ctrl->clickmclick.cmdseq)
			g_string_free(ctrl->clickmclick.cmdseq, TRUE);
		if(ctrl->general != NULL)
		{
			g_hash_table_foreach_remove(ctrl->general, general_remove, NULL);
			g_hash_table_destroy(ctrl->general);
		}
		g_free(ctrl);
	}
}

/* ----------------------------------------------------------------------------------------- */

void ctrl_numlock_ignore_set(CtrlInfo *ctrl, gboolean ignore)
{
	if(ctrl != NULL)
		ctrl->nonumlock = ignore;
}

gboolean ctrl_numlock_ignore_get(CtrlInfo *ctrl)
{
	if(ctrl != NULL)
		return ctrl->nonumlock;
	return FALSE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-16 -	Create a new key control with the given mapping. */
static CtrlKey * key_new(const gchar *keyname, const gchar *cmdseq)
{
	CtrlKey	*key;

	key = g_malloc(sizeof *key);
	g_strlcpy(key->keyname, keyname, sizeof key->keyname);
	if(cmdseq != NULL)
		key->cmdseq = g_string_new(cmdseq);
	else
		key->cmdseq = NULL;

	return key;
}

/* 1999-03-16 -	Destroy a key mapping. */
static void key_destroy(CtrlKey *key)
{
	if(key != NULL)
	{
		if(key->cmdseq != NULL)
			g_string_free(key->cmdseq, TRUE);
		g_free(key);
	}
}

/* 1999-03-16 -	Add a keyboard mapping to the given <ctrl>. Note that if the ctrl has
**		already been installed, adding keys to it won't activate them until it
**		is reinstalled.
*/
CtrlKey * ctrl_key_add(CtrlInfo *ctrl, const gchar *keyname, const gchar *cmdseq)
{
	CtrlKey	*key = NULL;

	if((ctrl != NULL) && (keyname != NULL))
	{
		key = key_new(keyname, cmdseq);
		ctrl_key_remove_by_name(ctrl, key->keyname);
		g_hash_table_insert(ctrl->keys, key->keyname, key);
	}
	return key;
}

/* 1999-03-17 -	Add a new key->command mapping, with the rather peculiar property that
**		the key name is in fact illegal (it is not an existing key), and the
**		command part is empty. The key name will, however, be unique among all
**		existing maps. Useful when adding keys interactively.
*/
CtrlKey * ctrl_key_add_unique(CtrlInfo *ctrl)
{
	gchar	buf[KEY_NAME_SIZE], *name = NULL;
	gint	i;

	for(i = 0; (name == NULL) && (i < 1000); i++)	/* Um, not quite unique, since we can give up. */
	{
		if(i == 0)
			g_snprintf(buf, sizeof buf, "none");
		else
			g_snprintf(buf, sizeof buf, "none-%d", i + 1);
		if(g_hash_table_lookup(ctrl->keys, buf) == NULL)
			name = buf;
	}
	return ctrl_key_add(ctrl, name, NULL);
}

/* 1999-03-16 -	Remove the given key mapping, and also destroy it. */
void ctrl_key_remove(CtrlInfo *ctrl, CtrlKey *key)
{
	if((ctrl != NULL) && (key != NULL))
		ctrl_key_remove_by_name(ctrl, key->keyname);
}

/* 1999-03-16 -	Remove a named key mapping, and destroy it. */
void ctrl_key_remove_by_name(CtrlInfo *ctrl, const gchar *keyname)
{
	if((ctrl != NULL) && (keyname != NULL))
	{
		CtrlKey	*key;

		if((key = g_hash_table_lookup(ctrl->keys, keyname)) != NULL)
		{
			g_hash_table_remove(ctrl->keys, key->keyname);
			key_destroy(key);
		}
	}
}

/* 1999-03-17 -	Remove all key mappings from <ctrl>. It is a very good idea to first
**		uninstall it from (all) keyboard contexts.
*/
void ctrl_key_remove_all(CtrlInfo *ctrl)
{
	if(ctrl != NULL)
		g_hash_table_foreach_remove(ctrl->keys, key_remove, NULL);
}

/* 1999-03-16 -	Return the key part of given key->command mapping. */
const gchar * ctrl_key_get_keyname(const CtrlKey *key)
{
	if(key != NULL)
		return key->keyname;
	return NULL;
}

/* 1999-03-16 -	Return the command part of a key-to-command mapping. */
const gchar * ctrl_key_get_cmdseq(const CtrlKey *key)
{
	if((key != NULL) && (key->cmdseq != NULL))
		return key->cmdseq->str;
	return NULL;
}

/* 1999-03-16 -	Change the key part of the given mapping. Changes it in the
**		<ctrl> as well. Is basically equivalent to a remove followed
**		by an add, but slightly more efficient.
*/
void ctrl_key_set_keyname(CtrlInfo *ctrl, CtrlKey *key, const gchar *keyname)
{
	if((ctrl != NULL) && (key != NULL) && (keyname != NULL))
	{
		if(g_hash_table_lookup(ctrl->keys, key->keyname) == key)
		{
			g_hash_table_remove(ctrl->keys, key->keyname);
			g_strlcpy(key->keyname, keyname, sizeof key->keyname);
			g_hash_table_insert(ctrl->keys, key->keyname, key);
		}
	}
}

/* 1999-03-16 -	Change command sequence part of given mapping. Equivalent to,
**		but far more efficient than, a remove+add pair.
*/
void ctrl_key_set_cmdseq(CtrlInfo *ctrl, CtrlKey *key, const gchar *cmdseq)
{
	if((ctrl != NULL) && (key != NULL))
	{
		if(g_hash_table_lookup(ctrl->keys, key->keyname) == key)
		{
			if(cmdseq != NULL)
			{
				if(key->cmdseq != NULL)
					g_string_assign(key->cmdseq, cmdseq);
				else
					key->cmdseq = g_string_new(cmdseq);
			}
			else
			{
				if(key->cmdseq != NULL)
					g_string_free(key->cmdseq, TRUE);
				key->cmdseq = NULL;
			}
		}
	}
}

/* 1999-05-08 -	Return TRUE if given <key> already has a command sequence equal to <cmdseq>.
**		If not, FALSE is returned.
*/
gboolean ctrl_key_has_cmdseq(CtrlInfo *ctrl, CtrlKey *key, const gchar *cmdseq)
{
	if((ctrl != NULL) && (key != NULL) && (cmdseq != NULL))
	{
		if(key->cmdseq != NULL)
			return strcmp(key->cmdseq->str, cmdseq) ? FALSE : TRUE;
		return FALSE;
	}
	return FALSE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-16 -	Just a simple hash callback to install a CtrlKey into a keyboard context. */
static void key_install(gpointer key, gpointer value, gpointer user)
{
	if(((CtrlKey *) value)->cmdseq != NULL)
		kbd_context_entry_add(user, ((CtrlKey *) value)->keyname, KET_CMDSEQ, ((CtrlKey *) value)->cmdseq->str);
}

/* 1999-03-16 -	Install all <ctrl>'s key mappings as command-type keyboard commands in <ctx>.
**		Note that the set of which keyboard contexts a CtrlInfo has been installed in
**		is a retained property in the CtrlInfo.
*/
void ctrl_keys_install(CtrlInfo *ctrl, KbdContext *ctx)
{
	if((ctrl != NULL) && (ctx != NULL))
	{
		/* FIXME: This really assumes there is only one attachment. */
		if(ctrl->nonumlock)
			kbd_context_mask_set(ctx, GDK_MOD2_MASK);
		else
			kbd_context_mask_set(ctx, 0);

		g_hash_table_foreach(ctrl->keys, key_install, ctx);
		ctrl->keys_inst = g_slist_prepend(ctrl->keys_inst, ctx);
	}
}

/* 1999-03-16 -	Callback for ctrl_keys_uninstall() below. */
static void key_uninstall(gpointer key, gpointer value, gpointer user)
{
	kbd_context_entry_remove(user, ((CtrlKey *) value)->keyname);
}

/* 1999-03-16 -	Uninstall all keys defined by <ctrl> from context <ctx>. If, in fact, the
**		ctrl has never been installed in the context, nothing happens. Does not
**		(ever) affect which mappings are contained in the ctrl.
*/
void ctrl_keys_uninstall(CtrlInfo *ctrl, KbdContext *ctx)
{
	if((ctrl != NULL) && (ctx != NULL))
	{
		if(g_slist_find(ctrl->keys_inst, ctx) != NULL)
		{
			g_hash_table_foreach(ctrl->keys, key_uninstall, ctx);
			ctrl->keys_inst = g_slist_remove(ctrl->keys_inst, ctx);
		}
	}
}

/* 1999-03-17 -	A foreach callback for ctrl_keys_uninstall_all() below. Does not just
**		call ctrl_keys_uninstall(), since that modofies the list which is a bad
**		idea when iterating it...
*/
static void keys_uninstall(gpointer data, gpointer user)
{
	g_hash_table_foreach(((CtrlInfo *) user)->keys, key_uninstall, data);
}

/* 1999-03-17 -	Uninstall given ctrlinfo from all of its keyboard contexts. Does not alter
**		the actual collection of key mappings in the ctrlinfo.
*/
void ctrl_keys_uninstall_all(CtrlInfo *ctrl)
{
	if(ctrl != NULL)
	{
		g_slist_foreach(ctrl->keys_inst, keys_uninstall, ctrl);
		g_slist_free(ctrl->keys_inst);
		ctrl->keys_inst = NULL;
	}
}

/* ----------------------------------------------------------------------------------------- */

static gint cmp_string(gconstpointer a, gconstpointer b)
{
	return strcmp(a, b);
}

static void key_insert(gpointer key, gpointer value, gpointer user)
{
	GSList	**list = user;

	*list = g_slist_insert_sorted(*list, value, cmp_string);
}

/* 1999-03-16 -	Return a list of all <ctrl>'s key mappings, sorted on the key names. */
GSList * ctrl_keys_get_list(CtrlInfo *ctrl)
{
	GSList	*list = NULL;

	if(ctrl != NULL)
		g_hash_table_foreach(ctrl->keys, key_insert, &list);

	return list;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-06-12 -	Create a new mouse command mapping. Empty. */
static CtrlMouse * mouse_new(void)
{
	CtrlMouse	*cm;

	cm = g_malloc(sizeof *cm);

	cm->button = cm->state = 0U;
	cm->cmdseq = NULL;

	return cm;
}

/* 1999-06-12 -	Destroy a mouse mapping which is no longer needed (or used). */
static void mouse_destroy(CtrlMouse *cm)
{
	if(cm != NULL)
	{
		if(cm->cmdseq != NULL)
			g_string_free(cm->cmdseq, TRUE);
		g_free(cm);
	}
}

/* 1999-06-12 -	Compare two mouse command definitions, ordering them in mouse button order. */
static gint mouse_cmp(gconstpointer a, gconstpointer b)
{
	const CtrlMouse	*ma = a, *mb = b;

	if(ma->button == mb->button)
		return ma->state - mb->state;
	return ma->button - mb->button;
}

/* 1999-06-12 -	Add a command mapping for a mouse button. Note that this will not prevent the
**		creation of multiple identical mappings, something that is silly at best, and
**		dangerous at worst. Beware.
*/
CtrlMouse * ctrl_mouse_add(CtrlInfo *ctrl, guint button, guint state, const gchar *cmdseq)
{
	CtrlMouse	*cm = NULL;

	if(ctrl != NULL)
	{
		cm = mouse_new();

		cm->button = button;
		cm->state  = state;
		cm->cmdseq = g_string_new(cmdseq);

		ctrl->mouse = g_list_insert_sorted(ctrl->mouse, cm, mouse_cmp);
	}
	return cm;
}

/* 1999-06-13 -	Set a new button for a mapping. */
void ctrl_mouse_set_button(CtrlMouse *mouse, guint button)
{
	if(mouse != NULL)
		mouse->button = button;
}

/* 1999-06-13 -	Set a new modifier state for a mapping. */
void ctrl_mouse_set_state(CtrlMouse *mouse, guint state)
{
	if(mouse != NULL)
		mouse->state = state;
}

/* 1999-06-13 -	Set a new command sequence for the given mapping. */
void ctrl_mouse_set_cmdseq(CtrlMouse *mouse, const gchar *cmdseq)
{
	if(mouse != NULL)
		g_string_assign(mouse->cmdseq, cmdseq);
}

/* 1999-06-13 -	Access the button part of a mouse command mapping. */
guint ctrl_mouse_get_button(const CtrlMouse *mouse)
{
	if(mouse != NULL)
		return mouse->button;
	return 0;
}

/* 1999-06-13 -	Get the state bits for a mouse button command mapping. */
guint ctrl_mouse_get_state(const CtrlMouse *mouse)
{
	if(mouse != NULL)
		return mouse->state;
	return 0;
}

/* 1999-06-13 -	Get the command sequence for a mouse mapping. */
const gchar * ctrl_mouse_get_cmdseq(const CtrlMouse *mouse)
{
	if(mouse != NULL)
		return mouse->cmdseq->str;
	return NULL;
}

/* 1999-06-20 -	Remove the mouse mapping <mouse> from <ctrl>. */
void ctrl_mouse_remove(CtrlInfo *ctrl, CtrlMouse *mouse)
{
	if((ctrl != NULL) && (mouse != NULL))
		ctrl->mouse = g_list_remove(ctrl->mouse, mouse);
}

/* 1999-06-20 -	Clear away all mouse mappings from <ctrl>. */
void ctrl_mouse_remove_all(CtrlInfo *ctrl)
{
	if(ctrl != NULL)
	{
		GList	*iter;

		for(iter = ctrl->mouse; iter != NULL; iter = g_list_next(iter))
			mouse_destroy(iter->data);
		g_list_free(ctrl->mouse);
		ctrl->mouse = NULL;
	}
}

/* 1999-06-12 -	Check if we have a mapping for the mouse button event described by <evt>, and if we
**		do, return a pointer to the command definition for the caller to execute. Else
**		returns NULL.
*/
const gchar * ctrl_mouse_map(CtrlInfo *ctrl, GdkEventButton *evt)
{
	if((ctrl != NULL) && (evt != NULL))
	{
		GList		*iter;
		CtrlMouse	*cm;
		guint		state;

		state = evt->state;
		/* Allow only the modifiers known to be configurable for mouse buttons. */
		state &= (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK);

		for(iter = ctrl->mouse; iter != NULL; iter = g_list_next(iter))
		{
			cm = iter->data;
			if((cm->button == evt->button) && (cm->state == state))
				return cm->cmdseq->str;
		}
	}
	return NULL;
}

/* 1999-06-13 -	Get a list of all CtrlMouse mouse button command mappings. When done with the
**		list, the caller must call g_slist_free() on it.
*/
GSList * ctrl_mouse_get_list(CtrlInfo *ctrl)
{
	GSList	*list = NULL;

	if(ctrl != NULL)
	{
		GList	*iter;

		for(iter = ctrl->mouse; iter != NULL; iter = g_list_next(iter))
			list = g_slist_append(list, iter->data);
	}

	return list;
}

/* 1999-06-20 -	This (borderline silly) function answers whether there exists a "collision" in the
**		mouse command mappings, i.e. if the same button+state combo is used more than once.
**		Ultimately, it shouldn't be possible to create such a situation, but in the current
**		GUI it is. So this function can be used to at least warn the user.
*/
gboolean ctrl_mouse_ambiguity_exists(const CtrlInfo *ctrl)
{
	if(ctrl != NULL)
	{
		GList	*i, *j;

		for(i = ctrl->mouse; i != NULL; i = g_list_next(i))
		{
			for(j = ctrl->mouse; j != NULL; j = g_list_next(j))
			{
				if(j == i)
					continue;
				if(mouse_cmp(i->data, j->data) == 0)
					return TRUE;
			}
		}
	}
	return FALSE;
}

/* ----------------------------------------------------------------------------------------- */

void ctrl_clickmclick_set_cmdseq(CtrlInfo *ctrl, const gchar *cmdseq)
{
	if(cmdseq)
	{
		if(ctrl->clickmclick.cmdseq)
			g_string_assign(ctrl->clickmclick.cmdseq, cmdseq);
		else
			ctrl->clickmclick.cmdseq = g_string_new(cmdseq);
	}
	else if(ctrl->clickmclick.cmdseq)
	{
		g_string_free(ctrl->clickmclick.cmdseq, TRUE);
		ctrl->clickmclick.cmdseq = NULL;
	}
}

const gchar * ctrl_clickmclick_get_cmdseq(const CtrlInfo *ctrl)
{
	return ctrl ? ctrl->clickmclick.cmdseq ? ctrl->clickmclick.cmdseq->str : 0 : 0;
}

void ctrl_clickmclick_set_delay(CtrlInfo *ctrl, gfloat delay)
{
	ctrl->clickmclick.delay = delay;
}

gfloat ctrl_clickmclick_get_delay(const CtrlInfo *ctrl)
{
	return ctrl ? ctrl->clickmclick.delay : 0.0f;
}

/* ----------------------------------------------------------------------------------------- */

/* 2004-04-26 -	Clear away all general command definitions. */
void ctrl_general_clear(CtrlInfo *ctrl)
{
	if(ctrl == NULL || ctrl->general == NULL)
		return;
	g_hash_table_foreach_remove(ctrl->general, general_remove, NULL);
}

/* 2004-04-19 -	Store a new general command sequence. */
void ctrl_general_set_cmdseq(CtrlInfo *ctrl, const gchar *context, const gchar *cmdseq)
{
	CtrlGen	*cg;

	if(ctrl == NULL)
		return;
	if(ctrl->general == NULL)
		ctrl->general = g_hash_table_new(g_str_hash, g_str_equal);
	cg = g_hash_table_lookup(ctrl->general, context);
	if(cg == NULL)
	{
		cg = g_malloc(sizeof *cg);
		g_snprintf(cg->context, sizeof cg->context, "%s", context);
		cg->cmdseq = g_string_new("");
		g_hash_table_insert(ctrl->general, cg->context, cg);
	}
	if(cmdseq != NULL)
		g_string_assign(cg->cmdseq, cmdseq);
	else
		general_remove(cg->context, cg, ctrl);
}

static void general_prepend(gpointer key, gpointer data, gpointer user)
{
	GSList	**list = user;

	*list = g_slist_prepend(*list, key);
}

/* 2004-04-25 -	Return list of contexts, i.e. char pointers. They are constants. */
GSList * ctrl_general_get_contexts(const CtrlInfo *ctrl)
{
	GSList	*list = NULL;

	if(ctrl != NULL && ctrl->general != NULL)
		g_hash_table_foreach(ctrl->general, general_prepend, &list);
	return list;
}

/* 2004-04-19 -	Retrieve general command sequence. */
const gchar * ctrl_general_get_cmdseq(const CtrlInfo *ctrl, const gchar *context)
{
	const CtrlGen	*cg;

	if(ctrl == NULL || ctrl->general == NULL)
		return NULL;
	cg = g_hash_table_lookup(ctrl->general, context);
	if(cg == NULL)
		return NULL;
	return cg->cmdseq != NULL ? cg->cmdseq->str : NULL;
}
