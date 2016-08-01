/*
** 1999-09-11 -	A fun Rename command, with support for both simplistic replacement in filenames
**		as well as powerful regular expression replacement thingie. The former is handy
**		for changing the extension in all selected names from ".jpg" to ".jpeg", for
**		example. The latter RE mode can be used to do rather complex mappings and trans-
**		forms on the filenames. For example, consider having a set of 100 image files,
**		named "frame0.gif", "frame1.gif", ..., "frame99.gif". You want to rename these
**		to include a movie index (11), and also (of course) change the extension to ".png".
**		No problem. Just enter "^frame([0-9]+)\.gif$" in the 'From' text entry field
**		on the Reg Exp page, and "frame-11-$1.png" in the 'To' entry, and bam! The point
**		here is that you can use up to 9 parenthesized (?) subexpressions, and then access
**		the text that matched each with $n (n is a digit, 1 to 9) in the 'To' string.
**		If you've programmed any Perl, you might be familiar with the concept.
*/

#include "gentoo.h"

#include <ctype.h>

#include "cmd_delete.h"
#include "dialog.h"
#include "dirpane.h"
#include "errors.h"
#include "guiutil.h"
#include "overwrite.h"
#include "strutil.h"

#include "cmd_renamere.h"

#define	CMD_ID	"renamere"

/* ----------------------------------------------------------------------------------------- */

typedef enum {
	LOWER, UPPER, INITIAL, HEADLINE
} CaseMode;

typedef struct {
	Dialog		*dlg;

	GtkWidget	*nbook;

	/* Simple replace mode. */
	GtkWidget	*r_vbox;
	GtkWidget	*r_from;
	GtkWidget	*r_to;
	GtkWidget	*r_cnocase;
	GtkWidget	*r_cglobal;

	/* Full RE matching mode. */
	GtkWidget	*re_vbox;
	GtkWidget	*re_from;
	GtkWidget	*re_to;
	GtkWidget	*re_cnocase;

	/* Mapping mode (for character substitutions). */
	GtkWidget	*map_vbox;
	GtkWidget	*map_from;
	GtkWidget	*map_to;
	GtkWidget	*map_remove;

	/* Case-conversion mode. */
	GtkWidget	*case_vbox;
	GtkWidget	*case_lower;
	GtkWidget	*case_upper;
	GtkWidget	*case_initial;

	MainInfo	*min;
	DirPane		*src;
	gint		page;
	GSList		*selection;
} RenREInfo;

/* ----------------------------------------------------------------------------------------- */

static gboolean do_rename(MainInfo *min, DirPane *dp, const DirRow2 *row, const gchar *newname, GError **error)
{
	OvwRes		ores;
	GFile		*file;
	gboolean	ok, doit = TRUE;

	if(strcmp(dp_row_get_name_display(dp_get_tree_model(dp), row), newname) == 0)
	{
		dp_unselect(dp, row);
		return TRUE;
	}

	/* Get imaginary destination file. */
	if((file = dp_get_file_from_name(dp, newname)) == NULL)
		return FALSE;
	ores = ovw_overwrite_unary_file(dp, file);
	if(ores == OVW_SKIP)
		ok = !(doit = FALSE);	/* H4xX0r. */
	else if(ores == OVW_CANCEL)
		ok = doit = FALSE;
	else if(ores == OVW_PROCEED_FILE || ores == OVW_PROCEED_DIR)
		ok = del_delete_gfile(min, file, FALSE, error);
	else
		ok = TRUE;

	if(ok && doit)
	{
		GFile	*dfile;

		if((dfile = dp_get_file_from_row(dp, row)) != NULL)
		{
			GFile	*nfile;

			if((nfile = g_file_set_display_name(dfile, newname, NULL, error)) != NULL)
				g_object_unref(nfile);
			g_object_unref(dfile);
			ok = nfile != NULL;
		}
		else
			ok = FALSE;
	}
	if(!ok && error != NULL && *error != NULL)
		err_set_gerror(min, error, CMD_ID, file);
	g_object_unref(file);

	if(ok)
		dp_unselect(dp, row);

	return ok;
}

/* ----------------------------------------------------------------------------------------- */

