/*
** 2010-06-13 -	A new dialog, which scratches an itch: if a program uses the "string with separator" way of showing lists,
**		it ought to provide a more user-friendly way of editing the list. Visual Studio hurts, in this department,
**		and up to now gentoo has, too. One of those is fixed, now.
*/

/* Editor function, returns TRUE if the contents of 'value' are to be kept. */
typedef gboolean	(*LDlgItemEditor)(GString *value, gpointer user);

extern void	ldl_dialog_sync_new_wait(gchar *list, gsize max, gunichar separator, const gchar *title);
extern void	ldl_dialog_sync_new_full_wait(gchar *list, gsize max, gunichar separator, const gchar *title, LDlgItemEditor editor, gpointer user);
