/*
** 1998-09-25 -	This module contains code formerly in the 'commands.c' module. It deals
**		with the parsing and transformation of command arguments in string format
**		(e.g. "ls -l {fp}") to argv-style argument vectors. Very useful in command
**		execution code. All code was modified for the new command format, of course.
** 1999-02-25 -	Finished (?) some major rewrites. This should be a lot more flexible now, and
**		perhaps even a couple of small measures faster. Now does half-decent quoting.
** 1999-03-07 -	Changes for the new selection representation.
** 1999-05-08 -	Simplified main cpr_parse() interface slightly; now just takes a const gchar
**		string rather than a GString.
** 1999-06-19 -	Adapted for new dialog module.
*/

#include "gentoo.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "dialog.h"
#include "dirpane.h"
#include "strutil.h"
#include "userinfo.h"
#include "cmdparse.h"

/* ----------------------------------------------------------------------------------------- */

#define	INP_PATTERN     "\1" "INP%02d" "\2"	/* Shouldn't occur in regular text. Really. */
#define	INP_PAT_LEN	(7)			/* Guaranteed length produced by above. */

#define	DLG_MAX_INPUT	(16)			/* Maximum amount of individual inputs {I}s for a single command. */

typedef enum { CITP_STRING, CITP_MENU, CITP_COMBO, CITP_LABEL, CITP_CHECKBOX } CIType;

typedef struct {
	GtkWidget	*entry;
} CIString;

typedef struct {
	GList		*items;			/* List of item texts (dynamic). */
	gint		current;		/* Index of currently selected item. */
} CIMenu;

typedef struct {
	GtkWidget	*combo;
} CICombo;

typedef struct {
	GtkWidget	*label;			/* This is either a GtkLabel or a HSeparator (from {It:-}). */
} CILabel;

typedef struct {
	GtkWidget	*check;			/* Just a GtkCheckButton. */
	const gchar	*active, *inactive;	/* Choice texts. If NULL, the defauls of TRUE and FALSE are used. */
} CICheckbox;

typedef struct {
	CIType		type;
	union {
	CIString	string;
	CIMenu		menu;
	CICombo		combo;
	CILabel		label;
	CICheckbox	checkbox;
	} inp;
} CmdInp;

typedef struct {
	MainInfo	*min;			/* So unbelievably handy. */
	Dialog		*dlg;			/* The dialog window. */
	CmdInp		input[DLG_MAX_INPUT];
	GtkWidget	*body;			/* This holds the entire GUI. */
	GtkWidget	*grid;			/* Input widgets and labels are added here. */
	gint		row;			/* Current row in table. */
	gchar		*wintitle;		/* Title to set on window (example: {Iw:"gentoo"}). */
	gint		str_count;		/* Counts number of string fields (for focus). */
	gboolean	str_focused;		/* Gets set as the string is first focused. */
} CmdDlg;

static const gchar	the_true[] = "true", the_false[] = "false";

/* ----------------------------------------------------------------------------------------- */

static void	parse_code(MainInfo *min, GString *cstr, gchar *code, CmdDlg *dlg, gboolean input_ok);

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-25 -	Parse a code beginning with 'f' or 'F'.
** 1998-10-26 -	Added the 'd' flag to get at the destination pane, rather than the source.
** 1999-02-24 -	Rewritten for new argument handling style. Quotes filenames now.
*/
static void parse_code_file(MainInfo *min, GString *cstr, const gchar *code)
{
	DirPane		*dp = min->gui->cur_pane;
	const gchar	*name;
	const gboolean	unsel = (strchr(code + 1, 'u') != NULL),
			path  = (strchr(code + 1, 'p') != NULL),
			dest  = (strchr(code + 1, 'd') != NULL),
			quote = (strchr(code + 1, 'Q') == NULL),	/* Upper case for default-on. */
			ext   = (strchr(code + 1, 'E') == NULL);
	GSList		*slist, *iter;

	if(dest)		/* User requested the destination pane? */
		dp = dp_mirror(min, dp);
	if((slist = dp_get_selection(dp)) == NULL)
		return;

	for(iter = slist; iter != NULL; iter = g_slist_next(iter))
	{
		name = quote ? dp_name_quoted(dp, iter->data, path) : dp_row_get_name(dp_get_tree_model(dp), iter->data);

		if(iter != slist)
			g_string_append_c(cstr, ' ');
		if(ext)
			g_string_append(cstr, name);
		else
		{
			const gchar	*dot = strchr(name, '.');
			if(dot != NULL && dot > name)
			{
				/* Only append up to, but not including, the dot. */
				g_string_append_len(cstr, name, dot - name);
			}
			else
				g_string_append(cstr, name);
		}
		if(unsel)
			dp_unselect(dp, iter->data);
		if(code[0] == 'f')		/* Only first? */
			break;
	}
	dp_free_selection(slist);
}

