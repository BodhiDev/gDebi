/*
** 1999-03-10 -	Yay, it's a keyboard accelerator support module! I guess this should've been
**		written months ago, but it's not until now that GTK+'s keyboard support seems
**		to have developed enough for me to actually use it. :^) Oh, and then I'm evil
**		enough to actually go around it. I must be insane. But because I want the ability
**		to keyboard-accelerate stuff that isn't GTK+ objects, I'll do this my way.
** 2000-02-09 -	Added support for a context-wide mask of modifiers to be ignored. Very handy for
**		e.g. masking out that pesky NumLock key.
*/

#include "gentoo.h"

#include <stdarg.h>

#include "cmdseq.h"

#include "keyboard.h"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	GdkEventFunc	func;		/* GdkEvent function handler to call. */
	gpointer	user;		/* User data to pass along in call. */
} KEGdkEvent;

typedef struct {
	GObject		*obj;		/* Which object to signal. */
	gchar		*signame;	/* Name of signal to emit. */
	gpointer	user;		/* User data passed along signal emission. */
} KEObject;

typedef struct {
	gchar		*cmdseq;	/* Name of command sequence to run. */
} KECmdSeq;

typedef struct {
	gchar		*key;		/* The key that activates it. */
	KEType		type;		/* Type of accelerator; GTK+ object, gentoo command, etc. */
	union {
	KEGdkEvent	gdkevent;
	KEObject	object;
	KECmdSeq	cmdseq;
	} entry;
} KbdEntry;

struct KbdContext {
	MainInfo	*min;		/* A link to the ever-present core info structure. */
	GHashTable	*hash;		/* A hash of KbdEntries, hashed on key names. */
	GdkModifierType	mod_mask;	/* Indicated modifiers are ignored. */
	guint		use_count;	/* Counts # of attachments made. */
};

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-10 -	Create a new, empty, keyboard context to which the user then can add "entries". */
KbdContext * kbd_context_new(MainInfo *min)
{
	KbdContext	*ctx;

	ctx = g_malloc(sizeof *ctx);

	ctx->min	= min;
	ctx->hash	= g_hash_table_new(g_str_hash, g_str_equal);
	ctx->mod_mask	= 0U;
	ctx->use_count	= 0U;

	return ctx;
}

/* ----------------------------------------------------------------------------------------- */

/* 2000-02-08 -	Set a maskt to be applied against the 'state' member of keyboard events. */
void kbd_context_mask_set(KbdContext *ctx, GdkModifierType mask)
{
	if(ctx != NULL)
		ctx->mod_mask = mask;
}

