/*
** 1998-05-23 -	Header for the (g|u)uid-to-name mapping module.
*/

#include <unistd.h>

/* ------------------------------------------------------------------------------------------ */

gboolean	usr_init(void);

const gchar *	usr_get_home(void);

const gchar *	usr_lookup_uname(uid_t uid);
long		usr_lookup_uid(const gchar *name);
const gchar *	usr_lookup_uhome(const gchar *uname);
const gchar *	usr_lookup_gname(gid_t gid);
long		usr_lookup_gid(const gchar *name);

/* ------------------------------------------------------------------------------------------ */

typedef enum { UIC_USER = 0, UIC_GROUP } UsrCategory;

extern GList *	usr_string_list_create(UsrCategory category, gint id, gint *index);
extern void	usr_string_list_destroy(GList *list);