/* 1998-09-25 -	Parse a code beginning with 'P', resulting in a path.
** 1999-02-24 -	Touched up to comply with the new argument handling style.
*/
static void parse_code_path(MainInfo *min, GString *cstr, const gchar *code)
{
	const gchar	*path = NULL;
	DirPane		*src = min->gui->cur_pane, *dst = dp_mirror(min, src);

	if(code[1] == '\0')			/* Nothing but the 'P'? */
		path = src->dir.path;
	else
	{
		if(code[1] == 's')
			path = src->dir.path;
		else if(code[1] == 'd')
			path = dst->dir.path;
		else if(code[1] == 'l')
			path = min->gui->pane[0].dir.path;
		else if(code[1] == 'r')
			path = min->gui->pane[1].dir.path;
		else if(code[1] == 'h')
			path = getenv("HOME");
		else
			fprintf(stderr, "**CMDPARSE: Illegal path code '%s'\n", code + 1);
	}
	if(path != NULL)
	{
		if(strncmp(path, "file://", 7) == 0)
			path += 7;
		g_string_append(cstr, path);
	}
}

/* 2010-03-14 -	Parse a {u} code to look up an URI. */
static void parse_code_uri(MainInfo *min, GString *cstr, const gchar *code)
{
	DirPane		*dp = min->gui->cur_pane;
	const gboolean	single = code[0] == 'u',
			unsel = (strchr(code + 1, 'u') != NULL),
			dest  = (strchr(code + 1, 'd') != NULL),
			quote = (strchr(code + 1, 'Q') == NULL);	/* Upper case for default-on. */
	GSList		*slist, *iter;

	if(dest)		/* User requested the destination pane? */
		dp = dp_mirror(min, dp);
	if((slist = dp_get_selection(dp)) == NULL)
		return;

	for(iter = slist; iter != NULL; iter = g_slist_next(iter))
	{
		GFile	*child;

		child = dp_get_file_from_row(dp, iter->data);
		if(child != NULL)
		{
			gchar	*uri;

			if((uri = g_file_get_uri(child)) != NULL)
			{
				if(iter != slist)
					g_string_append_c(cstr, ' ');
				if(quote)
					g_string_append_c(cstr, '"');
				g_string_append(cstr, uri);
				if(quote)
					g_string_append_c(cstr, '"');
				if(unsel)
					dp_unselect(dp, iter->data);
				if(code[0] == 'f')		/* Only first? */
					break;
				g_free(uri);
			}
			if(single)
				break;
		}
	}
	dp_free_selection(slist);
}

/* 1998-09-25 -	Parse an environment variable lookup-code, {$...}.
** 1999-02-24 -	Touched up for the new argument handling style.
*/
static void parse_code_env(MainInfo *min, GString *cstr, const gchar *code)
{
	const gchar	*val;

	if((val = g_getenv(code + 1)) != NULL)
		g_string_append(cstr, val);
}

/* 1998-09-25 -	Parse a PID-lookup code. Currently that's just {#}, and gives the main
**		program's PID.
** 1999-02-24 -	Upgraded for new style. Not very much to change. :)
*/
static void parse_code_pid(MainInfo *min, GString *cstr, const gchar *code)
{
	g_string_sprintfa(cstr, "%d", (gint) getpid());
}