static gboolean rename_simple(RenREInfo *ri, GError **err)
{
	const gchar	*fromd, *tod;
	GString		*nn;
	GSList		*iter;
	gboolean	nocase, global, ok = TRUE;

	if((fromd = gtk_entry_get_text(GTK_ENTRY(ri->r_from))) == NULL)
		return 0;
	tod = gtk_entry_get_text(GTK_ENTRY(ri->r_to));

	nocase = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ri->r_cnocase));
	global = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ri->r_cglobal));

	ovw_overwrite_begin(ri->min, _("\"%s\" Already Exists - Proceed With Rename?"), 0U);
	nn = g_string_new("");
	for(iter = ri->selection; ok && (iter != NULL); iter = g_slist_next(iter))
	{
		if(stu_replace_simple(nn, dp_row_get_name_display(dp_get_tree_model(ri->src), iter->data), fromd, tod, global, nocase) > 0)
			ok = do_rename(ri->min, ri->src, iter->data, nn->str, err);
	}
	g_string_free(nn, TRUE);
	ovw_overwrite_end(ri->min);

	return ok;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-09-11 -	Go through <def>, copying characters to <str>. If the sequence $n is found,
**		where n is a digit 1..9, replace that with the n:th piece of <src>, as described
**		by <match>. To get a single dollar in the output, write $$ in <def>.
**		Returns TRUE if the entire <def> was successfully interpolated, FALSE on error.
*/
static gboolean interpolate(GString *str, const gchar *def, const GMatchInfo *match)
{
	const gchar	*ptr;

	g_string_truncate(str, 0);
	for(ptr = def; *ptr; ptr = g_utf8_next_char(ptr))
	{
		gunichar	here;

		here = g_utf8_get_char(ptr);
		if(here == '$')
		{
			ptr = g_utf8_next_char(ptr);
			here = g_utf8_get_char(ptr);
			if(g_unichar_isdigit(here))
			{
				gint	slot;

				slot = g_unichar_digit_value(here);
				if(slot >= 0 && slot <= 9)
				{
					gchar	*ms = g_match_info_fetch(match, slot);

					if(ms != NULL)
					{
						g_string_append(str, ms);
						g_free(ms);
					}
					else
						return FALSE;
				}
				else
					return FALSE;
			}
			else if(*ptr == '$')
				g_string_append_c(str, '$'), ptr++;
			else
				return FALSE;
		}
		else
			g_string_append_unichar(str, here);
	}
	return TRUE;
}

static gboolean rename_regexp(RenREInfo *ri, GError **err)
{
	const gchar	*fromdefd, *tod;
	guint		reflags;
	GRegex		*fromre = NULL;
	gboolean	ok = TRUE;

	if((fromdefd = gtk_entry_get_text(GTK_ENTRY(ri->re_from))) == NULL)
		return FALSE;
	if((tod = gtk_entry_get_text(GTK_ENTRY(ri->re_to))) == NULL)
		return FALSE;

	reflags = G_REGEX_EXTENDED | (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ri->re_cnocase)) ? G_REGEX_CASELESS : 0);
	if((fromre = g_regex_new(fromdefd, reflags, G_REGEX_MATCH_NOTEMPTY, err)) != NULL)
	{
		GSList		*iter;
		GString		*nn;

		ovw_overwrite_begin(ri->min, _("\"%s\" Already Exists - Proceed With Rename?"), 0U);
		nn = g_string_new("");
		for(iter = ri->selection; ok && (iter != NULL); iter = g_slist_next(iter))
		{
			GMatchInfo	*mi = NULL;

			if(g_regex_match(fromre, dp_row_get_name_display(dp_get_tree_model(ri->src), iter->data), G_REGEX_MATCH_NOTEMPTY, &mi))
			{
				if(interpolate(nn, tod, mi) && g_utf8_strchr(nn->str, G_DIR_SEPARATOR, -1) == NULL)
					ok = do_rename(ri->min, ri->src, iter->data, nn->str, err);
				g_match_info_free(mi);
			}
		}
		g_string_free(nn, TRUE);
		ovw_overwrite_end(ri->min);
	}
	return ok;
}

/* ----------------------------------------------------------------------------------------- */

