/*
** 1999-03-16 -	Header for the controls module. The use of opaque object-like data structures
**		is spreading rapidly across gentoo, and old proponents of a centralized-data
**		architecture are simply being rolled over. Or something.
*/

#if !defined CONTROLS_H
#define	CONTROLS_H

typedef struct CtrlInfo		CtrlInfo;
typedef struct CtrlKey		CtrlKey;
typedef struct CtrlMouse	CtrlMouse;

/* Functions for creating, copying, and destroying CtrlInfos. */
CtrlInfo *	ctrl_new(MainInfo *min);
CtrlInfo *	ctrl_new_default(MainInfo *min);
CtrlInfo *	ctrl_copy(CtrlInfo *ctrl);
void		ctrl_destroy(CtrlInfo *ctrl);

/* Functions to allow/disallow numlock's influence on bound commands.
** Does not change the actual binding, only controls if the numlock
** part of an eventual binding is respected or not.
*/
void		ctrl_numlock_ignore_set(CtrlInfo *ctrl, gboolean ignore);
gboolean	ctrl_numlock_ignore_get(CtrlInfo *ctrl);

/* Functions for working with individual keyboard bindings. */
CtrlKey *	ctrl_key_add(CtrlInfo *ctrl, const gchar *keyname, const gchar *cmdseq);
CtrlKey *	ctrl_key_add_unique(CtrlInfo *ctrl);
void		ctrl_key_remove(CtrlInfo *ctrl, CtrlKey *key);
void		ctrl_key_remove_by_name(CtrlInfo *ctrl, const gchar *keyname);
void		ctrl_key_remove_all(CtrlInfo *ctrl);
const gchar *	ctrl_key_get_keyname(const CtrlKey *key);
const gchar *	ctrl_key_get_cmdseq(const CtrlKey *key);
void		ctrl_key_set_keyname(CtrlInfo *ctrl, CtrlKey *key, const gchar *keyname);
void		ctrl_key_set_cmdseq(CtrlInfo *ctrl, CtrlKey *key, const gchar *cmdseq);
gboolean	ctrl_key_has_cmdseq(CtrlInfo *ctrl, CtrlKey *key, const gchar *cmdseq);

/* These functions work on *all* key bindings. */
void		ctrl_keys_install(CtrlInfo *ctrl, KbdContext *ctx);
void		ctrl_keys_uninstall(CtrlInfo *ctrl, KbdContext *ctx);
void		ctrl_keys_uninstall_all(CtrlInfo *ctrl);
GSList *	ctrl_keys_get_list(CtrlInfo *ctrl);

/* Functions to add, remove, and manipulate mouse button bindings. */
CtrlMouse *	ctrl_mouse_add(CtrlInfo *ctrl, guint button, guint state, const gchar *cmdseq);
void		ctrl_mouse_set_button(CtrlMouse *mouse, guint state);
void		ctrl_mouse_set_state(CtrlMouse *mouse, guint state);
void		ctrl_mouse_set_cmdseq(CtrlMouse *mouse, const gchar *cmdseq);
guint		ctrl_mouse_get_button(const CtrlMouse *mouse);
guint		ctrl_mouse_get_state(const CtrlMouse *mouse);
const gchar *	ctrl_mouse_get_cmdseq(const CtrlMouse *mouse);
void		ctrl_mouse_remove(CtrlInfo *ctrl, CtrlMouse *mouse);
void		ctrl_mouse_remove_all(CtrlInfo *ctrl);
const gchar *	ctrl_mouse_map(CtrlInfo *ctrl, GdkEventButton *evt);
GSList *	ctrl_mouse_get_list(CtrlInfo *ctrl);
gboolean	ctrl_mouse_ambiguity_exists(const CtrlInfo *ctrl);

/* Functions for assigning a command to Click-M-Click, which is kind of a simple gesture. */
void		ctrl_clickmclick_set_cmdseq(CtrlInfo *ctrl, const gchar *cmdseq);
const gchar *	ctrl_clickmclick_get_cmdseq(const CtrlInfo *ctrl);
void		ctrl_clickmclick_set_delay(CtrlInfo *ctrl, gfloat delay);
gfloat		ctrl_clickmclick_get_delay(const CtrlInfo *ctrl);

/* Functions for general commands, associated with some context. */
void		ctrl_general_clear(CtrlInfo *ctrl);
void		ctrl_general_set_cmdseq(CtrlInfo *ctrl, const gchar *context, const gchar *cmdseq);
const gchar *	ctrl_general_get_cmdseq(const CtrlInfo *ctrl, const gchar *context);
GSList *	ctrl_general_get_contexts(const CtrlInfo *ctrl);

#endif		/* CONTROLS_H */