/* 1998-12-23 -	Parse a {~...} code, used to look after user's home directories.
** 1999-02-24 -	Upgraded argument handling.
*/
static void parse_code_home(MainInfo *min, GString *cstr, const gchar *code)
{
	const gchar	*uhome;

	if((uhome = usr_lookup_uhome(code + 1)) != NULL)
		g_string_append(cstr, uhome);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-25 -	Initialize the command input dialog, by simply constructing the relevant
**		GTK+ widgets and connecting them together. GIMP-ified in this reimplementation,
**		to keep it simpler.
*/
static void init_input_dialog(CmdDlg *dlg)
{
	dlg->body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	dlg->grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(dlg->grid), 5);
	gtk_box_pack_start(GTK_BOX(dlg->body), dlg->grid, TRUE, TRUE, 0);
}

/* 1999-02-27 -	A little helper routine to pack widgets into the table forming the command input
**		dialog. <pos> can be either 0 (left column), 1 (right column) or 2 (both columns).
**		The <padding> is vertical, and best left at 0 for non-separator widgets.
*/
static void pack_input_widget(CmdDlg *dlg, GtkWidget *wid, gint pos, gint padding)
{
	if(dlg != NULL && wid != NULL)
	{
		gint	x = 0, width = 2;	/* Assume <pos> will be 2. */

		if(pos == 0)
			--width;
		else if(pos == 1)
			++x;
		gtk_widget_set_hexpand(wid, TRUE);
		gtk_widget_set_halign(wid, GTK_ALIGN_FILL);
		if(padding != 0)
		{
			gtk_widget_set_margin_top(wid, padding);
			gtk_widget_set_margin_bottom(wid, padding);
		}
		gtk_grid_attach(GTK_GRID(dlg->grid), wid, x, dlg->row, width, 1);
	}
}

/* 1999-02-23 -	Since we can't focus the widget until it actually is visible, and the root
**		gtk_widget_show() call is done inside the dialog module, this gross event
**		hack is used instead. Note how a special flag is used to avoid doing this
**		more than once during the lifetime of the dialog.
*/
static gint evt_input_string_realize(GtkWidget *wid, gpointer user)
{
	gboolean	*flag = user;

	if(!*flag)
	{
		gtk_widget_grab_focus(wid);
		*flag = TRUE;
	}
	return TRUE;
}

/* 1998-09-25 -	Build a string widget for an input dialog. Cut-down version.
** 1999-02-27 -	Modified for the new packing scheme.
** 2003-11-16 -	Now supports brace codes in the default specifier.
*/
static gint build_input_string(CmdDlg *dlg, const gchar *def, CmdInp *inp)
{
	GtkWidget	*ent;
	gchar		*end;

	inp->type = CITP_STRING;
	ent = gtk_entry_new();
	if(*def == '*')	/* Password mode? */
	{
		gtk_entry_set_visibility(GTK_ENTRY(ent), FALSE);
		def++;
	}
	if(*def == '=' && def[1] == '"')
	{
		if((end = strchr(def + 2, '"')) != NULL)
		{
			GString	*tmp, *dstr;

			*end = '\0';
			tmp = g_string_new(def + 2);
			dstr = g_string_new("");
			parse_code(dlg->min, dstr, tmp->str, dlg, FALSE);
			g_string_free(tmp, TRUE);
			gtk_entry_set_text(GTK_ENTRY(ent), dstr->str);
			gtk_editable_select_region(GTK_EDITABLE(ent), 0, -1);
			g_string_free(dstr, TRUE);
		}
	}
	pack_input_widget(dlg, inp->inp.string.entry = ent, 1, 0);

	return 1;
}

/* 1998-10-07 -	User selected an option in an option menu widget (created by {Im...}).
**		Store the index in the menu.
*/
static gint evt_menu_activated(GtkWidget *wid, gpointer user)
{
	CmdInp	*inp = user;

	inp->inp.menu.current = gtk_combo_box_get_active(GTK_COMBO_BOX(wid));

	return TRUE;
}

