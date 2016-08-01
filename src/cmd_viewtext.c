/*
** 1999-02-03 -	Moved this code from the textview module, cleaning it up.
** 1999-03-06 -	Changed for new selection management. Major changes, but it became
**		a *lot* cleaner. At least internally.
** 1999-04-06 -	Added use of the new command configuration system. Note that the info is
**		actually shared across the three individual commands defined in this module.
**		Also rewrote the hex-or-text decision code, and added error reporting to the
**		ViewTextOrHex command.
*/

#include "gentoo.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cmdseq_config.h"
#include "convstream.h"
#include "dirpane.h"
#include "errors.h"
#include "fileutil.h"
#include "textview.h"
#include "cmd_viewtext.h"

/* ----------------------------------------------------------------------------------------- */

typedef struct {			/* Options for the "ViewText" command. */
	gboolean	modified;
	gsize		buf_size;			/* Buffer size. */
	gsize		check_size;			/* The ViewTextOrHex command inspects this many bytes. */
	gboolean	exit_on_left;			/* Exit text reader on Left Arrow keypress? */
} OptViewText;

static OptViewText	viewtext_options;
static CmdCfg		*viewtext_cmc = NULL;

/* ----------------------------------------------------------------------------------------- */

/* 1998-05-19 -	Just open a window containing some text viewing widgetry.
** 1998-09-10 -	The creation of txi_open() above allowed this one to shrink from 50 to 5 lines, sort of.
*/
static gint view_file_text(MainInfo *min, GFile *file, const gchar *encoding, GError **err)
{
	GtkWidget	*txv;

	if((txv = txv_open(min, NULL)) != NULL)
	{
		txv_show(txv);
		txv_text_load(txv, file, viewtext_options.buf_size, encoding, err);
		txv_enable(txv);
	}
	return txv != NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-12-18 -	View file in hex mode. */
static gint view_file_hex(MainInfo *min, GFile *file, const gchar *encoding, GError **err)
{
	GtkWidget	*txv;

	if((txv = txv_open(min, NULL)) != NULL)
	{
		txv_show(txv);
		txv_text_load_hex(txv, file, err);
		txv_enable(txv);
	}
	return txv != NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-02-03 -	Inspect the first bytes (check_size) for printability. If all are OK, return 1.
**		Otherwise return 0. If an error occurs, -1 is returned.
** 2010-02-28 -	Ported to GIO, some comments that no longer apply were removed.
** 2010-07-20 -	Now tries to charset-convert the input, if that fails it's not text. Nice.
*/
static gint file_is_text(MainInfo *min, GFile *file, const gchar *encoding, GError **err)
{
	GFileInputStream	*is;
	gboolean		txt = FALSE;

	if(encoding == NULL)
		encoding = convstream_get_default_encoding();

	if((is = g_file_read(file, NULL, err)) != NULL)
	{
		gchar	*buf;
		gssize	got;

		buf = g_malloc(viewtext_options.check_size);
		if((got = g_input_stream_read(G_INPUT_STREAM(is), buf, viewtext_options.check_size, NULL, err)) > 0)
		{
			gchar	*out = g_convert(buf, got, convstream_get_target_encoding(), encoding, NULL, NULL, NULL);

			txt = (out != NULL);
			g_free(out);
		}
		g_free(buf);
		g_object_unref(is);
	}
	return *err != NULL ? -1 : txt;
}

/* 1999-02-03 -	This function views a file named <full_name> as either ASCII text or hex data.
**		Note that it does this by actually inspecting the first few bytes of the file,
**		rather than using gentoo's type system. This makes it work for all files, even
**		those of type "Unknown".
** 2010-02-28 -	Ported to GIO.
*/
static gint view_file_text_or_hex(MainInfo *min, GFile *file, const gchar *encoding, GError **err)
{
	gint	res;

	if((res = file_is_text(min, file, encoding, err)) >= 0)
	{
		if(res == 0)
			view_file_hex(min, file, NULL, err);
		else
			view_file_text(min, file, encoding, err);
	}
	return res >= 0;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-06 -	A general viewer-trampoline. Will view all currently selected files using
**		the given viewing function.
** 2010-02-28 -	Ported to GIO and GTK+ 2.0.
*/
static gint view_selection(MainInfo *min, DirPane *dp, gint (*viewer)(MainInfo *min, GFile *file, const gchar *encoding, GError **err), const gchar *encoding)
{
	gboolean	ok = TRUE;
	GSList		*slist, *iter;
	GError		*err = NULL;

	if((slist = dp_get_selection(dp)) == NULL)
		return 1;
	for(iter = slist; ok && iter != NULL; iter = g_slist_next(iter))
	{
		GFile	*file;

		if((file = dp_get_file_from_row(dp, iter->data)) != NULL)
		{
			ok = viewer(min, file, encoding, &err);
			if(ok)
				dp_unselect(dp, iter->data);
			else
				err_set_gerror(min, &err, "ViewText", file);
			g_object_unref(file);
		}
	}
	dp_free_selection(slist);

	return ok;
}

/* 1999-03-06 -	View selected files in plain text mode.
** 1999-05-29 -	Now uses the command argument system to control plain/hex/auto modes, thus
**		replacing the old ViewTextHex and ViewTextOrHex commands. Neater.
*/
gint cmd_viewtext(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	const gchar	*encoding;
	gint		mode;

	encoding = car_keyword_get_value(ca, "encoding", "");
	/* Map an empty encoding to NULL, which gives us the system's default. */
	if(encoding != NULL && *encoding == '\0')
		encoding = NULL;
	mode = car_keyword_get_enum(ca, "mode", 1, "auto", "text", "hex", NULL);
	switch(mode)
	{
		case 0:
			return view_selection(min, src, view_file_text_or_hex, encoding);
		case 1:
			return view_selection(min, src, view_file_text, encoding);
		case 2:
			return view_selection(min, src, view_file_hex, NULL);
		default:
			;
	}

	return 0;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-06 -	Register the ViewText & friends configuration info. */
void cfg_viewtext(MainInfo *min)
{
	if(viewtext_cmc == NULL)
	{
		/* Initialize options to legacy default values. */
		viewtext_options.modified    = FALSE;
		viewtext_options.buf_size    = (1 << 18);
		viewtext_options.check_size  = (1 << 9);
		viewtext_options.exit_on_left = TRUE;

		viewtext_cmc = cmc_config_new("ViewText", &viewtext_options);
		cmc_field_add_boolean(viewtext_cmc, "modified", NULL, offsetof(OptViewText, modified));
		cmc_field_add_size(viewtext_cmc, "buf_size", _("Buffer Size"), offsetof(OptViewText, buf_size), 1024, (1 << 20), 1024);
		cmc_field_add_size(viewtext_cmc, "check_size", _("Hex-Check First"), offsetof(OptViewText, check_size), 32, (1 << 16), 32);
		cmc_field_add_boolean(viewtext_cmc, "exit_left", _("Exit on Left Arrow Key?"), offsetof(OptViewText, exit_on_left));
		cmc_config_register(viewtext_cmc);
	}
}
