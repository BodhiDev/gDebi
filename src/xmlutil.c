/*
** 1998-07-25 -	Various utility functions for dealing with XML files. Does both writing
**		(xml_put_XXX) and reading (xml_get_XXX). Not meant to be beatiful, but
**		rather utalitarian.
** 1998-07-27 -	Rewrote the xml_put_XXX() stuff, and actually got it rather neat (IMHO).
**		Also made sure all three config-modules actually use these functions (and
**		nothing BUT these functions) to write their config stuff.
** 1998-08-10 -	Now quotes all strings. Escapes any embedded quotes as \". This was re-
**		quired in order to support explicitly empty strings, which were previously
**		very ambigous and difficult to detect & handle correctly.
** 1999-01-04 -	Discovered a few old-style (incorrect, misguided) uses of g_slist_XXX()
**		functions, and fixed them.
** 1999-04-04 -	Increased use of glib types, fixed minor bug in indentation (>=).
** 1999-04-05 -	Added new value type, the unsigned integer. Identified in files by simply
**		prefixing the actual value with the letter 'u'.
*/

#include "gentoo.h"

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gdk/gdk.h>

#include "strutil.h"
#include "xmlutil.h"

/* ----------------------------------------------------------------------------------------- */

/* Types of values held by leaf nodes in trees. */
typedef enum { XVT_NONE, XVT_TEXT, XVT_INTEGER, XVT_UINTEGER, XVT_REAL, XVT_BOOL, XVT_COLOR } XVType;

typedef struct {
	XVType	type;
	GString	*value;
} XVText;

typedef struct {
	XVType	type;
	gint	value;
} XVInt;

typedef struct {
	XVType	type;
	guint	value;
} XVUInt;

typedef struct {
	XVType	type;
	gfloat	value;
} XVReal;

typedef struct {
	XVType	 type;
	gboolean value;
} XVBool;

typedef struct {		/* Um, perhaps kind of clumsy. But it saves space. */
	XVType	type;
       GdkColor	value;
} XVColor;

typedef union {
	XVType	type;		/* Should "collide" with type fields of actual value-structs. */
	XVText	text;
	XVInt	integer;
	XVUInt	uinteger;
	XVReal	real;
	XVBool	bool;
	XVColor	color;
} XmlData;

#define	XML_NAME_SIZE	(32)

struct XmlNode {
	gchar	name[XML_NAME_SIZE];	/* Name from surrounding tag. */
	XmlData	data;			/* Data for this node (only valid for leafs). */
	GSList	*children;		/* List of subtags in file order. */
};

/* ----------------------------------------------------------------------------------------- */

#define	THE_ONE_TRUE_TAB_SIZE	(8)	/* ;^) */

#define	XML_TRUE		"TRUE"
#define	XML_FALSE		"FALSE"
#define	XML_UNSIGNED		"u"		/* Prefix for unsigned integers. */
#define	XML_COLOR		"C"		/* Prefix for GdkColor RGB triplets. */
#define	NODE_INTERNAL(tag)	(isupper((guchar) tag[0]) || (tag[0] == '/' && isupper((guchar) tag[1])))
#define	NODE_COMMENT(tag)	(tag[0] == '?')
#define	SIGN(x)	(((x) == 0) ? 0 : (((x) > 0) ? 1 : -1))

/* Here are a few escape sequences for various characters that need protection. I haven't
** really added much here. Feel free to contribute.
*/
static	struct {
		gchar	*sequence;
		gchar	equiv;
	} esc_seq[] = {	{"&lt;", '<'},		{"&gt;", '>'},		{"&amp;", '&'},
			{"&apos;", '\''},	{"&quot;", '"'},
		     };
#define	NUM_ESC_SEQ	(sizeof esc_seq / sizeof esc_seq[0])

/* ----------------------------------------------------------------------------------------- */

/* This little global variable (shudder) holds the nesting level for tags in the file being
** written by xml_put_XXX() functions.
*/
static guint	put_level = 0;