/* 1998-10-07 -	Build an input menu widget, as defined by the {Im...} code.
** 1999-02-27 -	Modified for new packing scheme.
*/
static gint build_input_menu(CmdDlg *dlg, const gchar *def, CmdInp *inp)
{
	GtkWidget	*combo;
	const gchar	*itext, *colon;

	if(*def != '=')
		return 0;
	combo = gtk_combo_box_text_new();
	inp->type = CITP_MENU;
	inp->inp.menu.items = NULL;
	inp->inp.menu.current = 0;
	while((def = stu_scan_string(def, &itext)) != NULL)
	{
		if((colon = strchr(itext, ':')) != NULL)
		{
			gchar	tmp[2048];

			g_strlcpy(tmp, itext, sizeof tmp);
			tmp[colon - itext] = '\0';
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), tmp);
		}
		else
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), itext);

		inp->inp.menu.items = g_list_append(inp->inp.menu.items, (gpointer) itext);
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
	g_signal_connect(G_OBJECT(combo), "changed", G_CALLBACK(evt_menu_activated), inp);
	pack_input_widget(dlg, combo, 1, 0);

	return 1;
}

/* 1998-11-29 -	Build an input combo box widget, defined by a {Ic...} code.
** 1999-02-27 -	New packing scheme fixes.
*/
static gint build_input_combo(CmdDlg *dlg, const gchar *def, CmdInp *inp)
{
	GtkWidget	*combo;
	const gchar	*itext;

	if(*def != '=')
		return 0;

	combo = gtk_combo_box_text_new();
	while((def = stu_scan_string(def, &itext)) != NULL)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), itext);
	inp->type = CITP_COMBO;
	pack_input_widget(dlg, inp->inp.combo.combo = combo, 1, 0);

	return 1;
}

/* 1999-02-27 -	Build a label for the input window. This replaces the old single label, which was
**		only *set* by {It...}, not created. Now {It...} creates and packs another label,
**		or even a separator (if the code is {It:-}).
*/
static gint build_input_label(CmdDlg *dlg, const gchar *def, CmdInp *inp)
{
	if(def != NULL)
	{
		if(strcmp(def, "-") == 0)	/* Is the label a single hyphen? Then generate separator bar. */
		{
			inp->type = CITP_LABEL;
			pack_input_widget(dlg, inp->inp.label.label = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 2, 5);
		}
		else
		{
			if(*def == '-')		/* Skip first hyphen. To get a single visible one, use {It:"--"}. */
				def++;
			inp->type = CITP_LABEL;
			pack_input_widget(dlg, inp->inp.label.label = gtk_label_new(def), 2, 0);
		}
	}
	return def != NULL;
}

/* 1999-08-29 -	Build an input checkbox. Currently rather uglily (?) rendered, with just a box and
**		the label on the left side. A pressed-in ("checked") box generates TRUE, a non-
**		checked box emits a FALSE.
*/
static gint build_input_checkbox(CmdDlg *dlg, const gchar *label, const gchar *def, CmdInp *inp)
{
	if(def != NULL)
	{
		inp->type = CITP_CHECKBOX;
		inp->inp.checkbox.check = gtk_check_button_new_with_label(label);
		inp->inp.checkbox.active   = the_true;
		inp->inp.checkbox.inactive = the_false;

		if(*def == '*')	/* Pre-checked mode? */
		{
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(inp->inp.checkbox.check), TRUE);
			def++;
		}
		if(*def == '=')	/* Non-default result strings requested? */
		{
			def = stu_scan_string(++def, (const gchar **) &inp->inp.checkbox.active);
			if(*def == ',')
				stu_scan_string(++def, (const gchar **) &inp->inp.checkbox.inactive);
		}
		pack_input_widget(dlg, inp->inp.checkbox.check, 1, 0);
	}
	return def != NULL;
}

