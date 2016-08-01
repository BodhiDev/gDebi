/*
** 1998-09-14 -	A complete rewrite from scratch (original version written yesterday :().
**		Now handles a set of button "sheets". A sheet is a named collection of
**		button rows. Each row can contain an arbitrary number of buttons.
** 1999-05-01 -	Huge changes and increased data structure opacity to implement multi-
**		faceted widgets. Will probably be a huge win. :^)
** 1999-05-12 -	Found and fixed a couple of seemingly old memory allocation bugs.
*/

#include "gentoo.h"

#include <stdlib.h>

#include "cmdseq.h"
#include "menus.h"
#include "odmultibutton.h"
#include "strutil.h"
#include "xmlutil.h"

#include "buttons.h"

/* ----------------------------------------------------------------------------------------- */

#define	BTN_DEFAULT_SHEET	"Default"

/* This holds the color for a face. It includes a 'valid' member since it's entierly
** possible not to set the color (at all) for a face, thus getting the default color
** for the current GTK+ theme (I hope/assume/think). Therefore we need to know if the
** color is actually in-use or not.
*/
typedef struct {
	gboolean	valid;
	GdkColor	color;
} BColor;

struct Button {				/* A run-time button definition, as stored in button banks. */
	gchar	label[BTN_FACES][BTN_LABEL_SIZE];	/* User-visible text to label button with. */
	GString	*cmdseq[BTN_FACES];			/* Command sequence to run when clicked. */
	gchar	key[BTN_FACES][KEY_NAME_SIZE];		/* Keyboard shortcut. */
	BColor	color[BTN_FACES][2];			/* Back- and foreground colors for each face. */
	gchar	menu[MNU_MENU_NAME_SIZE];		/* Name of menu bound to this button. */
	gchar	tooltip[BTN_TOOLTIP_SIZE];		/* Tooltip (help text) for this button. */
	guint32	flags;					/* Various flags. See BTF_XXX defines in header. */
};

struct ButtonRow {			/* A single row of buttons. */
	guint	width;				/* How many buttons in this row? */
	Button	*button;			/* A vector of button definitions. */
};

struct ButtonSheet {			/* A "sheet" of buttons; basically a list of rows. */
	gchar	label[BTN_LABEL_SIZE];		/* The list has a name, which might be handy in the future. */
	guint	height;				/* Number of rows (length of <rows> list). */
	guint	visible;			/* Number of rows to keep visible. */
	guint	top_row;			/* Index of currently topmost visible row. */
	GList	*rows;				/* List of rows for this sheet. */
};

/* ----------------------------------------------------------------------------------------- */

static gboolean	button_set_face(GtkWidget *widget, BtnFace face);

static ButtonSheet *	btn_buttonsheet_new_default_commands(MainInfo *min);
static ButtonSheet *	btn_buttonsheet_new_default_shortcuts(MainInfo *min);

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-15 -	Set the given <bti> to the default (built-in) buttons. */
void btn_buttoninfo_new_default(MainInfo *min, ButtonInfo *bti)
{
	ButtonSheet	*bsh;

	bti->sheets = NULL;
	bsh = btn_buttonsheet_new_default_commands(min);
	bti->sheets = g_list_append(bti->sheets, bsh);
	bsh = btn_buttonsheet_new_default_shortcuts(min);
	bti->sheets = g_list_append(bti->sheets, bsh);
}

/* 1998-09-15 -	Copy <src> into <dst>, which really should be empty (or uninitialized). */
void btn_buttoninfo_copy(ButtonInfo *dst, ButtonInfo *src)
{
	ButtonSheet	*bsh;
	GList		*iter;

	dst->sheets = NULL;

	for(iter = src->sheets; iter != NULL; iter = g_list_next(iter))
	{
		if((bsh = btn_buttonsheet_copy(iter->data)) != NULL)
			dst->sheets = g_list_append(dst->sheets, bsh);
	}
}

/* 1998-12-25 -	Add a buttonsheet to given buttoninfo. Really simple stuff. */
void btn_buttoninfo_add_sheet(ButtonInfo *bti, ButtonSheet *bsh)
{
	GList	*iter, *next;

	if(bti == NULL || bsh == NULL)
		return;
	for(iter = bti->sheets; iter != NULL; iter = next)
	{
		next = g_list_next(iter);
		if(strcmp(((ButtonSheet *) iter->data)->label, bsh->label) == 0)
		{
			bti->sheets = g_list_remove_link(bti->sheets, iter);
			btn_buttonsheet_destroy(iter->data);
			g_list_free_1(iter);
		}
	}
	bti->sheets = g_list_append(bti->sheets, bsh);
}