static gboolean rename_map(RenREInfo *ri, GError **err)
{
	const gchar	*fromdef, *todef, *remdef;
	GSList		*iter;
	GString		*nn, *nr;
	gsize		fl, rl, i, nl;
	gboolean	ok = TRUE;

	if((fromdef = gtk_entry_get_text(GTK_ENTRY(ri->map_from))) == NULL)
		return FALSE;
	if((todef = gtk_entry_get_text(GTK_ENTRY(ri->map_to))) == NULL)
		return FALSE;
	if(g_utf8_strchr(todef, -1, G_DIR_SEPARATOR) != NULL)	/* As always, prevent rogue moves. */
		return FALSE;
	if((remdef = gtk_entry_get_text(GTK_ENTRY(ri->map_remove))) == NULL)
		return FALSE;

	/* Totally important: some lengths are in chars (for iteration), some in bytes for g_utf8_strchr(). */
	fl = strlen(fromdef);
	rl = strlen(remdef);

	ovw_overwrite_begin(ri->min, _("\"%s\" Already Exists - Proceed With Rename?"), 0U);
	nn = g_string_new("");
	nr = g_string_new("");
	for(iter = ri->selection; iter != NULL; iter = g_slist_next(iter))
	{
		const gchar	*ptr, *map;

		/* Step 1: clear the dynamic string. */
		g_string_truncate(nn, 0);
		ptr = dp_row_get_name_display(dp_get_tree_model(ri->src), iter->data);
		nl = g_utf8_strlen(ptr, -1);
		/* Step 2: go through and do the mapping. */
		for(i = 0; i < nl; i++, ptr = g_utf8_next_char(ptr))
		{
			gunichar	ch = g_utf8_get_char(ptr);

			if((map = g_utf8_strchr(fromdef, fl, ch)) != NULL)
			{
				glong		index = g_utf8_pointer_to_offset(fromdef, map);
				const gchar	*tptr = g_utf8_offset_to_pointer(todef, index);

				ch = g_utf8_get_char(tptr);	/* Map. */
			}
			g_string_append_unichar(nn, ch);
		}
		/* Step 3: remove any characters found in the 'remove' string. */
		ptr = nn->str;
		nl = g_utf8_strlen(ptr, -1);
		g_string_truncate(nr, 0);
		for(i = 0; i < nl; i++, ptr = g_utf8_next_char(ptr))
		{
			gunichar	ch = g_utf8_get_char(ptr);

			if(g_utf8_strchr(remdef, rl, ch) == NULL)
				g_string_append_unichar(nr, ch);
		}
		/* Step 4: do the rename, using the now-built "display name". */
		ok = do_rename(ri->min, ri->src, iter->data, nr->str, err);
	}
	g_string_free(nr, TRUE);
	g_string_free(nn, TRUE);
	ovw_overwrite_end(ri->min);

	return ok;
}

/* ----------------------------------------------------------------------------------------- */

/* 2008-07-19 -	Converts the given string to have an upper-case initial character, while the
 *		remainder is lower-case. This is a very western idea of a useful case
 *		conversion, but ... I'll include it anyway.
*/
static gchar * utf8_str_initial(const gchar *str, gssize len)
{
	GString		*tmp;
	gchar		*buf;
	gunichar	here;

	tmp = g_string_sized_new(len);

	here = g_utf8_get_char(str);
	g_string_append_unichar(tmp, g_unichar_toupper(here));

	while(--len)
	{
		str = g_utf8_next_char(str);
		here = g_utf8_get_char(str);
		g_string_append_unichar(tmp, g_unichar_tolower(here));
	}
	buf = tmp->str;
	g_string_free(tmp, FALSE);

	return buf;
}

/* 2008-04-20 -	Minor touch-ups due to GTK+ 2.0 and UTF-8 in strings. We need to make a decision:
 *		Is the map operation happening in file-system or display space? The obvious
 *		choice seems to be file-system, which is after all what the filenames are for.
 *		However, we don't have a general way of downcasing if the local file system is
 *		using UTF-8 ... So we use the display versions of the names, downcase in UTF-8,
 *		and then convert back.
*/
static gboolean rename_case(RenREInfo *ri, GError **err)
{
	GString		*nn;
	GSList		*iter;
	gchar *		(*func)(const gchar *, gssize), *cc;
	gboolean	ok = TRUE;

	/* Select the case-conversion function, once. */
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ri->case_lower)))
		func = g_utf8_strdown;
	else if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ri->case_upper)))
		func = g_utf8_strup;
	else if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ri->case_initial)))
		func = utf8_str_initial;
	else
		return FALSE;

	ovw_overwrite_begin(ri->min, _("\"%s\" Already Exists - Proceed With Rename?"), 0U);
	nn = g_string_new("");
	for(iter = ri->selection; ok && (iter != NULL); iter = g_slist_next(iter))
	{
		const gchar	*dn = dp_row_get_name_display(dp_get_tree_model(ri->src), iter->data);

		/* Step 1: set the dynamic string to the input filename. */
		g_string_assign(nn, dn);
		/* Step 2: perform case conversion. */
		cc = func(nn->str, nn->len);
		/* Step 3: if resulting name is different, rename on disk. */
		if(strcmp(dn, cc) != 0)
		{
			ok = do_rename(ri->min, ri->src, iter->data, cc, err);
		}
		g_free(cc);
	}
	g_string_free(nn, TRUE);
	ovw_overwrite_end(ri->min);

	return ok;
}

