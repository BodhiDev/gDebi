/*
** 1999-03-10 -	Interface for the keyboard accelerator module. No, this does not make your
**		keyboard faster.
*/

#if !defined KEYBOARD_H
#define	KEYBOARD_H

#include <stdarg.h>

/* These are the various types of keyboard shortcuts supported. Each type has a comment showing
** the tail part of the intended prototype for an entry_add() call for that type.
*/
typedef enum {	KET_GDKEVENT = 0,	/* (.., GdkEventFunc func, gpointer user); */
		KET_GTKOBJECT,		/* (.., GtkObject *obj, const gchar *signal_name, gpointer user); */
		KET_CMDSEQ		/* (.., const gchar *cmdseq_name); */
	     } KEType;

/* So, for example, if you have a GtkButton called btn, and you want the key "<Shift>a" to
** activate it, you'd do kbd_context_entry_add(context, "<Shift>a", KET_GTKOBJECT, btn, "clicked", userdata);
*/

typedef struct KbdContext KbdContext;

KbdContext *	kbd_context_new(MainInfo *min);

/* These functions let you set a mask which is applied (binary AND:ed) to the
** 'state' member of the keyboard event when a key is pressed. The mask is also
** applied to the key describing  The primary use for this is to mask away that
** pesky numlock.
*/
void		kbd_context_mask_set(KbdContext *ctx, guint mask);
guint		kbd_context_mask_get(KbdContext *ctx);

gint		kbd_context_entry_add(KbdContext *ctx, const gchar *key, KEType type, ...);
gint		kbd_context_entry_vadd(KbdContext *ctx, const gchar *key, KEType type, va_list args);
void		kbd_context_entry_remove(KbdContext *ctx, const gchar *key);

/* To be used, the keyboard context needs to be attached to a window. */
gint		kbd_context_attach(KbdContext *ctx, GtkWindow *win);
void		kbd_context_detach(KbdContext *ctx, GtkWindow *win);

void		kbd_context_clear(KbdContext *ctx);

void		kbd_context_destroy(KbdContext *ctx);

#endif		/* KEYBOARD_H */
