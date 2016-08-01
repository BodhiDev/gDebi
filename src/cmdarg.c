/*
** 1999-05-06 -	This module implements some support for passing arguments to built-in commands.
**		The point is to move as much as possible of the burden of parsing those args out
**		of the actual command implementations, to reduce code duplication and generally
**		keep the commands themselves lean and mean.
*/

#include "gentoo.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>

#include "strutil.h"
#include "cmdarg.h"

/* ----------------------------------------------------------------------------------------- */

/* Determine if given character can be part of a key word. */
#define	IS_KEYWORD(c)	(((c) != '\0') && ((c) != '='))

struct CmdArg {
	GHashTable	*keywords;	/* All KEYWORD=VALUE pairs get hashed into here. */
	GList		*barewords;	/* All other things get listed here. */
};

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-06 -	Compute a hash value for <key>, assuming it to be pointing at the first letter
**		of a "keyword=value" string. Only characters up to (but not including) the '='
**		are included in the hash. Algorithm stolen from glib.
*/
static guint keyword_hash(gconstpointer key)
{
	const gchar	*ptr;
	guint		hash = 0U, g;

	for(ptr = key; (*ptr != '\0') && (*ptr != '='); ptr++)
	{
		hash = (hash << 4) + *ptr;
		if((g = hash & 0xf0000000))
		{
			hash ^= (g >> 24);
			hash ^= g;
		}
	}
	return hash;
}

/* 1999-05-06 -	Compare two keywords <a> and <b>, return TRUE if they're equal. A bit tricky,
**		since a keyword can be terminated either by '\0' or '='. For glib hash table.
**		Note that keywords, rather un-Unixedly, are cAsE iNsenSitIve. Ouch. :)
*/
static gint keyword_equal(gconstpointer a, gconstpointer b)
{
	const gchar	*ap = a, *bp = b;

	for(; IS_KEYWORD(*ap) && IS_KEYWORD(*bp) && (toupper((guchar) *ap) == toupper((guchar) *bp)); ap++, bp++)
		;
	return !(IS_KEYWORD(*ap) && IS_KEYWORD(*bp));
}

/* 1999-05-06 -	Go through the given <argv> vector (typically created by a call to cpr_parse() in
**		the cmdparse module), and build the command argument representation from it. <argv>
**		must be NULL-terminated. Note that we assume that the <argv> data will be around
**		longer than the CmdArg being built here, so we don't need to duplicate any strings.
**		Note: this routine cannot fail. If it returns NULL, that means there were no
**		arguments to the command. That is expected to be a common case, and so worth to
**		optimize a bit. All routines in this module interpret a NULL CmdArg argument as
**		being empty, and react accordingly.
*/
CmdArg * car_create(gchar **argv)
{
	CmdArg	*ca = NULL;

	if((argv != NULL) && (argv[1] != NULL))		/* Safe, because if argv != NULL, argv[0] != NULL too. */
	{
		gchar	*eq;
		guint	i;

		ca = g_malloc(sizeof *ca);
		ca->keywords  = NULL;
		ca->barewords = NULL;
		for(i = 1; argv[i] != NULL; i++)
		{
			if((eq = strchr(argv[i], '=')) != NULL)
			{
				if(ca->keywords == NULL)
					ca->keywords = g_hash_table_new(keyword_hash, keyword_equal);
				g_hash_table_insert(ca->keywords, argv[i], eq + 1);
			}
			else
				ca->barewords = g_list_append(ca->barewords, argv[i]);
		}
		if((ca->keywords != NULL) || (ca->barewords != NULL))
			return ca;
		car_destroy(ca);
	}
	return NULL;
}

