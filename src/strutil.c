/*
** 1998-08-02 -	This module holds various utility functions sharing one common trait:
**		they all deal with character strings in one way or another.
*/

#include "gentoo.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sizeutil.h"
#include "strutil.h"

/* ----------------------------------------------------------------------------------------- */

/* 2002-04-28 -	Create a textual representation of <n>, with <tick> inserted every
**		3rd digit (counting from the right) into <buf>. Returns pointer to
**		first digit.
** NOTE NOTE	That <buf> pointer should point at one position past where the **last**
**		character of the tickified number is to appear. This is for speed reasons.
*/
gchar * stu_tickify(gchar *buf, guint64 n, gchar tick)
{
	register gint	cnt = 0;

	do
	{
		if(tick && cnt == 3)
			*--buf = tick, cnt = 0;
		*--buf = '0' + n % 10;
		n /= 10;
		cnt++;
	} while(n);
	return buf;
}

/* ----------------------------------------------------------------------------------------- */

/* Attempt to find <string> in <vector> of strings. Returns vector index, or <def>. */
gint stu_strcmp_vector(const gchar *string, const gchar **vector, gsize vector_size, gint def)
{
	gsize	i;

	for(i = 0; i < vector_size && vector[i] != NULL; ++i)
	{
		if(strcmp(string, vector[i]) == 0)
			return (gint) i;
	}
	return def;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-30 -	Core glob->RE translator. Returns a pointer to a (dynamically allocated)
**		piece of memory holding the RE. When done with that, please g_free() it.
*/
gchar * stu_glob_to_re(const gchar *glob)
{
	GString		*re;
	gchar		here, *ret;
	const gchar	*ptr, *end;

	if((re = g_string_new(NULL)) == NULL)
		return NULL;

	for(ptr = glob; *ptr != '\0'; ptr++)
	{
		here = *ptr;
		if(here == '[' && ptr[1] != ']')		/* Character set begins? */
		{
			if((end = strchr(ptr + 1, ']')) != NULL)
			{
				for(; ptr <= end; ptr++)
					g_string_append_c(re, *ptr);
				ptr--;
			}
		}
		else
		{
			switch(here)
			{
				case '.':
					g_string_append(re, "\\.");
					break;
				case '*':
					g_string_append(re, ".*");
					break;
				case '+':
					g_string_append(re, "\\+");
					break;
				case '?':
					g_string_append_c(re, '.');
					break;
				default:
					g_string_append_c(re, here);
			}
		}
	}
	ret = re->str;
	g_string_free(re, FALSE);	/* Keeps the buffer. */
	return ret;
}

/* 1998-08-30 -	Translate a glob pattern into a System V8 regular expression. Thanks to the
**		magic of GStrings, it will look to the caller as if the string is simply
**		replaced by the translation. This code was moved from the good 'ol cmd_select
**		module, since I wanted glob->RE translation in other places, too (types).
**		This routine doesn't exactly fit in among the other, but since it deals with
**		strings, I thought it could live here at least for a while.
*/
void stu_gstring_glob_to_re(GString *glob)
{
	gchar	*re;

	if((re = stu_glob_to_re(glob->str)) != NULL)
	{
		g_string_assign(glob, re);
		g_free(re);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-19 -	Convert the protection <mode> to a (more) human-readable form stored at <buf>.
**		Will not use more than <max> bytes of <buf>. Returns <buf>, or NULL on failure.
** 1999-01-05 -	Finally sat down and experimentally deduced the way GNU 'ls' formats its mode
**		strings, and did something similar here.
** 2000-09-14 -	g_snprintf() is not scanf(). Remembered that.
*/
gchar * stu_mode_to_text(gchar *buf, gsize buf_max, mode_t mode)
{
	gchar	*grp[] = { "---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx" };
	gint	u, g, o;

	if(buf_max < 12)			/* A lazy size limitation. */
		return NULL;

	u = (mode & S_IRWXU) >> 6;
	g = (mode & S_IRWXG) >> 3;
	o = (mode & S_IRWXO);
	if(g_snprintf(buf, buf_max, "-%s%s%s", grp[u], grp[g], grp[o]) < 0)
		return NULL;

	/* Set the left-most character according to the file's intrinsic type. */
	if(S_ISLNK(mode))
		buf[0] = 'l';
	else if(S_ISDIR(mode))
		buf[0] = 'd';
	else if(S_ISBLK(mode))
		buf[0] = 'b';
	else if(S_ISCHR(mode))
		buf[0] = 'c';
	else if(S_ISFIFO(mode))
		buf[0] = 'p';
	else if(S_ISSOCK(mode))		/* This is just a guess... */
		buf[0] = 's';

	/* This is magic until you understand how it works. The trick seems to be that one
	** bit (e.g. "SETUID") is displayed on top of another bit (in this case user read)
	** by changing that character either to 'S' (if it was not set) or 's' (if set).
	** AFAIK, this is not documented anywhere (except perhaps in ls's source).
	*/
	if(mode & S_ISVTX)		/* Sticky bit set? This is not POSIX... */
		buf[9] = (buf[9] == '-') ? 'T' : 't';
	if(mode & S_ISGID)		/* Set GID bit set? */
		buf[6] = (buf[6] == '-') ? 'S' : 's';
	if(mode & S_ISUID)		/* Set UID bit set? */
		buf[3] = (buf[3] == '-') ? 'S' : 's';

	return buf;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-10-07 -	Scan a string from <def>, and put a pointer to a dynamically allocated version
**		of it into <str>. The string should be delimited by double quotes. Characters
**		(such as commas and whitespace) between strings are ignored. Returns a pointer
**		to the beginning of the next string (suitable for a repeat call), or NULL when
**		no more strings were found.
** 1999-02-24 -	Added support for backslash escaping. Might be useful when this routine is used
**		to scan strings which are then parsed by the command argument stuff. Or, you
**		could just use single quotes of course...
*/
const gchar * stu_scan_string(const gchar *def, const gchar **str)
{
	GString	*tmp;

	if((def == NULL) || (str == NULL))
		return NULL;

	while(*def && *def != '"')
		def++;

	if(*def == '"')			/* Beginning of string actually found? */
	{
		def++;
		if((tmp = g_string_new(NULL)) != NULL)
		{
			while(*def && *def != '"')
			{
				if(*def == '\\')
				{
					def++;
					if(*def == '\0')
						break;
				}
				g_string_append_c(tmp, *def++);
			}
			if(*def == '"')		/* Closing quote here, too? */
			{
				*str = tmp->str;
				g_string_free(tmp, FALSE);				
				return ++def;	/* Then return with an OK status. */
			}
			g_string_free(tmp, TRUE);
		}
	}
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-02-24 -	Compute the length and content of the first word at <str>. Knows about
**		quoting and backslash escapes. Stores word at <store>. Will typically be
**		run twice on the same input, since you can't know how much space is going
**		to be needed without running it once (with store == NULL).
**		Quoting rules:	A word can contain whitespace (space, tab) only if quoted.
**				Double (") and single (') quotes can both be used, and have
**				the same "power". One quotes the other, so "'" and '"' are
**				both legal 1-character words. To include the quote used for
**				a word IN the word, it must be backslash escaped: "\"" is
**				the 1-character word ".
**		Returns pointer to first character after word, or NULL if there are no more
**		words. Stores word length at <len> (if non-NULL).
*/
const gchar * stu_word_length(const gchar *str, gsize *len, gchar *store)
{
	gchar	quote = 0, here;
	gsize	l = 0;

	if(str == NULL)
		return NULL;

	while(*str && isspace((guchar) *str))		/* Skip inter-word spaces. */
		str++;

	if(*str == '\0')
		return NULL;
	for(; *str && !(quote == 0 && isspace((guchar) *str)); l++)
	{
		if((here = *str) == '\\')	/* Backslash escapade? */
		{
			here = *++str;
			if(here == '\0')	/* At end of string? */
				break;
		}
		else if(here == '\'' || here == '"')
		{
			if(quote == 0 || quote == here)	/* Ignore "other" quote. */
			{
				if(quote == here)
					quote = 0;
				else
					quote = here;
				str++;
				l--;			/* Don't count the quote. */
				continue;		/* Avoid storing the quote. */
			}
		}
		if(store != NULL)
			*store++ = here;
		str++;
	}
	if(quote != '\0' && *str == quote)	/* Skip ending quote. */
		str++;
	if(len != NULL)
		*len = l;

	return str;
}

/* 1999-02-24 -	This takes a string intended as a shell command and splits it into a word-
**		vector as used by exec() functions. Think argv[]. Handles some quoting and
**		escaped characters, too. The returned vector will be NULL-terminated, and
**		can be freed by a single call to g_free().
*/
gchar ** stu_split_args(const gchar *argstring)
{
	gsize		wlen;
	gint		wtotlen, wnum, i;
	const gchar	*ptr = argstring;
	gchar		**argv, *store;

	for(wnum = wlen = wtotlen = 0; (ptr = stu_word_length(ptr, &wlen, NULL)) != NULL; wnum++, wtotlen += wlen + 1)
		;

	if(wnum == 0)		/* Nothing found? */
		return NULL;

	argv  = g_malloc((wnum + 1) * sizeof *argv + wtotlen);
	store = (gchar *) argv + (wnum + 1) * sizeof *argv;
	for(ptr = (gchar *) argstring, i = 0; (ptr = stu_word_length(ptr, &wlen, store)) != NULL; i++)
	{
		argv[i] = store;
		store[wlen] = '\0';
		store += (wlen + 1);
	}
	argv[i] = NULL;

	return argv;
}

/* ----------------------------------------------------------------------------------------- */

/* 2003-11-25 -	Create internal (static, be careful!) version of <string> where certain
**		characters have been escaped by backslashes, and return pointer to it.
*/
const gchar * stu_escape(const gchar *string)
{
	static GString	*str = NULL;

	if(str == NULL)
		str = g_string_new("");
	else
		g_string_truncate(str, 0);
	for(; *string; string++)
	{
		if(*string == '"' || *string == '\'' || *string == '\\')
			g_string_append_c(str, '\\');
		g_string_append_c(str, *string);
	}
	return str->str;
}

/* ----------------------------------------------------------------------------------------- */

/* 2009-10-11 -	Do a simple "search and replace", on a single input string. The text in <find>
**		is searched for, and replaced with <replace>. If <global>, all instances are
**		replaced, otherwise only the first.
**
**		Note: All strings are assumed to be UTF-8 encoded.
**		Example: input="fr�knar �r inte �ckligt", find="�", replace="e"
*/
guint stu_replace_simple(GString *output, const gchar *input, const gchar *find, const gchar *replace, gboolean global, gboolean nocase)
{
	gboolean	busy;
	gsize		flen;
	guint		count = 0;

	if(output == NULL || input == NULL || find == NULL || replace == NULL)
		return count;

	/* Make sure output is clean. */
	g_string_truncate(output, 0);
	flen = strlen(find);

	do
	{
		gchar	*hit;

		/* Plain old strstr() works on UTF-8. */
		if((hit = strstr(input, find)) != NULL)
		{
			gsize	pfx = hit - input;
			g_string_append_len(output, input, pfx);
			input += flen + pfx;
			g_string_append(output, replace);
			busy = global;
			count += 1;
		}
		else
			busy = FALSE;
	} while(busy);
	/* There might be a trailing tail, here. */
	g_string_append(output, input);

	return count;
}

/* ----------------------------------------------------------------------------------------- */

/* 2010-03-07 -	Tries to look up a character in the given string, by calling the filtering function for each
**		character. If the function returns true, a pointer to the character is returned, else NULL.
*/
const gchar * stu_utf8_find(const gchar *string, gboolean (*function)(gunichar ch, gpointer user), gpointer user)
{
	if(string == NULL || function == NULL)
		return NULL;

	while(*string != '\0')
	{
		gunichar	here = g_utf8_get_char(string);

		if(function(here, user))
			return string;
		string = g_utf8_next_char(string);
	}
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 2010-12-03 -	Interpolate things looking like "{this}" by replacing with the value for the key "this" from
**		the dictionary. Fun for the whole family. To get an explicit brace, escape with backslash
**		(which has the power to escape anything, for simplicity).
*/
gboolean stu_interpolate_dictionary(gchar *out, gsize size, const gchar *format, const GHashTable *dictionary)
{
	gchar		key[32], *kput = NULL, here, *out_end;
	gboolean	in_key = FALSE;
	gconstpointer	value;

	/* A NULL dictionary can be okay, but not if it's referenced. Lazy. */
	if(out == NULL || size <= 1 || format == NULL)
		return FALSE;

	out_end = out + size - 1;
	while((here = *format++) != '\0' && out < out_end)
	{
		if(in_key)
		{
			if(here == '}')
			{
				*kput = '\0';
				/* If we got this far without a hash, fail. */
				if(!dictionary)
					return FALSE;
				if((value = g_hash_table_lookup((GHashTable *) dictionary, key)) != NULL)
				{
					const gsize	vlen = strlen(value);

					if(vlen <= (out_end - out))
					{
						strcpy(out, value);
						out += vlen;
					}
				}
				in_key = FALSE;
			}
			else if(kput < (key + sizeof key - 1))	/* Silently drop overflowing key characters. */
				*kput++ = here;
			continue;
		}
		else if(here == '{')
		{
			kput = key;
			in_key = TRUE;
			continue;
		}
		else if(here == '\\')
		{
			if(*format != '\0')
			{
				*out++ = *format;
				format++;
				continue;
			}
			else
				break;
		}
		*out++ = here;
	}
	*out = '\0';

	return TRUE;
}