/* ----------------------------------------------------------------------------------------- */

static void evt_nbook_switchpage(GtkWidget *wid, gpointer page, gint page_num, gpointer user)
{
	RenREInfo	*ri = user;

	ri->page = page_num;
	gtk_widget_grab_focus(ri->page ? ri->re_from : ri->r_from);
}

gint cmd_renamere(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	GtkWidget		*label, *grid;
	static RenREInfo	ri = { NULL };
	gboolean		ok = TRUE;

	ri.min	= min;
	ri.src	= src;

	if((ri.selection = dp_get_selection(ri.src)) == NULL)
		return 1;

	if(ri.dlg == NULL)
	{
		ri.nbook = gtk_notebook_new();
		g_signal_connect(G_OBJECT(ri.nbook), "switch-page", G_CALLBACK(evt_nbook_switchpage), &ri);

		/* Build the 'Simple' mode's GUI. */
		ri.r_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		label = gtk_label_new(_("Look for substring in all filenames, and replace\nit with another string."));
		gtk_box_pack_start(GTK_BOX(ri.r_vbox), label, FALSE, FALSE, 0);
		grid = gtk_grid_new();
		label = gtk_label_new(_("Replace"));
		gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
		ri.r_from = gui_dialog_entry_new();
		gtk_widget_set_hexpand(ri.r_from, TRUE);
		gtk_widget_set_halign(ri.r_from, GTK_ALIGN_FILL);
		gtk_grid_attach(GTK_GRID(grid), ri.r_from, 1, 0, 1, 1);
		label = gtk_label_new(_("With"));
		gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
		ri.r_to = gui_dialog_entry_new();
		gtk_grid_attach(GTK_GRID(grid), ri.r_to, 1, 1, 1, 1);
		gtk_box_pack_start(GTK_BOX(ri.r_vbox), grid, FALSE, FALSE, 0);
		grid = gtk_grid_new();
		ri.r_cnocase = gtk_check_button_new_with_label(_("Ignore Case?"));
		gtk_widget_set_hexpand(ri.r_cnocase, TRUE);
		gtk_widget_set_halign(ri.r_cnocase, GTK_ALIGN_FILL);
		gtk_grid_attach(GTK_GRID(grid), ri.r_cnocase, 0, 0, 1, 1);
		ri.r_cglobal = gtk_check_button_new_with_label(_("Replace All?"));
		gtk_grid_attach(GTK_GRID(grid), ri.r_cglobal, 1, 0, 1, 1);
		gtk_box_pack_start(GTK_BOX(ri.r_vbox), grid, FALSE, FALSE, 0);

		gtk_notebook_append_page(GTK_NOTEBOOK(ri.nbook), ri.r_vbox, gtk_label_new(_("Simple")));

		/* Build the 'Reg Exp' mode's GUI. */
		ri.re_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		label = gtk_label_new(	_("Execute the 'From' RE on each filename, storing\n"
					"parenthesised subexpression matches. Then replace\n"
					"any occurance of $n in 'To', where n is the index\n"
					"(counting from 1) of a subexpression, with the text\n"
					"that matched, and use the result as a new filename."));
		gtk_box_pack_start(GTK_BOX(ri.re_vbox), label, FALSE, FALSE, 0);
		grid = gtk_grid_new();
		label = gtk_label_new(_("From"));
		gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
		ri.re_from = gui_dialog_entry_new();
		gtk_widget_set_hexpand(ri.re_from, TRUE);
		gtk_widget_set_halign(ri.re_from, GTK_ALIGN_FILL);
		gtk_grid_attach(GTK_GRID(grid), ri.re_from, 1, 0, 1, 1);
		label = gtk_label_new(_("To"));
		gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
		ri.re_to = gui_dialog_entry_new();
		gtk_grid_attach(GTK_GRID(grid), ri.re_to, 1, 1, 1, 1);
		gtk_box_pack_start(GTK_BOX(ri.re_vbox), grid, FALSE, FALSE, 0);
		ri.re_cnocase = gtk_check_button_new_with_label(_("Ignore Case?"));
		gtk_box_pack_start(GTK_BOX(ri.re_vbox), ri.re_cnocase, FALSE, FALSE, 0);

		gtk_notebook_append_page(GTK_NOTEBOOK(ri.nbook), ri.re_vbox, gtk_label_new(_("Reg Exp")));
		gtk_widget_show_all(ri.re_vbox);

		/* Build the 'Map' mode's GUI. */
		ri.map_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		label = gtk_label_new(_("Look for each character in the 'From' string, and replace\n"
					"any hits with the corresponding character in the 'To' string.\n"
					"Then, any characters in the 'Remove' string are removed from\n"
					"the filename, and the result used as the new name for each file."));
		gtk_box_pack_start(GTK_BOX(ri.map_vbox), label, FALSE, FALSE, 0);
		grid = gtk_grid_new();
		label = gtk_label_new(_("From"));
		gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
		ri.map_from = gui_dialog_entry_new();
		gtk_widget_set_hexpand(ri.map_from, TRUE);
		gtk_widget_set_halign(ri.map_from, GTK_ALIGN_FILL);
		gtk_grid_attach(GTK_GRID(grid), ri.map_from, 1, 0, 1, 1);
		label = gtk_label_new(_("To"));
		gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
		ri.map_to = gui_dialog_entry_new();
		gtk_grid_attach(GTK_GRID(grid), ri.map_to, 1, 1, 1, 1);
		label = gtk_label_new(_("Remove"));
		gtk_grid_attach(GTK_GRID(grid), label, 0, 2, 1, 1);
		ri.map_remove = gui_dialog_entry_new();
		gtk_grid_attach(GTK_GRID(grid), ri.map_remove, 1, 2, 1, 1);
		gtk_box_pack_start(GTK_BOX(ri.map_vbox), grid, FALSE, FALSE, 0);
		gtk_notebook_append_page(GTK_NOTEBOOK(ri.nbook), ri.map_vbox, gtk_label_new(_("Map")));

		/* Build the 'Case' mode's GUI. */
		ri.case_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		label = gtk_label_new(_("Changes the case (upper/lower) of the characters\nin the selected filename(s)."));
		gtk_box_pack_start(GTK_BOX(ri.case_vbox), label, FALSE, FALSE, 0);
		ri.case_lower = gtk_radio_button_new_with_label(NULL, _("Lower Case?"));
		gtk_box_pack_start(GTK_BOX(ri.case_vbox), ri.case_lower, FALSE, FALSE, 0);
		ri.case_upper = gtk_radio_button_new_with_label(gtk_radio_button_get_group(GTK_RADIO_BUTTON(ri.case_lower)), _("Upper Case?"));
		gtk_box_pack_start(GTK_BOX(ri.case_vbox), ri.case_upper, FALSE, FALSE, 0);
		ri.case_initial = gtk_radio_button_new_with_label(gtk_radio_button_get_group(GTK_RADIO_BUTTON(ri.case_lower)), _("Upper Case Initial?"));
		gtk_box_pack_start(GTK_BOX(ri.case_vbox), ri.case_initial, FALSE, FALSE, 0);
		gtk_widget_show_all(ri.map_vbox);
		gtk_notebook_append_page(GTK_NOTEBOOK(ri.nbook), ri.case_vbox, gtk_label_new(_("Case")));

		ri.dlg = dlg_dialog_sync_new(ri.nbook, _("RenameRE"), NULL);

		ri.page = 0;
	}
	gtk_notebook_set_current_page(GTK_NOTEBOOK(ri.nbook), ri.page);
	gtk_widget_grab_focus(ri.page ? ri.re_from : ri.r_from);

	if(dlg_dialog_sync_wait(ri.dlg) == DLG_POSITIVE)
	{
		GError		*err = NULL;

		if(ri.page == 0)
			ok = rename_simple(&ri, &err);
		else if(ri.page == 1)
			ok = rename_regexp(&ri, &err);
		else if(ri.page == 2)
			ok = rename_map(&ri, &err);
		else
			ok = rename_case(&ri, &err);
		if(ok)
			dp_rescan_post_cmd(ri.src);
	}
	dp_free_selection(ri.selection);
	ri.selection = NULL;

	return ok;
}
