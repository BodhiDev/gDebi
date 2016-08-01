/*
** 1998-08-02 -	Header file for the new string module.
** 2000-04-16 -	When <string.h> is included, all symbols starting with "str" are reserved for
**		the C implementation. This makes my use of "str_" as a module-prefix for this
**		module a blatantly obvious conformance error. Fixed by changing to "stu_"
**		(for string utils, of course?).
*/

#include <sys/types.h>		/* For mode_t. */

#include <glib.h>		/* For the g-types. */

/* ----------------------------------------------------------------------------------------- */

extern gchar *		stu_tickify(gchar *buf, guint64 n, gchar tick);

extern gint		stu_strcmp_vector(const gchar *string, const gchar **vector, gsize vector_size, gint def);

extern gchar *		stu_glob_to_re(const gchar *glob);
extern void		stu_gstring_glob_to_re(GString *glob);

extern gchar *		stu_mode_to_text(gchar *buf, gsize buf_max, mode_t mode);

extern const gchar *	stu_scan_string(const gchar *def, const gchar **str);

extern const gchar *	stu_word_length(const gchar *str, gsize *len, gchar *store);
extern gchar **		stu_split_args(const char *argstring);

extern const gchar *	stu_escape(const gchar *string);

extern guint		stu_replace_simple(GString *output, const gchar *input, const gchar *find, const gchar *replace, gboolean global, gboolean nocase);

extern const gchar *	stu_utf8_find(const gchar *string, gboolean (*function)(gunichar ch, gpointer user), gpointer user);

extern gboolean		stu_interpolate_dictionary(gchar *output, gsize size, const gchar *format, const GHashTable *dictionary);
