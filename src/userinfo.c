/*
** 1998-05-23 -	This little module helps map user and group IDs to ASCII names.
**		To perform this mapping, it reads /etc/passwd.
** 1998-05-29 -	Heavily modified. Now it is more eager to learn, since it will always
**		store *all* new encountered (uid,uname) and (gid,gname) pairs.
**		This maximizes the amount of user/group info available, and allows
**		this info to be used for other funky things (such as a chown command).
** 1998-06-08 -	Redesigned the menu-creation support, and also added some crucial
**		functionality (squeezed it in).
** 1998-12-22 -	Completely rewrote the /etc/passwd parsing. No longer lets the user control
**		the file name, but considers it the system's business. Uses the BSD-ish
**		getpwent()-API to access the file. Much cleaner and more robust. Also added
**		storing of the user's home directories to another hash (for ~-support).
**		  Then did the exact same thing for group file parsing. These changes resulted
**		in a cleaner interface, since this module is now self-contained.
*/

#include "gentoo.h"

#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>

#include "strutil.h"
#include "userinfo.h"

/* ----------------------------------------------------------------------------------------- */

typedef struct {			/* Holds information about system users (maps UIDs to names). */
	GHashTable	*dict_uname;		/* Hashes uid -> uname. */
	GHashTable	*dict_uhome;		/* Hashes uname -> uhome. Handy for "~emil" and stuff. */
	GHashTable	*dict_gname;		/* Hashes gid -> gname. */
} UsrInfo;

static UsrInfo	the_usr = { NULL, NULL, NULL };

/* ----------------------------------------------------------------------------------------- */

static gint cmp_string_num(gconstpointer a, gconstpointer b)
{
	gint	ia, ib;

	if(sscanf(a, "%d", &ia) == 1 && sscanf(b, "%d", &ib) == 1)
		return (ia < ib) ? -1 : (ia > ib);
	return 0;
}

static void list_visit(gpointer key, gpointer value, gpointer user)
{
	*(GList **) user = g_list_insert_sorted(*(GList **) user, g_strdup_printf("%d %s", GPOINTER_TO_INT(key), (const gchar *) value), cmp_string_num);
}

/* 2000-03-21 -	Build a list of strings (not GStrings, but plain old dynamic gchar pointers) of
**		all groups (if category == UIC_USER) or users (UIC_GROUP) we know of. If <index>
**		is non-NULL, it will be filled in with the index for the row describing the group
**		or user whose numerical ID is <id>.
*/
GList * usr_string_list_create(UsrCategory category, gint id, gint *index)
{
	GList	*list = NULL, *iter;

	g_hash_table_foreach(category == UIC_USER ? the_usr.dict_uname : the_usr.dict_gname, list_visit, &list);

	if(index != NULL)		/* Do we need to search? */
	{
		gint	tmp, pos;

		for(iter = list, pos = 0; iter != NULL; iter = g_list_next(iter), pos++)
		{
			if((sscanf(iter->data, "%d", &tmp) == 1) && (tmp == id))
				*index = pos;
		}
	}
	return list;
}