/* ----------------------------------------------------------------------------------------- */

static void	put_string(FILE *out, const gchar *str);

/* ----------------------------------------------------------------------------------------- */

/* 1998-07-27 -	Open a file for output, and write any required XML header to it. Then return the
**		file pointer, so the user can use it in further calls to this module, in order to
**		store various nodes and leafs. When done, the user should call xml_put_close().
**		BUG	Calls to xml_put_open() and xml_put_close() don't nest!! Sue me!
*/
FILE * xml_put_open(const gchar *name)
{
	FILE	*out;

	if((out = fopen(name, "wt")) != NULL)
	{
		fprintf(out, "<?xml version=\"1.0\" standalone=\"yes\"?>\n\n");
		put_level = 0;	/* The reason we don't nest. */
	}
	return out;
}

/* 1998-07-27 -	Close the current XML output file. */
void xml_put_close(FILE *out)
{
	if(put_level != 0)
		fprintf(stderr, "**XML Put Error: xml_put_close() called on level %d\n", put_level);
	fclose(out);
}

/* 1998-07-27 -	Output indentation for current level. Smart enough to use TAB characters when
**		possible.
*/
static void indent(FILE *out)
{
	guint	i;

	for(i = put_level; i >= THE_ONE_TRUE_TAB_SIZE; i -= THE_ONE_TRUE_TAB_SIZE)
		fputc('\t', out);
	for(; i > 0; i--)
		fputc(' ', out);
}

/* 1998-07-27 -	Output an opening node, i.e. something that (possibly) has children. */
void xml_put_node_open(FILE *out, const gchar *node)
{
	indent(out);
	fprintf(out, "<%s>\n", node);
	put_level++;
}

/* 1998-07-27 -	Close a node previously opened. Does not protect against overlapping
**		nodes.
*/
void xml_put_node_close(FILE *out, const gchar *node)
{
	if(put_level > 0)
	{
		put_level--;
		indent(out);
		fprintf(out, "</%s>\n", node);
	}
	else
		fprintf(stderr, "**XML Put Error: xml_put_node_close() called on level 0!\n");
}

/* 1998-07-27 -	Output a leaf with an integer in it. */
void xml_put_integer(FILE *out, const gchar *name, gint value)
{
	indent(out);
	fprintf(out, "<%s>%d</%s>\n", name, value, name);
}

/* 1999-04-05 -	Output unsigned leaf. Note use of prefix to resolve it from plain integer. */
void xml_put_uinteger(FILE *out, const gchar *name, guint value)
{
	indent(out);
	fprintf(out, "<%s>%s%u</%s>\n", name, XML_UNSIGNED, value, name);
}

/* 1998-07-27 -	Output a leaf with a real number.
** 2002-06-16 -	Significantly more complex, since we need to do this in ASCII. glib 2.0, please.
** 2014-12-25 -	Somewhat later: now we really can assume glib 2.x. Right now, it's 2.42.1.
*/
void xml_put_real(FILE *out, const gchar *name, gfloat value)
{
	gchar	buf[G_ASCII_DTOSTR_BUF_SIZE + 1];

	indent(out);
	g_ascii_dtostr(buf, sizeof buf, value);
	fprintf(out, "<%s>%s</%s>\n", name, buf, name);
}

/* 1998-07-27 -	Output a leaf with a boolean in it. */
void xml_put_boolean(FILE *out, const gchar *name, gboolean value)
{
	indent(out);
	fprintf(out, "<%s>%s</%s>\n", name, value ? XML_TRUE : XML_FALSE, name);
}

/* 1998-07-27 -	Output a text leaf. Might screw up the indentation if the text contains
**		line feeds. Big deal.
*/
void xml_put_text(FILE *out, const gchar *name, const gchar *value)
{
	indent(out);
	fprintf(out, "<%s>\"", name);
	put_string(out, value);
	fprintf(out, "\"</%s>\n", name);
}