/* 1998-09-25 -	Parse a code dealing with an input dialog {I...}. Tough stuff.
** 1999-02-24 -	Updated to use new argument storage/handling format.
*/
static void parse_code_input(MainInfo *min, GString *cstr, gchar *code, CmdDlg *dlg)
{
	gchar		*ltxt = NULL, *end;
	gchar		*def = code + 1;
	gint		succ = 0;
	GtkWidget	*label;

	dlg->min = min;
	if(dlg->body == NULL)
		init_input_dialog(dlg);

	if(strlen(code) >= 5)		/* Enough room for a (possibly empty) label? */
	{
		if(code[2] == ':' && code[3] == '"')	/* Colon followed by quote? */
		{
			ltxt = code + 4;
			if((end = strchr(ltxt, '"')) != NULL)
			{
				*end = '\0';
				def = end + 1;
			}
			else
				ltxt = NULL;
		}
	}
	else
		def++;

	switch(code[1])
	{
		case 's':
			succ = build_input_string(dlg, def, &dlg->input[dlg->row]);
			if(dlg->str_count++ == 0)
				g_signal_connect(G_OBJECT(dlg->input[dlg->row].inp.string.entry), "realize", G_CALLBACK(evt_input_string_realize), &dlg->str_focused);
			break;
		case 'm':
			succ = build_input_menu(dlg, def, &dlg->input[dlg->row]);
			break;
		case 'c':
			succ = build_input_combo(dlg, def, &dlg->input[dlg->row]);
			break;
		case 't':
			succ = build_input_label(dlg, ltxt, &dlg->input[dlg->row]);
			break;
		case 'w':
			if(dlg->wintitle != NULL)
				g_free(dlg->wintitle);
			dlg->wintitle = g_strdup(ltxt);
			break;
		case 'x':
			succ = build_input_checkbox(dlg, ltxt, def, &dlg->input[dlg->row]);
			ltxt = NULL;
			break;
	}

	if(succ)	/* Input widget built successfully? */
	{
		/* Don't do standard label and INPxx-field for label-type fields. */
		if(dlg->input[dlg->row].type != CITP_LABEL)
		{
			if(ltxt)
			{
				label = gtk_label_new(ltxt);
				gtk_grid_attach(GTK_GRID(dlg->grid), label, 0, dlg->row, 1, 1);
			}
			g_string_sprintfa(cstr, INP_PATTERN, dlg->row++);
		}
		else
			dlg->row++;
	}
}

/* 1998-09-25 -	Get a pointer to a string representation of row <row> in dialog <dlg>.
** 2010-05-16 -	Don't return text pointers; append to the given GString, return FALSE on failure.
*/
static gboolean eval_and_append_input(CmdDlg *dlg, gint row, GString *out)
{
	GList	*data;
	gchar	*tmp;

	if(row >= 0 && row < dlg->row)
	{
		switch(dlg->input[row].type)
		{
			case CITP_STRING:
				g_string_append(out, gtk_entry_get_text(GTK_ENTRY(dlg->input[row].inp.string.entry)));
				return TRUE;
			case CITP_MENU:
				if((data = g_list_nth(dlg->input[row].inp.menu.items, dlg->input[row].inp.menu.current)) != NULL)
				{
					gchar	*colon;

					if((colon = strchr(data->data, ':')) != NULL)
						g_string_append(out, colon + 1);
					else
						g_string_append(out, data->data);
					return TRUE;
				}
				return FALSE;
			case CITP_COMBO:
				tmp = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dlg->input[row].inp.combo.combo));
				g_string_append(out, tmp);
				return TRUE;
			case CITP_CHECKBOX:
				if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dlg->input[row].inp.checkbox.check)))
					g_string_append(out, dlg->input[row].inp.checkbox.active);
				else
					g_string_append(out, dlg->input[row].inp.checkbox.inactive);
				return TRUE;
			case CITP_LABEL:
				return TRUE;		/* Just to keep the compiler happy. Labels don't generate INPxx's. */
		}
	}
	return FALSE;
}

