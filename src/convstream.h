/*
** 2010-07-09 -	Convert a stream of text from a given encoding, into UTF-8. The idea is to make it easier to
**		implement support for various encodings in the text reader, by letting the user specify. To
**		auto-detect encodings is deemed Too Hard to be in gentoo's scope.
*/

/* Fashionably opaque. */
typedef struct ConvStream	ConvStream;

/* Magical semantics:
** If length == 0, conversion has failed and text will be the name of the expected input encoding.
*/
typedef void (*ConvSink)(const gchar *text, gsize length, gpointer user);

extern const gchar *	convstream_get_default_encoding(void);
extern const gchar *	convstream_get_target_encoding(void);

extern ConvStream *	convstream_new(const gchar *encoding, gsize buffer, ConvSink sink, gpointer user);
extern void		convstream_source(ConvStream *cs, const gchar *source, gsize length);
extern void		convstream_destroy(ConvStream *cs);