/* 1999-05-01 -	Write out a GdkColor. Kind of unusual, perhaps (and very application-
**		specific), but a lot more efficient than doing it with another nested
**		node. Note that we save only the red, green & blue components.
*/
void xml_put_color(FILE *out, const gchar *name, const GdkColor *color)
{
	indent(out);
	fprintf(out, "<%s>%s%04X,%04X,%04X</%s>\n", name, XML_COLOR, color->red, color->green, color->blue, name);
}

/* 1998-07-25 -	Write string <str> to file <out>, while attempting to catch any character
**		we don't want appearing between tags (such as < and >). This is overly
**		paranoid; usually you don't need to protect the '>' character.
*/
static void put_string(FILE *out, const gchar *str)
{
	gchar	here;
	gint	i;

	for(; str && (here = *str) != '\0'; str++)
	{
		for(i = 0; i < NUM_ESC_SEQ; i++)
		{
			if(here == esc_seq[i].equiv)
			{
				fprintf(out, "%s", esc_seq[i].sequence);
				break;
			}
		}
		if(i == NUM_ESC_SEQ)	/* No escape sequence found? */
		{
			if(here == '\\')
				fprintf(out, "\\\\");
			else
				putc(here, out);
		}
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-01-30 -	Find a child of <node> named <name>, having a value of type <type>. */
static const XmlNode * get_node(const XmlNode *node, const gchar *name, XVType type)
{
	const XmlNode	*data;

	if(node == NULL || name == NULL)
		return NULL;
	if(((data = xml_tree_search(node, name)) != NULL) && (data->data.type == type))
		return data;
	return NULL;
}

/* 1998-12-25 -	Look through <node>'s children for a node named <name>. When found, store its
**		data (which really should be integer) in <n>, and return 1. If any of these
**		steps fail, return 0. Can be called with <n> == NULL to check for the presence
**		of a correctly typed and named value.
*/
gboolean xml_get_integer(const XmlNode *node, const gchar *name, gint *n)
{
	const XmlNode	*data;

	if((data = get_node(node, name, XVT_INTEGER)) != NULL)
	{
		if(n != NULL)
			*n = data->data.integer.value;
		return TRUE;
	}
	return FALSE;
}

gboolean xml_get_uinteger(const XmlNode *node, const gchar *name, guint *n)
{
	const XmlNode	*data;

	if((data = get_node(node, name, XVT_UINTEGER)) != NULL)
	{
		if(n != NULL)
			*n = data->data.uinteger.value;
		return TRUE;
	}
	return FALSE;
}

/* 1998-12-25 -	Search through <node>'s children for a real-valued node named <name>, and store
**		the number in question at <x>. Returns 1 if value was found, 0 otherwise.
*/
gboolean xml_get_real(const XmlNode *node, const gchar *name, gfloat *x)
{
	const XmlNode	*data;

	if((data = get_node(node, name, XVT_REAL)) != NULL)
	{
		if(x != NULL)
			*x = data->data.real.value;
		return TRUE;
	}
	return FALSE;
}

/* 1998-12-25 -	Search <node>'s kids for a boolean value named <name>. Returns 1 if the named
**		value was indeed found (storing it in <x> if it's non-NULL), 0 otherwise.
*/
gboolean xml_get_boolean(const XmlNode *node, const gchar *name, gboolean *x)
{
	const XmlNode	*data;

	if((data = get_node(node, name, XVT_BOOL)) != NULL)
	{
		if(x != NULL)
			*x = data->data.bool.value;
		return TRUE;
	}
	return FALSE;
}

/* 1998-12-25 -	Look through <node>'s children for a node named <name>, having type
**		text. If found, return 1 while storing a pointer to the text at <str>
**		(if non-NULL). Otherwise returns 0.
*/
gboolean xml_get_text(const XmlNode *node, const gchar *name, const gchar **str)
{
	const XmlNode	*data;

	if((data = get_node(node, name, XVT_TEXT)) != NULL)
	{
		if(str != NULL)
			*str = data->data.text.value->str;
		return TRUE;
	}
	return FALSE;
}

gboolean xml_get_color(const XmlNode *node, const gchar *name, GdkColor *col)
{
	const XmlNode	*data;

	if((data = get_node(node, name, XVT_COLOR)) != NULL)
	{
		if(col != NULL)
			*col = data->data.color.value;
		return TRUE;
	}
	return FALSE;
}

/* 1998-12-25 -	Look through <node>'s children for a node named <name>, hoping it
**		contains text. If found, and <str> is non-NULL, copy the text there,
**		using no more than <max> characters. Returns 1 on success, or 0.
*/
gboolean xml_get_text_copy(const XmlNode *node, const gchar *name, gchar *str, gsize max)
{
	const XmlNode	*data;

	if((data = get_node(node, name, XVT_TEXT)) != NULL)
	{
		if(str != NULL)
			g_strlcpy(str, data->data.text.value->str, max);
		return TRUE;
	}
	return FALSE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-07-26 -	Report some kind of parse error to the (probably ignorant) user. If any of
**		these errors start popping up, the config file is likely damaged (or edited:).
*/
static void xml_error(gchar *fmt, ...)
{
	va_list	arg;

	va_start(arg, fmt);
	fprintf(stderr, "**XML: ");
	vfprintf(stderr, fmt, arg);
	fputc('\n', stderr);
	va_end(arg);
}

/* 1998-07-25 -	Get the next "token" and return it. I define a token as either a tag (opening
**		or closing), or as an arbitrary amound of characters found between tags. Note
**		that tokens may well include whitespace. Example: "<a> hello </a>" consists of
**		three tokens; "a", " hello " (note the spaces) and "/a". The first and the last
**		would be considered tags.
**		Returns a GString holding the token, which really should be destroyed by the
**		caller when done. Also returns an integer in <what> indicating the type of
**		the string returned; this is 0 for user text, 1 for a tag, and -1 on error.
*/
static GString * get_token(FILE *in, gint *what)
{
	GString	*str;
	gint	here, in_tag = 0, found_nsc = 0, in_string = 0;

	if(what == NULL)
		return NULL;
	*what = -1;			/* Assume the worst. */

	if((str = g_string_new(NULL)) != NULL)
	{
		while((here = fgetc(in)) >= 0)
		{
			if(!in_string && here == '<')		/* Either beginning of tag or end of user data. */
			{
				if(found_nsc)			/* Any non-space characters found? */
				{
					*what = 0;
					ungetc(here, in);
					return str;
				}
				else
				{
					g_string_truncate(str, 0);
					in_tag = 1;
				}
			}
			else if(!in_string && here == '>')	/* *Must* be end of tag. */
			{
				if(in_tag)
				{
					*what = 1;
					return str;
				}
				else
				{
					xml_error("Unmatched '>' detected");
					break;
				}
			}
			else
			{
				if(here == '\\')
				{
					if((here = fgetc(in)) < 0)
						break;
					g_string_append_c(str, '\\');
					g_string_append_c(str, here);
					continue;
				}
				g_string_append_c(str, here);
				found_nsc |= !isspace((guchar) here);
				in_string ^= (here == '"');
			}
		}
	}
	g_string_free(str, TRUE);
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-07-25 -	Allocate and initialize a new node structure. */
XmlNode * xml_node_new(const gchar *name)
{
	XmlNode	*node;

	if((node = g_malloc(sizeof *node)) != NULL)
	{
		if(name != NULL)
			g_strlcpy(node->name, name, sizeof node->name);
		else
			node->name[0] = '\0';
		node->data.type = XVT_NONE;
		node->children  = NULL;
	}
	return node;
}

/* 1998-12-25 -	Check if the given node is called <name>. If so, return 1.
**		Otherwise return 0.
*/
gint xml_node_has_name(const XmlNode *node, const gchar *name)
{
	if(node != NULL && name != NULL)
		return strcmp(node->name, name) == 0;
	return 0;
}

/* 1998-07-25 -	Add <child> as a child to the node <node>.
** 1999-01-04 -	Noted that this still used my old misguided way of using the glib list
**		functions. Fixed.
*/
void xml_node_add_child(XmlNode *node, XmlNode *child)
{
	if((node->children = g_slist_append(node->children, child)) == NULL)
		xml_error("Couldn't build child list for '%s'", node->name);
}

/* 1998-07-26 -	Visit all of a node's immediate children, using a callback. This is not
**		recursive, since the intent is to leave that to the user, who may want
**		to use different callback functions for different nodes.
** 1999-01-04 -	This too used the old bad way of using glib's list functions. Fixed.
*/
void xml_node_visit_children(const XmlNode *node, void (*func)(const XmlNode *child, gpointer user), gpointer user)
{
	const GSList	*iter;

	for(iter = node->children; iter != NULL; iter = g_slist_next(iter))
		func(iter->data, user);
}

/* 1998-07-26 -	Destroy a node. Knows how to destroy string data. You really shouldn't destroy a
**		node that has live children, since it will become (infinitely) more difficult to
**		access those children when their parent has been destroyed!
*/
void xml_node_destroy(XmlNode *node)
{
	if(node->data.type == XVT_TEXT)
		g_string_free(node->data.text.value, TRUE);
	if(node->children != NULL)
		g_slist_free(node->children);
	g_free(node);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-07-26 -	Returns TRUE if the given strings are a matched tag pair, FALSE otherwise. */
static gint match_tags(const gchar *beg, const gchar *end)
{
	if(beg[0] == '/')
		return FALSE;
	if(end[0] != '/')
		return FALSE;
	return strcmp(beg, end + 1) == 0;
}

/* 1998-07-26 -	A fancy strcmp() that knows how to skip whitespace. Checks if <str> is
**		equal to <word>, ignoring any whitespace. Case-sensitive.
*/
static gint is_word(const gchar *str, const gchar *word)
{
	while(isspace((guchar) *str))
		str++;
	return strcmp(str, word) == 0;
}

/* 1998-07-27 -	Parse a real number from <str>, and store it in <p>. Requires that the
**		number contains exactly one period ('.') used to separate integer and
**		fractional parts. Doesn't give a damn about i18n, obviously. This function
**		is required since you can't "stack" sscanf() calls with formats of "%f
**		and "%d" the way I want to... :(
** 1998-07-31 -	Bug fix: attempted to parse things beginning with a period as floats,
**		thus clearly blurring the line between text and floats. This means that
**		reals are now required to contain at least one digit before the dot.
*/
static gboolean get_real(const gchar *str, gfloat *p)
{
	const gchar	*beg = str;
	gfloat		num = 0.0f, fract, weight;
	gint		sign = 1;

	if(*str == '-')
	{
		sign = -1;
		str++;
	}
	while(isdigit((guchar) *str))
	{
		num *= 10;
		num += *str - '0';
		str++;
	}
	if(str == beg)		/* If no digits were found, it's not a real anymore. */
		return FALSE;
	if(*str == '.')
	{
		for(str++, fract = 0.0f, weight = 0.1f; isdigit((guchar) *str); str++, weight /= 10.0f)
			fract += (*str - '0') * weight;
		*p = sign * (num + fract);
		return TRUE;
	}
	return FALSE;
}

/* 1998-07-27 -	Create a duplicate of the string data in <in>, while taking care to replace any &-escaped
**		"sequences" (such as "&amp;") with their single-character equivalences. Note that we don't
**		even pretend to support all XML sequences.
*/
static GString * get_text_data(const gchar *in)
{
	GString	*str;
	gchar	here, next;
	guint	i;

	if((str = g_string_new(NULL)) != NULL)
	{
		if(*in == '"')
			in++;
		for(; (here = *in) != '\0'; in++)
		{
			if(here == '&')		/* Special sequence here? */
			{
				for(i = 0; i < NUM_ESC_SEQ; i++)
				{
					if(strncmp(in, esc_seq[i].sequence, strlen(esc_seq[i].sequence)) == 0)
					{
						g_string_append_c(str, esc_seq[i].equiv);
						in += strlen(esc_seq[i].sequence) - 1;
						break;
					}
				}
				if(i == NUM_ESC_SEQ)	/* No match found? */
					xml_error("Unknown &-sequence ignored");
			}
			else if(here == '\\')	/* Escape sequence? */
			{
				if((next = *(in + 1)) == '\0')	/* Trailing backslash OK and ignored. */
					break;
				switch(next)
				{
					case '\\':
						g_string_append_c(str, '\\');
						break;
					case '"':
						g_string_append_c(str, '"');
						break;
					default:
						xml_error("Unsupported backslash escaped character '%c'!", next);
				}
				in++;
			}
			else if(!(here == '"' && *(in + 1) == '\0'))	/* Accept internal quotes, reject final. */
				g_string_append_c(str, here);
		}
	}
	return str;
}

/* 1998-07-26 -	Parse out the data from the string <str>. Data can be either text, a bool,
**		an integer or a real number. Returns TRUE if data was successfully parsed,
**		FALSE if it was not. Uses built-in knowledge about how different types of
**		data look in the XML file to determine the type of data at <str>.
*/
static gint get_data(XmlNode *node, const gchar *str)
{
	guint	red, green, blue;

	node->data.type = XVT_NONE;

	if(sscanf(str, XML_UNSIGNED "%u", &node->data.uinteger.value) == 1)
		node->data.type = XVT_UINTEGER;
	else if(sscanf(str, XML_COLOR "%04X,%04X,%04X", &red, &green, &blue) == 3)
	{
		node->data.type = XVT_COLOR;
		node->data.color.value.red   = red;
		node->data.color.value.green = green;
		node->data.color.value.blue  = blue;
	}
	else if(get_real(str, &node->data.real.value) == 1)
		node->data.type = XVT_REAL;
	else if(sscanf(str, "%d", &node->data.integer.value) == 1)
		node->data.type = XVT_INTEGER;
	else if(is_word(str, XML_TRUE))
	{
		node->data.bool.value = TRUE;
		node->data.type = XVT_BOOL;
	}
	else if(is_word(str, XML_FALSE))
	{
		node->data.bool.value = FALSE;
		node->data.type = XVT_BOOL;
	}
	else
	{
		if((node->data.text.value = get_text_data(str)) != NULL)
			node->data.type = XVT_TEXT;
		else
			xml_error("No memory for text data in node '%s'", node->name);
	}
	return node->data.type != XVT_NONE;
}

/* 1998-07-26 -	Read nodes from file <in>, build a tree rooted in <root>. Tags are classified
**		as either internal nodes or leaf nodes by the casing of their first character;
**		internal nodes begin with upper-case characters, leafs with lower-case chars.
**		Note: this will only read *one* tree from the file; if there are more, all trees
**		but the first will be ignored. Also note that this function is recursive, so if
**		you're allergic to that kind of thing, stay away. Otherwise, enjoy!
*/
static XmlNode * read_tree(FILE *in, XmlNode *root)
{
	GString	*str, *data, *end;
	XmlNode	*node, *tree;
	gint	type, data_type, end_type;

	while((str = get_token(in, &type)) != NULL)
	{
		if(type == 1 && NODE_INTERNAL(str->str))
		{
			if(str->str[0] == '/')
			{
				if(root != NULL)
				{
					if(match_tags(root->name, str->str))
						break;
				}
				else
					xml_error("Bare ending tag '%s' found!", str->str);
			}
			else
			{
				if((node = xml_node_new(str->str)) != NULL)
				{
					if(root == NULL)
						tree = read_tree(in, root = node);
					else
					{
						tree = read_tree(in, node);
						xml_node_add_child(root, tree);
					}
				}
				else
					xml_error("Couldn't create node for '%s'", str->str);
			}
		}
		else if(type == 1 && NODE_COMMENT(str->str))
			continue;
		else if(root != NULL && (node = xml_node_new(str->str)))
		{
			if((data = get_token(in, &data_type)) != NULL)
			{
				if((end = get_token(in, &end_type)) != NULL)
				{
					if(match_tags(str->str, end->str))
					{
						if(get_data(node, data->str))
							xml_node_add_child(root, node);
						else
							xml_error("Couldn't get data for '%s' (unknown type)", node->name);
					}
					else
						xml_error("Overlapping tag found (%s,%s)", str->str, end->str);
				}
				else
					xml_error("Missing end tag for '%s'", node->name);
			}
			else
				xml_error("Couldn't get data for '%s' (no token)", node->name);
		}
		else
			xml_error("Bare data found");
		g_string_free(str, TRUE);
	}
	if(str != NULL)
		g_string_free(str, TRUE);
	return root;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-07-26 -	Destroy a tree, by freeing all the nodes. Don't keep references into a tree
**		you're about to destroy; copy any interesting data, then destroy the tree. Don't
**		let the tree hang around for longer than required either, since it'skind'a large.
** 1999-01-04 -	Corrected the way glib's list functions are used. Was broken.
*/
void xml_tree_destroy(XmlNode *node)
{
	const GSList	*iter;

	if(node != NULL)
	{
		for(iter = node->children; iter != NULL; iter = g_slist_next(iter))
			xml_tree_destroy(iter->data);
		xml_node_destroy(node);
	}
}

/* ----------------------------------------------------------------------------------------- */

#if 0		/* xml_tree_print() */
/* 1998-07-27 -	Prints an outline of a tree. Quite handy when debugging. */
void xml_tree_print(const XmlNode *root, gpointer user)
{
	gint	i, level = (gint) user;

	if(root != NULL)
	{
		for(i = 0; i < level; i++)
			putchar(' ');
		printf("%s", root->name);
		if(root->data.type != XVT_NONE)
		{
			switch(root->data.type)
			{
				case XVT_NONE:		/* Keep the compiler happy. */
					break;
				case XVT_TEXT:
					printf(" = '%s'", root->data.text.value->str);
					break;
				case XVT_INTEGER:
					printf(" = %d", root->data.integer.value);
					break;
				case XVT_UINTEGER:
					printf(" = %s%u", XML_UNSIGNED, root->data.uinteger.value);
					break;
				case XVT_REAL:
					printf(" = %f", root->data.real.value);
					break;
				case XVT_BOOL:
					printf(" = %s", root->data.bool.value ? XML_TRUE : XML_FALSE);
					break;
				case XVT_COLOR:
					printf(" = [%04X,%04X,%04X]", root->data.color.value.red, root->data.color.value.green, root->data.color.value.blue);
			}
		}
		putchar('\n');
		g_slist_foreach(root->children, (GFunc) xml_tree_print, GINT_TO_POINTER((level + 1)));
	}
}
#endif

/* ----------------------------------------------------------------------------------------- */

/* 1998-07-26 -	Find a node named <name>, and return a pointer to it. If no such node exists,
**		we return NULL. Recurses through entire tree.
*/
const XmlNode * xml_tree_search(const XmlNode *root, const gchar *name)
{
	const GSList	*iter;
	const XmlNode	*node;

	if(root != NULL)
	{
		if(strcmp(root->name, name) == 0)
			return root;
		for(iter = root->children; iter != NULL; iter = g_slist_next(iter))
		{
			if((node = xml_tree_search(iter->data, name)) != NULL)
				return node;
		}
	}
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-07-26 -	Load the first XML tree found in the file <name>. Case of the first
**		letter of tags determine wether the node created should be internal
**		or leaf.
*/
XmlNode * xml_tree_load(const gchar *name)
{
	XmlNode	*tree = NULL;
	FILE	*in;

	if((in = fopen(name, "rt")) != NULL)
	{
		tree = read_tree(in, NULL);
		fclose(in);
	}
	return tree;
}