/* 1999-05-06 -	Destroy given CmdArg. After this, it is no longer valid. */
void car_destroy(CmdArg *ca)
{
	if(ca != NULL)
	{
		if(ca->keywords != NULL)
			g_hash_table_destroy(ca->keywords);
		if(ca->barewords != NULL)
			g_list_free(ca->barewords);
		g_free(ca);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-06 -	Return the value given with <keyword>, or <default_value> if not present. */
const gchar * car_keyword_get_value(const CmdArg *ca, const gchar *keyword, const gchar *default_value)
{
	if((ca != NULL) && (keyword != NULL))
	{
		if(ca->keywords != NULL)
			return g_hash_table_lookup(ca->keywords, keyword);
	}
	return default_value;
}

/* 2009-07-03 -	Return the integer value given with <keyword>, or <default_value> if not present.
**		Note that the value returned is signed, "foo=-12" is fine.
*/
gint car_keyword_get_integer(const CmdArg *ca, const gchar *keyword, gint default_value)
{
	if((ca != NULL) && (keyword != NULL))
	{
		const gchar	*value = car_keyword_get_value(ca, keyword, NULL);

		if(value != NULL)
		{
			gchar	*eptr;
			long	v;

			v = strtol(value, &eptr, 10);
			if(eptr > value)
				return v;
		}
	}
	return default_value;
}

/* 1999-05-06 -	Match the value of <keyword> to an enumerated list of strings, and return the index
**		of the string that matched.
**		The var-args part should be a list of strings, the index of which is returned on a
**		successful match. In case there is no match, default_map is returned.
**		Example: if the command string was "SelectRE set=unselected re=*.jpg", and you call
**		this with: car_map_keyword(ca, "set", 0, "all", "selected", "unselected", NULL), it
**		will return 2, that being the index of "unselected". It's really not that hard! :)
*/
gint car_keyword_get_enum(const CmdArg *ca, const gchar *keyword, gint default_value, ...)
{
	gint	ret = default_value;

	if((ca != NULL) && (keyword != NULL))
	{
		const gchar	*value;

		if((value = car_keyword_get_value(ca, keyword, NULL)) != NULL)
		{
			gint		index;
			const gchar	*word;
			va_list		arg;

			va_start(arg, default_value);
			for(index = 0; (word = va_arg(arg, const gchar *)) != NULL; index++)
			{
				if(strcmp(value, word) == 0)
				{
					ret = index;
					break;
				}
			}
			va_end(arg);
		}
	}
	return ret;
}

/* 1999-05-07 -	Check if the value for <keyword> was either true or false. The tokens "", "yes",
**		"true", "1" and "on" all map to TRUE, any other to FALSE. Note that since the
**		return type is really guint, it's legal to have a default value outside of {0, 1}.
**		This is not only cool, its sometimes even useful.
** 2004-12-26 -	Make sure comparison is always done against English strings, too.
*/
guint car_keyword_get_boolean(const CmdArg *ca, const gchar *keyword, guint default_value)
{
	if((ca != NULL) && (keyword != NULL))
	{
		const gchar	*value;

		if((value = car_keyword_get_value(ca, keyword, NULL)) != NULL)
		{
			const gchar	*yes[] = { "", N_("yes"), N_("true"), N_("on"), "1" };
			guint		i;

			for(i = 0; i < sizeof yes / sizeof yes[0]; i++)
			{
				if(strcmp(value, yes[i]) == 0)
					return TRUE;
				if(strcmp(value, _(yes[i])) == 0)
					return TRUE;
			}
			return FALSE;
		}
	}
	return default_value;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-11 -	Return the number of barewords in <ca>. */
guint car_bareword_get_amount(const CmdArg *ca)
{
	if((ca != NULL) && (ca->barewords != NULL))
		return g_list_length(ca->barewords);
	return 0U;
}

/* 1999-05-11 -	Return the <index>'th bareword, or NULL. */
const gchar * car_bareword_get(const CmdArg *ca, guint index)
{
	if((ca != NULL) && (index < car_bareword_get_amount(ca)))
		return g_list_nth_data(ca->barewords, index);
	return NULL;
}

/* 1999-05-11 -	Check whether <word> is among the barewords in <ca> or not. Ignores case. */
gboolean car_bareword_present(const CmdArg *ca, const gchar *word)
{
	if((ca != NULL) && (ca->barewords != NULL) && (word != NULL))
	{
		GList	*iter;

		for(iter = ca->barewords; iter != NULL; iter = g_list_next(iter))
		{
			if(strcmp(iter->data, word) == 0)
				return TRUE;
		}
	}
	return FALSE;
}

/* 1999-05-11 -	Do something similar to car_keyword_get_enum(), but on bareword number <index>. */
guint car_bareword_get_enum(const CmdArg *ca, guint index, guint default_value, ...)
{
	guint	ret = default_value;

	if((ca != NULL) && (index < car_bareword_get_amount(ca)))
	{
		const gchar	*bw = g_list_nth_data(ca->barewords, index);

		if(bw != NULL)
		{
			const gchar	*word;
			va_list		arg;

			va_start(arg, default_value);
			for(index = 0; (word = va_arg(arg, const gchar *)) != NULL; index++)
			{
				if(strcmp(bw, word) == 0)
				{
					ret = index;
					break;
				}
			}
			va_end(arg);
		}
	}
	return ret;
}