/* 1998-09-15 -	Destroy all button sheets described by <bti>. */
void btn_buttoninfo_clear(ButtonInfo *bti)
{
	GList	*iter;

	for(iter = bti->sheets; iter != NULL; iter = g_list_next(iter))
		btn_buttonsheet_destroy(iter->data);
	g_list_free(bti->sheets);
	bti->sheets = NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-01 -	Save button face definitions. */
static void save_button_faces(Button *b, FILE *out)
{
	const gchar	*str, *str2;
	guint		i;
	GdkColor	col;

	for(i = 0; i < BTN_FACES; i++)
	{
		if((str = btn_button_get_cmdseq(b, i)) != NULL)
		{
			xml_put_node_open(out, "BFace");
			xml_put_uinteger(out, "face", i);
			if((str2 = btn_button_get_label(b, i)) != NULL)
				xml_put_text(out, "label", str2);
			if((str2 = btn_button_get_cmdseq(b, i)) != NULL)
				xml_put_text(out, "cmdseq", str2);
			if((str2 = btn_button_get_key(b, i)) != NULL)
				xml_put_text(out, "key", str2);
			if(btn_button_get_color_bg(b, i, &col))
				xml_put_color(out, "bg", &col);
			if(btn_button_get_color_fg(b, i, &col))
				xml_put_color(out, "fg", &col);
			xml_put_node_close(out, "BFace");
		}
	}
}

/* 1998-09-15 -	Save a single button. Note how we use the <pos> thing to allow suppression
**		of empty buttons on load.
** 1999-05-01 -	Adapted to the new multi-face view of buttons.
*/
static void save_button(Button *b, guint pos, FILE *out)
{
	const gchar	*str;

	if(!btn_button_is_blank(b))		/* Avoid saving completely empty buttons. */
	{
		xml_put_node_open(out, "Button");
		xml_put_uinteger(out, "pos", pos);

		xml_put_node_open(out, "BFaces");
		save_button_faces(b, out);
		xml_put_node_close(out, "BFaces");

		if((str = btn_button_get_tooltip(b)) != NULL)
			xml_put_text(out, "tooltip", str);
		xml_put_uinteger(out, "flags", btn_button_get_flags(b));
		xml_put_node_close(out, "Button");
	}
}

/* 1998-09-15 -	Save a row of buttons. */
static void save_row(gpointer r, gpointer f)
{
	FILE		*out = f;
	ButtonRow	*brw = r;
	guint		i;

	xml_put_node_open(out, "ButtonRow");
	xml_put_integer(out, "width", brw->width);
	xml_put_node_open(out, "ButtonRowButtons");
	for(i = 0; i < brw->width; i++)
		save_button(brw->button + i, i, out);	
	xml_put_node_close(out, "ButtonRowButtons");
	xml_put_node_close(out, "ButtonRow");
}

/* 1998-09-15 -	Save a sheet of buttons. */
static void save_sheet(gpointer s, gpointer f)
{
	FILE		*out = f;
	ButtonSheet	*bsh = s;

	xml_put_node_open(out, "ButtonSheet");
	xml_put_text(out, "label", bsh->label);
	xml_put_node_open(out, "ButtonSheetRows");
	g_list_foreach(bsh->rows, save_row, f);
	xml_put_node_close(out, "ButtonSheetRows");
	xml_put_node_close(out, "ButtonSheet");
}

/* 1999-05-01 -	Save all currently defined buttons. */
void btn_buttoninfo_save(MainInfo *min, const ButtonInfo *bti, FILE *out, const gchar *tag)
{
	xml_put_node_open(out, tag);
	g_list_foreach(bti->sheets, save_sheet, out);
	xml_put_node_close(out, tag);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-01 -	Load the face definitions for a given button. */
static void load_button_faces(const XmlNode *node, gpointer button)
{
	Button		*btn = button;	/* Saves a few casts. */
	const gchar	*str;
	guint		face;
	GdkColor	tmp_c;

	if(xml_get_uinteger(node, "face", &face))
	{
		if(xml_get_text(node, "label", &str))
			btn_button_set_label(btn, face, str);
		if(xml_get_text(node, "cmdseq", &str))
			btn_button_set_cmdseq(btn, face, str);
		if(xml_get_text(node, "key", &str))
			btn_button_set_key(btn, face, str);
		if(xml_get_color(node, "bg", &tmp_c))
			btn_button_set_color_bg(btn, face, &tmp_c);
		if(xml_get_color(node, "fg", &tmp_c))
			btn_button_set_color_fg(btn, face, &tmp_c);
	}
}

/* 1999-05-01 -	Load a single button. Moved and adapted from cfg_buttons, where it used to
**		live up to now.
** FIXMEFIXME -	This function contains backwards-compatibility. Yuck. Remove later.
*/
static void load_button(const XmlNode *node, gpointer r)
{
	ButtonRow	*brw = r;
	Button		*btn;
	const gchar	*str;
	gint		pos;
	guint		pos2;

	if(xml_get_integer(node, "pos", &pos))		/* If position is signed, it's the old format. */
	{
		gint	tmp;

		btn = brw->button + pos;
		if(xml_get_text(node, "label", &str))
			btn_button_set_label(btn, BTN_PRIMARY, str);
		if(xml_get_text(node, "cmd_seq", &str))
			btn_button_set_cmdseq(btn, BTN_PRIMARY, str);
		if(xml_get_text(node, "key", &str))
			btn_button_set_key(btn, BTN_PRIMARY, str);

		if(xml_get_text(node, "tooltip", &str))
			btn_button_set_tooltip(btn, str);

		if(xml_get_boolean(node, "narrow", &tmp))
			btn_button_set_flags_boolean(btn, BTF_NARROW, tmp);
		if(xml_get_boolean(node, "show_tooltip", &tmp))
			btn_button_set_flags_boolean(btn, BTF_SHOW_TOOLTIP, tmp);
	}
	else if(xml_get_uinteger(node, "pos", &pos2))	/* If position is unsigned (more logical), we're modern. */
	{
		guint		tmpu;
		const XmlNode	*bfaces;

		btn = brw->button + pos2;

		if((bfaces = xml_tree_search(node, "BFaces")) != NULL)
			xml_node_visit_children(bfaces, load_button_faces, btn);

		if(xml_get_text(node, "tooltip", &str))
			btn_button_set_tooltip(btn, str);
		if(xml_get_uinteger(node, "flags", &tmpu))
			btn_button_set_flags(btn, tmpu);
	}
}

static void load_row(const XmlNode *node, gpointer s)
{
	ButtonSheet	*bsh = s;
	ButtonRow	*brw;
	const XmlNode	*data;
	gint		width;

	if(xml_get_integer(node, "width", &width))
	{
		if(width >= 1)
		{
			if((brw = btn_buttonrow_new(width)) != NULL)
			{
				if((data = xml_tree_search(node, "ButtonRowButtons")) != NULL)
					xml_node_visit_children(data, load_button, brw);
				btn_buttonsheet_append_row(bsh, brw);
			}
		}
		else
			fprintf(stderr, "**BUTTONS: A row width of %d is unsupportedly silly\n", width);
	}
}

static void load_sheet(const XmlNode *node, gpointer user)
{
	ButtonSheet	*bsh;
	const XmlNode	*data;

	if((bsh = btn_buttonsheet_new(NULL)) != NULL)
	{
		xml_get_text_copy(node, "label", bsh->label, sizeof bsh->label);
		if((data = xml_tree_search(node, "ButtonSheetRows")) != NULL)
			xml_node_visit_children(data, load_row, bsh);
		btn_buttoninfo_add_sheet(user, bsh);
	}
}

void btn_buttoninfo_load(MainInfo *min, ButtonInfo *bti, const XmlNode *node)
{
	xml_node_visit_children(node, load_sheet, bti);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-08 -	If <base> is NULL, allocate <num> consequent buttons, and return a pointer to the
**		first one. If <base> is non-NULL, assume it to be the pointer to a vector of <num>
**		buttons, and just do the initialization. Note that if you allocate buttons on your
**		own, then calling this function is *mandatory* before using the button!
*/
Button * btn_button_new(Button *base, guint num)
{
	if(num > 0)
	{
		guint	i, j;

		if(base == NULL)
			base = g_malloc(num * sizeof *base);
		for(i = 0; i < num; i++)
		{
			for(j = 0; j < BTN_FACES; j++)
			{
				base[i].label[j][0] = '\0';
				base[i].cmdseq[j] = NULL;
				base[i].key[j][0] = '\0';
				base[i].color[j][0].valid = FALSE;
				base[i].color[j][1].valid = FALSE;
			}
			base[i].menu[0] = '\0';
			base[i].tooltip[0] = '\0';
			base[i].flags = 0U;
		}
	}
	return base;
}

/* 1999-03-31 -	Set the label of a button. */
void btn_button_set_label(Button *btn, BtnFace face, const gchar *label)
{
	if(btn != NULL)
	{
		if(label != NULL)
			g_strlcpy(btn->label[face], label, sizeof btn->label[face]);
		else
			btn->label[face][0] = '\0';
	}
}

/* 1999-05-01 -	Set the label of an already built button. The <widget> argument MUST be
**		a pointer to a widget as returned by btn_button_build(). Note that this
**		will use some magic to also change the corresponding definition (Button).
*/
void btn_button_set_label_widget(GtkWidget *widget, BtnFace face, const gchar *label)
{
	if((widget != NULL) && (label != NULL))
	{
		Button	*btn = btn_button_get(widget);

		btn_button_set_label(btn, face, label);
		button_set_face(widget, face);
	}
}

/* 1998-09-15 -	Set a new command sequence for given button.
** 1998-09-25 -	Rewritten as a consequence of the new sequence format.
** 1999-05-08 -	Rewritten, since command sequences are now better handled as GStrings.
*/
void btn_button_set_cmdseq(Button *btn, BtnFace face, const gchar *seq)
{
	if(btn != NULL)
	{
		if(seq != NULL)
		{
			if(btn->cmdseq[face] != NULL)
				g_string_assign(btn->cmdseq[face], seq);
			else
				btn->cmdseq[face] = g_string_new(seq);
		}
		else if(btn->cmdseq[face] != NULL)
		{
			g_string_free(btn->cmdseq[face], TRUE);
			btn->cmdseq[face] = NULL;
		}
	}
}

/* 1999-03-11 -	Set the keyboard accelerator name for this button. */
void btn_button_set_key(Button *btn, BtnFace face, const gchar *key)
{
	if(btn != NULL)
	{
		if(key != NULL)
			g_strlcpy(btn->key[face], key, sizeof btn->key[face]);
		else
			btn->key[face][0] = '\0';
	}
}

/* 1999-05-01 -	Set back- and foreground colors for given button face to <fg> and <bg>. Any of
**		the two colors can be NULL, in case it will be *ignored*, NOT reset! To reset,
**		use btn_button_set_color_XX() with a NULL color. Color me crazy on API design.
*/
void btn_button_set_colors(Button *btn, BtnFace face, const GdkColor *bg, const GdkColor *fg)
{
	if(btn != NULL)
	{
		if(bg != NULL)
		{
			btn->color[face][0].valid = TRUE;
			btn->color[face][0].color = *bg;
		}
		if(fg != NULL)
		{
			btn->color[face][1].valid = TRUE;
			btn->color[face][1].color = *fg;
		}
	}
}

/* 1999-05-02 -	Set background color of indicated button's indicated face to <bg>. If
**		<bg> == NULL, the color is reset (marked as invalid internally).
*/
void btn_button_set_color_bg(Button *btn, BtnFace face, const GdkColor *bg)
{
	if(btn != NULL)
	{
		if((btn->color[face][0].valid = (bg != NULL)) != 0)
			btn->color[face][0].color = *bg;
	}
}

/* 1999-05-02 -	Set background color of already constructed button. */
void btn_button_set_color_bg_widget(GtkWidget *widget, BtnFace face, const GdkColor *bg)
{
	if(widget != NULL)
	{
		Button	*btn = btn_button_get(widget);

		btn_button_set_color_bg(btn, face, bg);
		button_set_face(widget, face);
	}
}

/* 1999-05-02 -	Set foreground color of indicated button's indicated face to <bg>. If
**		<bg> == NULL, the color is reset (marked as invalid internally).
*/
void btn_button_set_color_fg(Button *btn, BtnFace face, const GdkColor *fg)
{
	if(btn != NULL)
	{
		if((btn->color[face][1].valid = (fg != NULL)) != 0)
			btn->color[face][1].color = *fg;
	}
}

/* 1999-05-02 -	Set foreground color of already constructed button. */
void btn_button_set_color_fg_widget(GtkWidget *widget, BtnFace face, const GdkColor *fg)
{
	if(widget != NULL)
	{
		Button	*btn = btn_button_get(widget);

		btn_button_set_color_fg(btn, face, fg);
		button_set_face(widget, face);
	}
}

/* 1999-05-01 -	Get the definition from an instantiated widget. */
Button * btn_button_get(GtkWidget *wid)
{
	if(wid != NULL)
		return g_object_get_data(G_OBJECT(wid), "button");
	return NULL;
}

/* 1999-05-01 -	Return label for given button face. */
const gchar * btn_button_get_label(Button *btn, BtnFace face)
{
	if((btn != NULL) && (btn->label[face][0] != '\0'))
		return btn->label[face];
	return NULL;
}

const gchar * btn_button_get_cmdseq(Button *btn, BtnFace face)
{
	if((btn != NULL) && (btn->cmdseq[face] != '\0'))
		return btn->cmdseq[face]->str;
	return NULL;
}

/* 1999-05-01 -	Return pointer to keyboard accelerator key string for given button. */
const gchar * btn_button_get_key(Button *btn, BtnFace face)
{
	if((btn != NULL) && (btn->key[face][0] != '\0'))
		return btn->key[face];
	return NULL;
}

/* 1999-05-01 -	Return (in <bg>) the background color of <btn>. If the color has not
**		been previously set (it's not valid), *<bg> is untouched and FALSE is
**		returned. On success, TRUE is returned.
*/
gboolean btn_button_get_color_bg(Button *btn, BtnFace face, GdkColor *bg)
{
	if((btn != NULL) && (bg != NULL))
	{
		if(btn->color[face][0].valid)
		{
			*bg = btn->color[face][0].color;
			return TRUE;
		}
	}
	return FALSE;
}

/* 1999-05-01 -	Return (in <bg>) the background color of <btn>. If the color has not
**		been previously set (it's not valid), *<bg> is untouched and FALSE is
**		returned. On success, TRUE is returned.
*/
gboolean btn_button_get_color_fg(Button *btn, BtnFace face, GdkColor *fg)
{
	if((btn != NULL) && (fg != NULL))
	{
		if(btn->color[face][1].valid)
		{
			*fg = btn->color[face][1].color;
			return TRUE;
		}
	}
	return FALSE;
}

/* 1999-01-13 -	Just install a tooltip string. Really simple. */
void btn_button_set_tooltip(Button *btn, const gchar *tip)
{
	if(btn != NULL)
	{
		if(tip != NULL)
			g_strlcpy(btn->tooltip, tip, sizeof btn->tooltip);
		else
			btn->tooltip[0] = '\0';
	}
}

/* 1999-05-01 -	Return (strictly read-only) pointer to <btn>'s tooltip string. */
const gchar * btn_button_get_tooltip(Button *btn)
{
	if((btn != NULL) && (btn->tooltip[0] != '\0'))
		return btn->tooltip;
	return NULL;
}


/* 1999-05-01 -	Set the button's flags to (exactly) <flags>. */
void btn_button_set_flags(Button *btn, guint32 flags)
{
	if(btn != NULL)
		btn->flags = flags;
}

/* 1999-05-01 -	Return the current flag values for <btn>. */
guint32 btn_button_get_flags(Button *btn)
{
	if(btn != NULL)
		return btn->flags;
	return 0U;
}

/* 1999-05-01 -	Make the flags indicated by <mask> (each) equal to <value>. This means,
**		that if <value> is TRUE, all masked flags will be set, else cleared.
*/
void btn_button_set_flags_boolean(Button *btn, guint32 mask, gboolean value)
{
	if(btn != NULL)
	{
		if(value)
			btn->flags |= mask;
		else
			btn->flags &= ~mask;
	}
}

/* 1999-05-01 -	Return TRUE if and only if all masked flags of <btn> are set. */
gboolean btn_button_get_flags_boolean(Button *btn, guint32 mask)
{
	if(btn != NULL)
		return ((btn->flags & mask) == mask) ? TRUE : FALSE;
	return FALSE;
}

/* 1999-05-01 -	Returns TRUE if given button has no command sequence on any face, else FALSE. */
gboolean btn_button_is_blank(Button *btn)
{
	guint	i;

	for(i = 0; i < BTN_FACES; i++)
	{
		if(btn_button_get_cmdseq(btn, i) != NULL)
			return FALSE;
	}
	return TRUE;
}

/* 1998-09-15 -	Clear <btn> to some vacuum state. */
void btn_button_clear(Button *btn)
{
	guint	i;

	for(i = 0; i < BTN_FACES; i++)
	{
		btn_button_set_label(btn, i, NULL);
		btn_button_set_cmdseq(btn, i, NULL);
		btn_button_set_key(btn, i, NULL);
		btn->color[i][0].valid = FALSE;		/* Invalidate back- and foreground colors. */
		btn->color[i][1].valid = FALSE;
	}
	btn_button_set_tooltip(btn, NULL);
	btn->flags = 0U;
}

/* 1998-09-15 -	Swap the contents of the two buttons, without doing (dynamic) memory allocation. */
void btn_button_swap(Button *a, Button *b)
{
	Button	temp;

	temp = *a;
	*a = *b;
	*b = temp;
}

/* 1998-09-15 -	Write a copy of <src> into <dst>. */
void btn_button_copy(Button *dst, Button *src)
{
	if((dst != NULL) && (src != NULL))
	{
		guint	i;

		for(i = 0; i < BTN_FACES; i++)
		{
			g_strlcpy(dst->label[i], src->label[i], sizeof dst->label[i]);
			btn_button_set_cmdseq(dst, i, btn_button_get_cmdseq(src, i));
			g_strlcpy(dst->key[i], src->key[i], sizeof dst->key[i]);
			dst->color[i][0] = src->color[i][0];
			dst->color[i][1] = src->color[i][1];
		}
		g_strlcpy(dst->menu, src->menu, sizeof dst->menu);
		g_strlcpy(dst->tooltip, src->tooltip, sizeof dst->tooltip);
		dst->flags = src->flags;
	}
}

/* 1999-05-02 -	Copy color information from <src> to <dst>. Can be made from other
**		function calls, but I felt lazy/overambitious.
*/
void btn_button_copy_colors(Button *dst, Button *src)
{
	if((dst != NULL) && (src != NULL))
	{
		guint	i;

		for(i = 0; i < BTN_FACES; i++)
		{
			dst->color[i][0] = src->color[i][0];
			dst->color[i][1] = src->color[i][1];
		}
	}
}

/* 1998-09-15 -	Destroy a button. Very simple, since buttons are never allocated separately. */
void btn_button_destroy(Button *b)
{
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-01 -	This function gets called when the user clicks on an action button, regardless
**		of which "face" it was. This assumes that MainInfo as passed as last argument
**		to the btn_buttonsheet_build() call that resulted in the widget.
** 2004-11-19 -	Ported to use new ODMultiButton.
*/
static void evt_button_clicked(GtkWidget *wid, gpointer user)
{	
	Button		*btn;
	MainInfo	*min = user;
	guint		face = od_multibutton_get_index(OD_MULTIBUTTON(wid));

	btn = g_object_get_data(G_OBJECT(wid), "button");
	if((min != NULL) && (btn != NULL) && (btn->cmdseq[face] != NULL))
		csq_execute(min, btn->cmdseq[face]->str);
}

/* 1999-05-01 -	Update the face of an already constructed button, reading from the definition. */
static gboolean button_set_face(GtkWidget *widget, BtnFace face)
{
	Button		*btn;
	gboolean	ret = FALSE;

	if((widget != NULL) && ((btn = g_object_get_data(G_OBJECT(widget), "button")) != NULL))
	{
		const gchar	*lab;

		if((lab = btn_button_get_label(btn, face)) != NULL)
		{
			GdkColor	bgb, fgb, *bgp = NULL, *fgp = NULL;

			if(btn_button_get_color_bg(btn, face, &bgb))
				bgp = &bgb;
			if(btn_button_get_color_fg(btn, face, &fgb))
				fgp = &fgb;
			od_multibutton_set_text(OD_MULTIBUTTON(widget), face, lab, bgp, fgp, NULL);
			ret = TRUE;
		}
		gtk_widget_queue_draw(widget);
	}
	return ret;
}

/* 1998-09-15 -	Build the single GTK+ button widget for the given <btn>. Connect its clicked
**		signal to <func>, passing it <user> on invocation.
** 1999-05-01 -	Rewritten; now builds an ODEmilButton instead, with multi-face support. Way cool.
*/
static GtkWidget * button_build(MainInfo *min, Button *btn, gboolean partial, GCallback func, gpointer user)
{
	GtkWidget	*but;

	if((but = od_multibutton_new()) != NULL)
	{
		gboolean	set = FALSE;
		BtnFace		i;

		gtk_widget_set_can_focus(but, FALSE);
		g_object_set_data(G_OBJECT(but), "min", min);
		g_object_set_data(G_OBJECT(but), "button", btn);

		for(i = 0; i < BTN_FACES; i++)
			set |= button_set_face(but, i);
		g_signal_connect(G_OBJECT(but), "clicked", G_CALLBACK(func), user);
		if(partial)
			od_multibutton_set_config(OD_MULTIBUTTON(but), TRUE);
	}
	return but;
}

/* 1999-01-13 -	Add a tooltip from the definition of <def>, if enabled, and return TRUE.
**		If the specified button doesn't use tooltips, don't do anything but
**		return FALSE.
*/
static gboolean button_tooltip(MainInfo *min, Button *def, GtkWidget *btn)
{
	if(btn_button_get_flags_boolean(def, BTF_SHOW_TOOLTIP) && (def->tooltip[0] != '\0'))
	{
		gtk_widget_set_tooltip_text(btn, def->tooltip);
		return TRUE;
	}
	return FALSE;
}

/* 1998-09-15 -	Build GUI representation of given row. */
static GtkWidget * buttonrow_build(MainInfo *min, ButtonRow *brw, gboolean partial, GCallback func, gpointer user)
{
	GtkWidget	*hbox, *hbox2 = NULL, *btn;
	guint		i;

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	for(i = 0; i < brw->width;)
	{
		if(btn_button_get_flags_boolean(&brw->button[i], BTF_NARROW))
		{
			hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			for(; i < brw->width && btn_button_get_flags_boolean(&brw->button[i], BTF_NARROW); i++)
			{
				if((btn = button_build(min, brw->button + i, partial, func, user)) != NULL)
				{
					if(!partial)
						button_tooltip(min, brw->button + i, btn);
					g_object_set_data(G_OBJECT(btn), "row", brw);
					gtk_box_pack_start(GTK_BOX(hbox2), btn, FALSE, FALSE, 0);
				}
			}
			gtk_box_pack_start(GTK_BOX(hbox), hbox2, FALSE, TRUE, 0);
		}
		else
		{
			hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			gtk_box_set_homogeneous(GTK_BOX(hbox2), TRUE);
			for(; i < brw->width && !btn_button_get_flags_boolean(&brw->button[i], BTF_NARROW); i++)
			{
				if((btn = button_build(min, brw->button + i, partial, func, user)) != NULL)
				{
					if(!partial)
						button_tooltip(min, brw->button + i, btn);
					g_object_set_data(G_OBJECT(btn), "row", brw);
					gtk_box_pack_start(GTK_BOX(hbox2), btn, TRUE, TRUE, 0);
				}
			}
			gtk_box_pack_start(GTK_BOX(hbox), hbox2, TRUE, TRUE, 0);
		}
	}
	return hbox;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-11-20 -	Return a pointer to a fresh buttonrow holding <width> buttons. */
ButtonRow * btn_buttonrow_new(gint width)
{
	ButtonRow	*brw;

	brw	    = g_malloc(sizeof *brw);
	brw->width  = width;
	brw->button = btn_button_new(NULL, width);

	return brw;
}

#define	DEF_WIDTH	(8)

/* 1998-09-15 -	Build the default button row. Just a few very basic functions. */
ButtonRow * btn_buttonrow_new_default(MainInfo *min)
{
	ButtonRow	*brw;
	const gchar	*lab[BTN_FACES * DEF_WIDTH] =
			 {"All",  "Copy",    "Move",   "Delete", "Rename", "MkDir", "ChMod", "Configure",
			  "None", "Copy As", "Move As", NULL,    NULL,     NULL,    "ChOwn", NULL };
	const gchar	*cmd[BTN_FACES * DEF_WIDTH] =
			 {"SelectAll", "Copy", "Move", "Delete", "Rename", "MkDir", "ChMod", "Configure",
			  "SelectNone", "CopyAs", "MoveAs", NULL, NULL, NULL,       "ChOwn", NULL };
	const gchar	*key[BTN_FACES * DEF_WIDTH] =
			 {NULL, NULL, NULL, "Delete", "F2", NULL, NULL, "c",
			  NULL, NULL, NULL, NULL,     NULL, NULL, NULL, NULL};
	guint		i, face, index;

	if((brw = btn_buttonrow_new(DEF_WIDTH)) != NULL)
	{
		for(i = 0; i < brw->width; i++)
		{
			for(face = 0; face < BTN_FACES; face++)
			{
				index = i + DEF_WIDTH * face;
				btn_button_set_label(brw->button + i, face, lab[index]);
				btn_button_set_cmdseq(brw->button + i, face, cmd[index]);
				if(key[index] != NULL)
					btn_button_set_key(brw->button + i, face, key[index]);
			}
		}
	}
	return brw;
}

/* 1998-09-15 -	Create a new buttonrow that is a copy of <src>.
** 1999-05-08 -	Rewritten. Now builds on existing routines and is generally better.
*/
ButtonRow * btn_buttonrow_copy(ButtonRow *src)
{
	ButtonRow	*brw = NULL;

	if((src != NULL) && ((brw = btn_buttonrow_new(src->width)) != NULL))
	{
		guint	i;

		for(i = 0; i < brw->width; i++)
			btn_button_copy(brw->button + i, src->button + i);
	}
	return brw;
}

/* 1998-09-15 -	Set the width of <brw> to <width> buttons. Keeps the buttons in the
**		(left-aligned) intersection, if any.
*/
void btn_buttonrow_set_width(ButtonRow *brw, guint width)
{
	guint	i, j;

	if((brw != NULL) && (width > 0))
	{
		if(width == brw->width)		/* No change? */
			return;

		if(width < brw->width)		/* Shrinking? */
		{
			for(i = width; i < brw->width; i++)
				btn_button_destroy(brw->button + i);
		}
		brw->button = g_realloc(brw->button, width * sizeof *brw->button);
		if(width > brw->width)          /* Growing? Then init the new ones. */
		{
			for(i = brw->width; i < width; i++)
			{
				for(j = 0; j < sizeof brw->button[i].cmdseq / sizeof *brw->button[i].cmdseq; j++)
					brw->button[i].cmdseq[j] = NULL;
				btn_button_clear(brw->button + i);
			}
		}
		brw->width = width;
	}
}

/* 1999-05-01 -	Get the width of the given button row. Simpler than setting it. :) */
guint btn_buttonrow_get_width(ButtonRow *brw)
{
	if(brw != NULL)
		return brw->width;
	return 0U;
}

/* 1998-09-15 -	Destroy a row of buttons.
** 1999-01-05 -	Adjusted for the new allocation style. One free() does it all.
** 1999-05-12 -	One free() is certainly not enough.
*/
void btn_buttonrow_destroy(ButtonRow *brw)
{
	guint	i;

	if(brw != NULL)
	{
		if(brw->button != NULL)
		{
			for(i = 0; i < brw->width; i++)
				btn_button_destroy(brw->button + i);
			g_free(brw->button);
		}
		g_free(brw);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-15 -	Create a new empty buttonsheet labeled <label>. */
ButtonSheet * btn_buttonsheet_new(const gchar *label)
{
	ButtonSheet	*bsh;

	bsh = g_malloc(sizeof *bsh);
	if(label != NULL)
		g_strlcpy(bsh->label, label, sizeof bsh->label);
	bsh->height = 0;
	bsh->visible = 0;
	bsh->top_row = 0;
	bsh->rows = NULL;

	return bsh;	
}

/* 1998-09-15 -	Build the default button sheet, and return it.
** 1999-01-05 -	Fixed a *huge* bug, where the same row was added twice to the list!!
*/
static ButtonSheet * btn_buttonsheet_new_default_commands(MainInfo *min)
{
	ButtonSheet	*bsh;
	ButtonRow	*brw;

	bsh = g_malloc(sizeof *bsh);
	g_strlcpy(bsh->label, BTN_DEFAULT_SHEET, sizeof bsh->label);
	bsh->rows = NULL;
	if((brw = btn_buttonrow_new_default(min)) != NULL)
		btn_buttonsheet_append_row(bsh, brw);
	if((brw = btn_buttonrow_new(8)) != NULL)
		btn_buttonsheet_append_row(bsh, brw);
	if((brw = btn_buttonrow_new(8)) != NULL)
		btn_buttonsheet_append_row(bsh, brw);
	return bsh;
}

/* 2002-05-23 -	Build default shortcut buttonsheet. It's happening, slowly. */
static ButtonSheet * btn_buttonsheet_new_default_shortcuts(MainInfo *min)
{
	ButtonSheet	*bsh;
	ButtonRow	*brw;
	const gchar	*lab[] = { "Home", "Local", "/", "CD-ROM" },
			*pth[] = { "$HOME", "/usr/local/", "/", "/cdrom/" };
	gchar		buf[1024];
	guint		i;

	bsh = g_malloc(sizeof *bsh);
	g_strlcpy(bsh->label, "Shortcuts", sizeof bsh->label);
	bsh->rows = NULL;
	for(i = 0; i < sizeof lab / sizeof *lab; i++)
	{
		brw = btn_buttonrow_new(1);
		btn_button_set_label(brw->button, BTN_PRIMARY, lab[i]);
		g_snprintf(buf, sizeof buf, "DirEnter 'dir=%s'", pth[i]);
		btn_button_set_cmdseq(brw->button, BTN_PRIMARY, buf);
		btn_buttonsheet_append_row(bsh, brw);
	}
	return bsh;
}

/* 1998-09-15 -	Return a new, freshly allocated copy of <src>. The copy shares no memory with
**		the original (it is a "deep" copy).
** 1998-12-23 -	Rewritten slightly. Didn't use btn_buttonsheet_new(), which was silly.
*/
ButtonSheet * btn_buttonsheet_copy(ButtonSheet *src)
{
	ButtonSheet	*bsh;
	ButtonRow	*brw;
	GList		*iter;

	if((bsh = btn_buttonsheet_new(src->label)) != NULL)
	{
		for(iter = src->rows; iter != NULL; iter = g_list_next(iter))
		{
			if((brw = btn_buttonrow_copy(iter->data)) != NULL)
				btn_buttonsheet_append_row(bsh, brw);
		}
	}
	return bsh;
}

/* 1998-12-23 -	Append row <brw> to the sheet <bsh>. */
void btn_buttonsheet_append_row(ButtonSheet *bsh, ButtonRow *brw)
{
	if(bsh == NULL || brw == NULL)
		return;
	bsh->rows = g_list_append(bsh->rows, brw);
	bsh->height = g_list_length(bsh->rows);
}

/* 1998-09-15 -	Add a new row of buttons before <anchor> in <bsh>. If <anchor> is NULL, a new
**		row is created and appended. The new row will contain <width> buttons.
** 1998-12-23 -	Renamed from add_row to insert_row, since it confused me so much...
*/
void btn_buttonsheet_insert_row(ButtonSheet *bsh, ButtonRow *anchor, gint width)
{
	ButtonRow	*nr;
	GList		*link;
	gint		pos;

	if((nr = btn_buttonrow_new(width)) != NULL)
	{
		if((anchor != NULL) && (link = g_list_find(bsh->rows, anchor)) != NULL)
		{
			pos = g_list_position(bsh->rows, link);
			bsh->rows = g_list_insert(bsh->rows, nr, pos);
			bsh->height = g_list_length(bsh->rows);
		}
		else
			btn_buttonsheet_append_row(bsh, nr);
	}
}

/* 1998-09-15 -	Delete row <brw> from sheet <bsh>. Frees all resources occupied by it
**		(except for any previously returned widgets, of course).
*/
void btn_buttonsheet_delete_row(ButtonSheet *bsh, ButtonRow *brw)
{
	bsh->rows = g_list_remove(bsh->rows, brw);
	btn_buttonrow_destroy(brw);
	bsh->height = g_list_length(bsh->rows);
}

/* 1998-09-19 -	Move the row <brw> up (delta == -1) or down (delta == 1) in the sheet <bsh>.
**		The rows will "wrap" around at top and bottom of the sheet.
*/
void btn_buttonsheet_move_row(ButtonSheet *bsh, ButtonRow *brw, gint delta)
{
	GList	*link;
	gint	pos, np;

	link = g_list_find(bsh->rows, brw);
	pos = g_list_position(bsh->rows, link);
	bsh->rows = g_list_remove(bsh->rows, brw);
	np = pos + delta;
	if(np < 0)
		np = g_list_length(bsh->rows);
	else if(np > (gint) g_list_length(bsh->rows))
		np = 0;
	bsh->rows = g_list_insert(bsh->rows, brw, np);
}

/* 1999-05-18 -	Get height (number of rows) of a given sheet. */
guint btn_buttonsheet_get_height(ButtonSheet *bsh)
{
	if(bsh != NULL)
		return bsh->height;
	return 0U;
}

/* 1998-09-15 -	Return pointer to sheet labeled <label>. */
ButtonSheet * btn_buttonsheet_get(ButtonInfo *bti, const gchar *label)
{
	GList	*iter;

	if(label == NULL)
		label = BTN_DEFAULT_SHEET;
	for(iter = bti->sheets; iter != NULL; iter = g_list_next(iter))
	{
		if(strcmp(((ButtonSheet *) iter->data)->label, label) == 0)
			return iter->data;
	}
	return NULL;
}

/* 1998-09-15 -	Build GUI representation of given sheet. Note: the returned widget has not
**		ben gtk_widget_show()n.
** 1999-05-12 -	Now wraps the vbox holding the sheet's rows in a ODScrolledBox widget. Nice.
*/
static GtkWidget * buttonsheet_build(MainInfo *min, ButtonSheet *bsh, gboolean partial, GCallback func, gpointer user)
{
	GtkWidget	*scbox, *vbox, *row;
	GList		*iter;

	if(bsh->rows == NULL)
		return NULL;

	/* First, stack all rows in a vbox. */
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	for(iter = bsh->rows; iter != NULL; iter = g_list_next(iter))
	{
		if((row = buttonrow_build(min, iter->data, partial, func, user)) != NULL)
			gtk_box_pack_start(GTK_BOX(vbox), row, FALSE, FALSE, 0);
	}
	/* Next, wrap the Shortcuts in a scrolled window, since the default buttons decide the height. */
	if(strcmp(bsh->label, "Shortcuts") == 0)
	{
		scbox = gtk_scrolled_window_new(NULL, NULL);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scbox), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		gtk_container_add(GTK_CONTAINER(scbox), vbox);
		return scbox;
	}
	return vbox;
}

/* 1998-09-15 -	Build the GTK+ GUI representation of sheet named <label>.
** 1998-12-23 -	Added the <partial> argument so it's there when GTK+ 1.2.0 is released
**		(if I live to see it). It's going to rule.
*/
GtkWidget * btn_buttonsheet_build(MainInfo *min, ButtonInfo *bti, const gchar *label, gboolean partial, GCallback func, gpointer user)
{
	GtkWidget	*sheet = NULL;
	GList		*iter;

	if(label == NULL)
		label = BTN_DEFAULT_SHEET;
	if(func == NULL)
		func = G_CALLBACK(evt_button_clicked);
	if(user == NULL)
		user = min;
	for(iter = bti->sheets; iter != NULL; iter = g_list_next(iter))
	{
		if(strcmp(((ButtonSheet *) iter->data)->label, label) == 0)
		{
			sheet = buttonsheet_build(min, iter->data, partial, func, user);
			break;
		}
	}
	return sheet;
}

/* 1999-03-11 -	This gets called once for every widget in a row. If the widget is indeed a
**		button, everything is cool and we actually add the keyboard shortcut. If the
**		widget is a hbox, we need to recurse.
** 1999-05-04 -	Adapted for new multi-face buttons. Now lets the keyboard module directly
**		invoke the relevant command sequence, rather than going through an activation
**		event. Saves the trouble of extending the kbd module to support multi-argument
**		signals (which would be required for the activation event of ODEmilButton).
*/
static void btn_callback(GtkWidget *wid, gpointer user)
{
	Button		*btn;
	MainInfo	*min = user;

	if(OD_IS_MULTIBUTTON(wid) && ((btn = g_object_get_data(G_OBJECT(wid), "button")) != NULL))
	{
		const gchar	*key, *cmd;
		guint		i;

		for(i = 0; i < BTN_FACES; i++)
		{
			if(((key = btn_button_get_key(btn, i)) != NULL) && ((cmd = btn_button_get_cmdseq(btn, i)) != NULL))
				kbd_context_entry_add(min->gui->kbd_ctx, key, KET_CMDSEQ, cmd);
		}
	}
	else if(GTK_IS_BOX(wid))
		gtk_container_foreach(GTK_CONTAINER(wid), btn_callback, user);
}

/* 1999-03-11 -	This gets called once for every row in a sheet. */
static void row_callback(GtkWidget *wid, gpointer user)
{
	if(GTK_IS_BOX(wid))
		gtk_container_foreach(GTK_CONTAINER(wid), btn_callback, user);
	else
		fprintf(stderr, "**BUTTONS: Unexpected non-hbox widget found!\n");
}

/* 1999-03-11 -	Add key entries for buttons *from the GTK+ sheet*! This is kind of complex,
**		since the structure of the sheet is non-trivial.
** 2008-03-01 -	Basically, For the "Shortcuts" sheet, it's a scrolled box, containing a
**		viewport, containing a vbox full of hboxes, full of hboxes, full of buttons.
**		For "Default" sheet, it's just a vbox. Simple! 
*/
void btn_buttonsheet_built_add_keys(MainInfo *min, GtkContainer *sheet, gpointer user)
{
	if(user == NULL)
		user = min;
	else
		fprintf(stderr, "**BUTTONS: Can't have non-NULL user pointer for key!\n");

	/* Only need to dig out across the scrolled window for shortcuts, Default doesn't scroll. */
	if(GTK_IS_SCROLLED_WINDOW(sheet))
		sheet = GTK_CONTAINER(gtk_bin_get_child(GTK_BIN(gtk_bin_get_child(GTK_BIN(sheet)))));
	gtk_container_foreach(sheet, row_callback, user);	/* Visits each hbox. */
}

/* 1998-09-15 -	Destroy a sheetful of buttons. */
void btn_buttonsheet_destroy(ButtonSheet *bsh)
{
	GList	*iter;

	for(iter = bsh->rows; iter != NULL; iter = g_list_next(iter))
		btn_buttonrow_destroy(iter->data);
	g_list_free(bsh->rows);
	g_free(bsh);
}