/* 1999-02-24 -	Interpolate any INP-fields into <cstr>, by grabbing data from <dlg>.
 **		For simplicity, done in two steps with an intermediate scratch string.
*/
static void interpolate_input(GString *cstr, CmdDlg *dlg)
{
	gchar	*ptr;
	gint	row;
	GString	*scratch;

	scratch = g_string_new(NULL);
	for(ptr = cstr->str; *ptr;)
	{
		if(sscanf(ptr, INP_PATTERN, &row) == 1)
		{
			eval_and_append_input(dlg, row, scratch);
			ptr += INP_PAT_LEN;
		}
		else
			g_string_append_c(scratch, *ptr++);
	}
	g_string_assign(cstr, scratch->str);
	g_string_free(scratch, TRUE);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-10-07 -	Free a string from a menu item list. */
static void free_menu(gpointer a, gpointer b)
{
	g_free(a);
}

/* 1998-10-07 -	Destroy any extra data kept by input fields. */
static void destroy_input(CmdDlg *dlg)
{
	guint	i;

	for(i = 0; i < dlg->row; i++)
	{
		switch(dlg->input[i].type)
		{
			case CITP_STRING:
				break;
			case CITP_MENU:
				g_list_foreach(dlg->input[i].inp.menu.items, free_menu, NULL);
				g_list_free(dlg->input[i].inp.menu.items);
				break;
			case CITP_COMBO:
				break;
			case CITP_LABEL:
				break;
			case CITP_CHECKBOX:
				if(dlg->input[i].inp.checkbox.active != the_true)
					g_free((gpointer) dlg->input[i].inp.checkbox.active);
				if(dlg->input[i].inp.checkbox.inactive != the_false)
					g_free((gpointer) dlg->input[i].inp.checkbox.inactive);
				break;
		}
	}
	if(dlg->wintitle != NULL)
		g_free(dlg->wintitle);
}

/* ----------------------------------------------------------------------------------------- */

/* 2003-11-16 -	Get ending brace for a code definition. Knows about embedded braces, unlike strchr(). */
static const gchar * get_code_end(const gchar *def)
{
	const gchar	*base = def;
	gint		level;

	for(level = 1; *def; def++)
	{
		if(def > base && def[-1] != '\\')
		{
			if(*def == '{')
				level++;
			else if(*def == '}')
				level--;
			if(level == 0)
				break;
		}
	}
	if(*def && level == 0)
		return def;
	return NULL;
}

/* 1998-09-25 -	Parse a word that contains 1 or more special "brace-codes". The codes
**		need to be replaced by a value, or an input-dialog place holder.
*/
static void parse_code(MainInfo *min, GString *cstr, gchar *code, CmdDlg *dlg, gboolean input_ok)
{
	if(code == NULL || cstr == NULL)
		return;

	while(code && *code)
	{
		if(*code == '\\')		/* Backslash escape? */
		{
			if(code[1])
			{
				g_string_append_c(cstr, code[1]);
				code += 2;
			}
			else
				break;
		}
		else if(*code == '{')		/* Code here? */
		{
			gchar	*end;

			code++;
			if((end = (gchar *) get_code_end(code)) != NULL)
			{
				*end = '\0';		/* This is in a GString, so OK. */
				if(code[0] == 'f' || code[0] == 'F')
					parse_code_file(min, cstr, code);
				else if(code[0] == 'P')
					parse_code_path(min, cstr, code);
				else if(code[0] == 'u' || code[0] == 'U')
					parse_code_uri(min, cstr, code);
				else if(code[0] == '$')
					parse_code_env(min, cstr, code);
				else if(code[0] == '#')
					parse_code_pid(min, cstr, code);
				else if(code[0] == '~')
					parse_code_home(min, cstr, code);
				else if(code[0] == 'I' && input_ok)
					parse_code_input(min, cstr, code, dlg);
				code = end + 1;
			}
		}
		else
			g_string_append_c(cstr, *code++);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-02-25 -	Return a pointer to the character *after* the closing brace matching the
**		one at <ptr>. Returns NULL if no such brace could be found.
*/
static const gchar * code_end(const gchar *ptr)
{
	if(ptr != NULL)
	{
		gint	count;

		for(count = 1, ptr++; *ptr && count > 0; ptr++)
		{
			if(*ptr == '{' && ptr[-1] != '\\')
				count++;
			else if(*ptr == '}' && ptr[-1] != '\\')
				count--;
		}
		return ptr;
	}
	return NULL;
}

/* 1999-02-25 -	Interpolate {}-codes in <str>, replacing them with whatever they should
**		evaluate to. For input codes, this means the placeholder "INPxx" string.
**		Returns new version of string. Doesn't touch <str> at all. Note that
**		quotes don't suppress {}-code interpolation; the only way to do so is to
**		escape the initial brace with a backslash.
*/
static GString * interpolate_brace_codes(MainInfo *min, const gchar *str, CmdDlg *dlg)
{
	const gchar	*base, *ptr;
	GString		*tmp;

	tmp = g_string_new(NULL);

	for(base = ptr = str; base != NULL && ptr != NULL;)
	{
		if((ptr = strchr(base, '{')) != NULL)		/* Get next code. */
		{
			if(ptr == base || (*(ptr - 1) != '\\'))	/* Genuine? */
			{
				const gchar	*end;

				if((end = code_end(ptr)) != NULL)
				{
					GString	*code;

					for(; base < ptr;)
						g_string_append_c(tmp, *base++);
					code = g_string_new(ptr);
					g_string_truncate(code, end - ptr);
					parse_code(min, tmp, code->str, dlg, TRUE);
					g_string_free(code, TRUE);
					base = end;
				}
			}
			else
			{
				for(; base <= ptr;)
					g_string_append_c(tmp, *base++);
			}
		}
		else
			g_string_append(tmp, base);
	}
	return tmp;
}

/* 1998-09-25 -	Rewritten from scratch. This function parses the command definition in <def>, and returns
**		an argv-style vector of the "words" contained therein. It also does {}-code substitution,
**		and is generally cool. The returned vector is dynamically allocated and MUST be freed
**		when you're done with it - first word by word, then the entire vector!
**		The major change in this rewrite is that it is self-contained; the caller need not bother
**		with the dialog any longer - it's all internal. We just return the argument vector, with
**		any dialog-related stuff *already there*, or NULL on failure (or dialog cancel). Great!
** 1999-02-24 -	Semi-rewritten. In the old version, each {}-code except for {f} resulted in exactly ONE
**		argument, regardless of the number of words emitted. This has changed, now spaces separate
**		arguments, and quoting has to be used to create arguments with spaces in them. This is
**		more like the shell, a lot more flexible, and generally the right thing. I think.
*/
gchar ** cpr_parse(MainInfo *min, const gchar *def)
{
	GString		*cstr;
	static CmdDlg	dlg;

	if(min == NULL || def == NULL)
		return NULL;

	dlg.body = NULL;
	dlg.row = 0;
	dlg.wintitle = NULL;
	dlg.str_count = 0;
	dlg.str_focused = FALSE;

	cstr = interpolate_brace_codes(min, def, &dlg);

	if(dlg.body != NULL)	/* Do we need an input window? */
	{
		dlg.dlg = dlg_dialog_sync_new(dlg.body, dlg.wintitle, NULL);
		if(dlg_dialog_sync_wait(dlg.dlg) == DLG_POSITIVE)	/* OK? */
			interpolate_input(cstr, &dlg);
		else
		{
			g_string_free(cstr, TRUE);
			cstr = NULL;
		}
		destroy_input(&dlg);
		dlg_dialog_sync_destroy(dlg.dlg);
	}
	if(cstr != NULL)	/* Did we get any arguments? */
	{
		gchar	**argv;

		argv = stu_split_args(cstr->str);
		g_string_free(cstr, TRUE);
		return argv;
	}
	return NULL;
}

/* 1998-09-26 -	Free an argument vector as returned by cpr_parse(). Not complex, but more than
**		one line - so well worth a function of its own.
** 1999-02-24 -	Thanks to the allocation intelligence of the new stu_split_args() routine, this
**		has considerably less to do. Fine by me.
*/
void cpr_free(gchar **argv)
{
	if(argv != NULL)
		g_free(argv);
}