/* 2000-02-08 -	Return current state mask. */
GdkModifierType kbd_context_mask_get(KbdContext *ctx)
{
	if(ctx != NULL)
		return ctx->mod_mask;
	return 0U;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-10 -	Create a new entry. */
static KbdEntry * entry_new(const gchar *key, KEType type, va_list args)
{
	KbdEntry	*ent;

	if(key == NULL)
		return NULL;

	ent = g_malloc(sizeof *ent);
	ent->key = g_strdup(key);
	ent->type = type;

	switch(type)
	{
		case KET_GDKEVENT:
			ent->entry.gdkevent.func  = va_arg(args, GdkEventFunc);
			ent->entry.gdkevent.user  = va_arg(args, gpointer);
			break;
		case KET_GTKOBJECT:
			ent->entry.object.obj	  = va_arg(args, GObject *);
			ent->entry.object.signame = g_strdup(va_arg(args, gchar *));
			ent->entry.object.user	  = va_arg(args, gpointer);
			break;
		case KET_CMDSEQ:
			ent->entry.cmdseq.cmdseq = g_strdup(va_arg(args, gchar *));
			break;
	}

	return ent;
}

/* 1999-03-10 -	Destroy an entry, freeing all memory used by it. */
static void entry_destroy(KbdEntry *ent)
{
	if(ent != NULL)
	{
		switch(ent->type)
		{
			case KET_GDKEVENT:
				break;
			case KET_GTKOBJECT:
				g_free(ent->entry.object.signame);
				break;
			case KET_CMDSEQ:
				g_free(ent->entry.cmdseq.cmdseq);
				break;
		}
		g_free(ent->key);
		g_free(ent);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-16 -	Add an entry based on the va_list of arguments. The list must be destroyed by
**		the caller; we don't do va_end() on it here.
** 2000-02-09 -	Now applies the context-wide modifier mask before creating the binding, if any.
*/
gint kbd_context_entry_vadd(KbdContext *ctx, const gchar *key, KEType type, va_list args)
{
	KbdEntry	*ent;

	if((ctx == NULL) || (key == NULL))
		return 0;

	if(ctx->mod_mask != 0U)		/* Does the context require modifiers to be filtered out? */
	{
		guint		keysym;
		GdkModifierType	mod;

		gtk_accelerator_parse(key, &keysym, &mod);
		mod &= ~ctx->mod_mask;	/* Apply the modifier mask. */
		key = gtk_accelerator_name(keysym, mod);
	}

	if((ent = entry_new(key, type, args)) != NULL)
	{
		kbd_context_entry_remove(ctx, key);
		g_hash_table_insert(ctx->hash, ent->key, ent);
	}
	return ent != NULL;
}

/* 1999-03-10 -	Add an "entry" to a keyboard context. These entries are sort of like GTK+'s
**		accelerator entries, only a bit more general. Basically, they map a keyboard
**		event to some action. Keys are recognized by their GTK+ names (such as "<Shift>a").
**		Note that it is entierly OK to add entries even after the context has been attached
**		to a window (in fact, that is the expected usage). There can only be one entry
**		per key; any preexisting entry will be removed before the new is added.
**		  Returns 1 on success, 0 on failure.
*/
gint kbd_context_entry_add(KbdContext *ctx, const gchar *key, KEType type, ...)
{
	va_list	args;
	gint	ret;

	va_start(args, type);
	ret = kbd_context_entry_vadd(ctx, key, type, args);
	va_end(args);
	return ret;
}

/* 1999-03-10 -	Remove an entry from given context. Since entries are module-private and highly
**		opaque (there is no publicly accessible entry type), the entry is also destroyed.
*/
void kbd_context_entry_remove(KbdContext *ctx, const gchar *key)
{
	if((ctx != NULL) && (key != NULL))
	{
		KbdEntry	*ent;

		if((ent = g_hash_table_lookup(ctx->hash, key)) != NULL)
		{
			g_hash_table_remove(ctx->hash, key);
			entry_destroy(ent);
		}
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-10 -	This gets called (by GTK+) when the user presses a key in a window to which
**		a keyboard context has been attached. Look up the key, trigger action.
** 2000-02-09 -	Now knows about, and applies, the modifier mask allowing e.g. NumLock to be
**		ignored.
*/
static gboolean evt_key_press(GtkWidget *wid, GdkEventKey *evt, gpointer user)
{
	KbdContext	*ctx = user;
	KbdEntry	*ent = NULL;
	gchar		*key;
	gint		ret;
	GdkModifierType	mod;

	mod = evt->state;
	if(ctx->mod_mask != 0U)
		mod &= ~ctx->mod_mask;

/*	printf("Someone pressed %u ('%c') state=%04X\n", evt->keyval, evt->keyval, evt->state);*/

	/* Here's the GTK+ function that provides the crucial mapping of (keyval,state) => ASCII name. */
	if((key = gtk_accelerator_name(evt->keyval, mod)) != NULL)
	{
/*		printf(" a key also known as \"%s\"\n", key);*/
		if((ent = g_hash_table_lookup(ctx->hash, key)) != NULL)
		{
/*			printf("  with a binding of type %d, too\n", ent->type);*/
			switch(ent->type)
			{
				case KET_GDKEVENT:
					ent->entry.gdkevent.func((GdkEvent *) evt, ent->entry.gdkevent.user);
					break;
				case KET_GTKOBJECT:
					g_signal_emit_by_name(G_OBJECT(ent->entry.object.obj),
								ent->entry.object.signame, ent->entry.object.user, &ret);
					break;
				case KET_CMDSEQ:
					csq_execute(ctx->min, ent->entry.cmdseq.cmdseq);
					break;
			}
		}
		g_free(key);
	}
	return ent != NULL;
}

/* 1999-03-10 -	Attach a keyboard context to a window. This will cause all keyboard events
**		generated in the window to be "trapped", and routed through the context, which
**		will envoke matching entries.
*/
gint kbd_context_attach(KbdContext *ctx, GtkWindow *win)
{
	if((ctx == NULL) || (win == NULL))
		return 0;

	return g_signal_connect(G_OBJECT(win), "key_press_event", G_CALLBACK(evt_key_press), ctx);
}

/* 1999-04-02 -	Detach given keyboard context from given window. If it's not attached, nothing
**		happens.
*/
void kbd_context_detach(KbdContext *ctx, GtkWindow *win)
{
	gulong	handler;

	if((ctx == NULL) || (win == NULL))
		return;

	if((handler = g_signal_handler_find(G_OBJECT(win), G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA,
			0u, 0, NULL, G_CALLBACK(evt_key_press), ctx)) != 0)
	{
		g_signal_handler_disconnect(G_OBJECT(win), handler);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-10 -	Free an entry, with GHashTable prototype. */
static gboolean entry_hash_free(gpointer key, gpointer value, gpointer user)
{
	entry_destroy(value);

	return TRUE;
}

/* 1999-03-11 -	Clear a context from all its entries. */
void kbd_context_clear(KbdContext *ctx)
{
	if(ctx != NULL)
		g_hash_table_foreach_remove(ctx->hash, entry_hash_free, NULL);
}

/* 1999-03-10 -	Destroy a keyboard context. Will whine if it is still attached to some window. */
void kbd_context_destroy(KbdContext *ctx)
{
	if(ctx != NULL)
	{
		if(ctx->use_count != 0)
			fprintf(stderr, "KBD: Destroying key context with non-zero use-count!\n");
		kbd_context_clear(ctx);

		g_hash_table_destroy(ctx->hash);
		g_free(ctx);
	}
}
