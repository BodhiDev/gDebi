/*
** 2010-07-09 -	A wrapper on top of glib's iconv wrapper (!) to make it easier to deal with reading
**		streams of text, in an initially unknown encoding. 
*/

#include "gentoo.h"

#include "convstream.h"

/* ----------------------------------------------------------------------------------------- */

struct ConvStream
{
	gchar		*buffer;	/* Base pointer, constant. */
	gsize		size;		/* Allocated size, from constructor. */
	gsize		used;		/* Currently occupied bytes, at beginning. */

	gchar		*out;
	gsize		outsize;

	gchar		*encoding;	/* Debugging hang-around. */
	gboolean	pass_through;	/* If the input encoding is UTF-8, do less work. */
	GIConv		iconv;
	
	ConvSink	sink;
	gpointer	user;

	gboolean	failed;
};

/* ----------------------------------------------------------------------------------------- */

const gchar * convstream_get_default_encoding(void)
{
	const gchar	*enc;

	g_get_charset(&enc);

	return enc;
}

const gchar * convstream_get_target_encoding(void)
{
	return "UTF-8";
}

/* ----------------------------------------------------------------------------------------- */

ConvStream * convstream_new(const gchar *encoding, gsize buffer, ConvSink sink, gpointer user)
{
	ConvStream	*cs;
	gsize		len;

	if(sink == NULL)
		return NULL;

	/* If nobody gave us an encoding, use the system's default. */
	if(encoding == NULL)
		encoding = convstream_get_default_encoding();

	len = strlen(encoding);

	cs = g_malloc(sizeof *cs + (2 * buffer) + (len + 1));

	cs->buffer = (gchar *) (cs + 1);
	cs->size = buffer;
	cs->used = 0;
	cs->out = (gchar *) (cs->buffer + cs->size);
	cs->outsize = cs->size;

	cs->encoding = (cs->out + cs->outsize);
	strcpy(cs->encoding, encoding);
	cs->pass_through = strcmp(cs->encoding, convstream_get_target_encoding()) == 0;

	if(!cs->pass_through)
	{
		cs->iconv = g_iconv_open(convstream_get_target_encoding(), cs->encoding);
		if(cs->iconv == (GIConv) -1)
		{
			fprintf(stderr, "**Failed to create GIConv for encoding '%s', can't convert text\n", cs->encoding);
			g_free(cs);
			return NULL;
		}
	}
	else
		cs->iconv = (GIConv) -1;

	cs->sink = sink;
	cs->user = user;
	cs->failed = FALSE;

	return cs;
}

void convstream_source(ConvStream *cs, const gchar *text, gsize length)
{
	if(cs == NULL || text == NULL || length == 0)
		return;

	while(!cs->failed && (length > 0 || cs->used != 0))
	{
		gsize	max = cs->size - cs->used;
		gsize	chunk = length > max ? max : length, count;
		gchar	*in, *out;

		/* Copy the chunk into the buffer, concatening it with any existing content. */
		memcpy(cs->buffer + cs->used, text, chunk);
		text += chunk;
		cs->used += chunk;
		length -= chunk;

		/* In pass-through mode (input is already UTF-8), validate to handle chunking. */
		if(cs->pass_through)
		{
			g_utf8_validate(cs->buffer, cs->used, (const gchar **) &in);
			count = in - cs->buffer;
			if(count == 0)
			{
				cs->sink(cs->encoding, 0, cs->user);
				cs->failed = TRUE;
				break;
			}
			cs->sink(cs->buffer, count, cs->user);
			cs->used -= count;
		}
		else
		{
			/* Try to convert the resulting bunch of bytes. */
			in = cs->buffer;
			out = cs->out;
			count = cs->outsize;
			g_iconv(cs->iconv, &in, &cs->used, &out, &count);
			/* If we got some output bytes, sink them. */
			if(count < cs->outsize)
				cs->sink(cs->out, cs->outsize - count, cs->user);
		}
		/* Copy any remaining input bytes back down to the start of the buffer. */
		if(cs->used != 0)
			memmove(cs->buffer, in, cs->used);
	}
}

void convstream_destroy(ConvStream *cs)
{
	if(cs == NULL)
		return;
	if(cs->iconv != (GIConv) - 1)
		g_iconv_close(cs->iconv);
	g_free(cs);
}