/* 2000-03-21 -	Free a list of strings as created by usr_string_list_create() above. */
void usr_string_list_destroy(GList *list)
{
	GList	*iter;

	for(iter = list; iter != NULL; iter = g_list_next(iter))
		g_free(iter->data);
	g_list_free(list);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-12-22 -	Build a hash table with system's password info in it. This might be very
**		stupid on large systems, in terms of memory usage...
*/
static void scan_uid(void)
{
	struct passwd	*pw;
	gchar		*old, *home, *name;

	setpwent();
	while((pw = getpwent()) != NULL)
	{
		if((old = g_hash_table_lookup(the_usr.dict_uname, GINT_TO_POINTER((gint) pw->pw_uid))) != NULL)
		{
			if((home = g_hash_table_lookup(the_usr.dict_uhome, old)) != NULL)
			{
				g_hash_table_remove(the_usr.dict_uname, GINT_TO_POINTER((gint) pw->pw_uid));
				g_hash_table_remove(the_usr.dict_uhome, old);
				g_free(old);
				g_free(home);
			}
			else
				fprintf(stderr, "USERINFO: passwd data for '%s' (%d) has no home dir!\n", old, (gint) pw->pw_uid);
		}
		if((name = g_strdup(pw->pw_name)) != NULL)
		{
			if((home = g_strdup(pw->pw_dir)) != NULL)
			{
				g_hash_table_insert(the_usr.dict_uname, GINT_TO_POINTER((gint) pw->pw_uid), name);
				g_hash_table_insert(the_usr.dict_uhome, name, home);
			}
			else
			{
				g_free(name);
				fprintf(stderr, "USERINFO: Couldn't duplicate home dir\n");
			}
		}
		else
			fprintf(stderr, "USERINFO: Couldn't duplicate user name string\n");
	}
	endpwent();
}

/* 1998-12-22 -	Parse some system-specific file containing group information (typically "/etc/group").
**		Store group names in a hash table indexed on group ids, for easy look-ups later.
*/
static void scan_gid(void)
{
	struct group	*gr;
	gchar		*old, *name;

	setgrent();
	while((gr = getgrent()) != NULL)
	{
		if((old = g_hash_table_lookup(the_usr.dict_gname, GINT_TO_POINTER((gint) gr->gr_gid))) != NULL)
		{
			g_hash_table_remove(the_usr.dict_gname, GINT_TO_POINTER((gint) gr->gr_gid));
			g_free(old);
		}
		if((name = g_strdup(gr->gr_name)) != NULL)
			g_hash_table_insert(the_usr.dict_gname, GINT_TO_POINTER((gint) gr->gr_gid), name);
	}
	endgrent();
}

/* ----------------------------------------------------------------------------------------- */

/* 2010-02-11 -	Returns the current user's home directory. Implemented according to the
**		guidelines in the glib documentation. Does never return NULL or an empty'
**		string, defaults to "/" if all else fails. Prefers $HOME over passwd.
*/
const gchar * usr_get_home(void)
{
	const char *home = g_getenv ("HOME");
	if(!home)
		home = g_get_home_dir();
	if(!home || *home == '\0')
		return "/";
	return home;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-05-29 -	Rewritten & renamed. This routine looks up the name of the user with the given
**		<uid>. Returns pointer to string, or NULL if the user is unknown.
*/
const gchar * usr_lookup_uname(uid_t uid)
{
	return g_hash_table_lookup(the_usr.dict_uname, GINT_TO_POINTER((gint) uid));
}

struct id_lookup {
	const gchar	*name;		/* The name we're looking for. */
	long		value;		/* The value, if found. */
};

/* 2000-03-23 -	Another g_hash_table_foreach() callback. Cool, because we can reuse
**		it for both user and group lookups.
*/
static void cb_lookup_id(gpointer key, gpointer value, gpointer user)
{
	struct id_lookup	*idl = user;

	if(strcmp(value, idl->name) == 0)
		idl->value = GPOINTER_TO_INT(key);
}

/* 2000-03-23 -	This does the reverse lookup of the function above. I don't really like the
**		name, but it'll do for now. :) Returns the uid, or -1 if the name wasn't
**		found. Oh, and this will be slow, so don't do it often, OK?
*/
long usr_lookup_uid(const gchar *name)
{
	struct id_lookup	idl;

	idl.name  = name;
	idl.value = -1;

	g_hash_table_foreach(the_usr.dict_uname, cb_lookup_id, &idl);

	return idl.value;
}

/* 1998-12-22 -	Look up the home directory of a user called <uname>. Returns pointer to string,
**		or NULL if there is no such user. If <uname> is "" or NULL, returns the home
**		directory of the current effective user.
*/
const gchar * usr_lookup_uhome(const gchar *uname)
{
	if(uname == NULL || *uname == '\0')
	{
		if((uname = usr_lookup_uname(geteuid())) != NULL)
			return g_hash_table_lookup(the_usr.dict_uhome, uname);
	}
	return usr_get_home();
}

/* 1998-05-29 -	Rewritten & renamed. This routine looks up the name of the group with the given
**		<gid>. Returns pointer to string, or NULL if the group is unknown.
*/
const gchar * usr_lookup_gname(gid_t gid)
{
	return g_hash_table_lookup(the_usr.dict_gname, GINT_TO_POINTER(gid));
}

/* 2000-03-23 -	This does the reverse lookup of the function above. I don't really like the
**		name, but it'll do for now. :) Returns the uid, or -1 if the name wasn't
**		found. Oh, and this will be slow, so don't do it often, OK?
*/
long usr_lookup_gid(const gchar *name)
{
	struct id_lookup idl;

	idl.name  = name;
	idl.value = -1;

	g_hash_table_foreach(the_usr.dict_gname, cb_lookup_id, &idl);

	return idl.value;
}

/* ----------------------------------------------------------------------------------------- */

/* Compare two pointers as integers. Can't use g_int_equal(), since that dereferences. */
static gint cmp_int(gconstpointer a, gconstpointer b)
{
	return GPOINTER_TO_INT(a) == GPOINTER_TO_INT(b);
}

/* 1998-12-22 -	Initialize the userinfo module. Calling any of the functions exported from this
**		module without a prior call to usr_init() is illegal, and will likely crash.
**		Returns TRUE on success, a sullen FALSE on failure.
*/
gboolean usr_init(void)
{
	if((the_usr.dict_uname = g_hash_table_new(g_direct_hash, cmp_int)) != NULL)
	{
		if((the_usr.dict_uhome = g_hash_table_new(g_str_hash, g_str_equal)) != NULL)
		{
			if((the_usr.dict_gname = g_hash_table_new(g_direct_hash, cmp_int)) != NULL)
			{
				scan_uid();
				scan_gid();
				return TRUE;
			}
			g_hash_table_destroy(the_usr.dict_uhome);
		}
		g_hash_table_destroy(the_usr.dict_uname);
	}
	return FALSE;
}
