/*
** This module implements a dialog that can "nag" the user, but also remember if the user
** really doesn't want to be nagged at any more. It remembers this separately for each
** thing we nag about, as is common these days.
*/

extern gboolean	ndl_dialog_sync_new_wait(MainInfo *min, const gchar *tag, const gchar *title, const gchar *body);
