/*
** 1998-06-22 -	Redesigned the configuration. Now each page gets its own module, and this module also
**		contains the necessary data to add the page to the config-notebook. Much cleaner and
**		easier to extend, IMO.
** 1998-06-24 -	Implemented the rest of the GUI. This module is now the single biggest in the project!
**		GUI code has a way to grow unbounded, and I don't think gtk is any different from other
**		toolkits/APIs I've seen in, that respect. The results are, though.
** 1998-06-25 -	Um... I forgot about sorting, and it also occured to me that having a default path would
**		be useful. So I added those.
** 1998-06-30 -	Added a check button for selecting how sorting should treat case
** 1998-08-10 -	Finally added some numeric feedback for the column width config.
** 1998-08-13 -	Replaced button click signal with row selection for the 'sclist', made things a lot simpler.
**		Now changes sensitivity of edit/remove/up/down buttons, too.
** 1999-03-05 -	Added the control mode setting, for the new selection design.
** 1999-05-15 -	Rewrote large parts of this file in order to make the two panes have separate GUIs (notebook
**		pages), and make room for some color config. Also moved code out into the dpformat module.
** 2000-07-02 -	Initialized translation, by marking up translatable strings using _() and N().
** 2009-04-25 -	Rewritten to no longer use GtkCList, since we're playing with the GTK+ 2.0 crowd now.
*/

#include "gentoo.h"

#include <stdlib.h>

#include "configure.h"
#include "dirpane.h"
#include "guiutil.h"
#include "xmlutil.h"
#include "cmdseq.h"
#include "nag_dialog.h"
#include "strutil.h"
#include "dpformat.h"
#include "color_dialog.h"
#include "odmultibutton.h"
#include "window.h"

#include "configure.h"
#include "cfg_module.h"

#include "cfg_dirpane.h"

#define	NODE	"DirPanes"

/* ----------------------------------------------------------------------------------------- */

typedef struct P_DirPane	P_DirPane;

typedef struct {			/* Pane definition widgets. */
	GtkWidget	*vbox;
	GtkListStore	*astore;	/* Available column types. */
	GtkWidget	*aview;		/* Tree view showing available content types. */

	GtkListStore	*sstore;	/* Selected column types. */
	GtkWidget	*sview;		/* Tree view for selected content types. */
	GtkWidget	*sbutton[4];	/* The fun buttons themselves. */

	GtkWidget	*scmenu;	/* Content option menu. */
	GtkWidget	*smmenu;	/* Sorting mode (directory mixing) option menu. */
	GtkWidget	*sinvert;	/* Check button for inverse sorting. */
	GtkWidget	*snocase;	/* Check button for case ignorance. */

	GtkWidget	*dentry;	/* An entry for the default directory. */

	GtkWidget	*fabove;	/* Check button to put path above pane. */
	GtkWidget	*fhide;		/* Check button to allow hiding. */
	GtkWidget	*fscroll;	/* Check button for scrollbar always. */
	GtkWidget	*fhparent;	/* Check button for huge parent button. */
	GtkWidget	*ffont;		/* Check button for font overriding. */
	GtkWidget	*fontbutton;	/* Font button for picking font. */
	GtkWidget	*frband;	/* Check button for rubber banding. */
	GtkWidget	*fruled;	/* Check button for GTK+ ruling of tree view. */

	GSList		*splist;
	GtkWidget	*spos[3];	/* Radio buttons for scrollbar position. */

	GSList		*mclist;
					/* visible ("clear" is kinda pointless, but there for completeness). */
	P_DirPane	*page;
	DPFormat	edit;
	guint		index;
	gint		srow;		/* Last clicked row in selected content list. */
	MainInfo	*min;		/* Handy to have around. */
} PDef;

enum {
	CONTENT_COLUMN_TITLE,
	CONTENT_COLUMN_DESCRIPTION,
	CONTENT_COLUMN_CONTENT,

	CONTENT_COLUMN_COUNT
};

typedef struct {
	GtkWidget	*vbox;
	GtkWidget	*orientation[2];
	GtkWidget	*mode[4];
	GtkWidget	*value;		/* Pointer to current value-holding widget. */
	DPPaning	edit;
} PPaning;

typedef struct {
	GtkWidget	*vbox;
	GtkWidget	*select;
	GtkWidget	*save;
	DPHistory	edit;
} PHistory;

typedef struct {
	GtkWidget	*show_type;		/* A checkbox. */
	GtkWidget	*show_linkname;		/* Another checkbox. */
} EE_Name;

typedef struct {
	GtkWidget	*options;
	SzUnit		unit;
	GtkWidget	*ticks;
	GtkWidget	*tick;
	GtkWidget	*digits;
	GtkWidget	*dir_show_fs_size;	/* A checkbox. */
	GtkWidget	*format;		/* KILL ME! */
} EE_Size;

typedef struct {
	GtkWidget	*format;		/* An entry. */
} EE_Format;

typedef struct {
	GtkWidget	*format;		/* An entry. */
} EE_Date;

typedef enum { ET_NONE = 0, ET_NAME, ET_SIZE, ET_FORMAT, ET_DATE } ExtraType;

typedef struct {		/* Holds data used when editing a column format. */
	PDef		*def;
	DPContent	content;	/* Type of content we're editing. */
	GtkWidget	*title;		/* Entry widget holding the wanted title. */
	GtkWidget	*just;		/* Justification option menu. */
	guint		curr_just;	/* Current justification (0=left, 1=right, 2=center). */
	GtkAdjustment	*wadj;		/* An adjustment for the width. */
	GtkWidget	*width;		/* A spin button for the width. */
	union {
	EE_Name		name;
	EE_Size		size;		/* Size has its own formatting. */
	EE_Format	format;		/* General integer format, used by protection, IDs, etc. */
	EE_Date		date;
	} extra;
	ExtraType	extype;
} P_Edit;

struct P_DirPane {		/* Information this module likes to have around. */
	/* No vbox here. */
	GtkWidget	*high_hbox;	/* Upper hbox, holds mostly buttons. */

	GtkWidget	*notebook;	/* A notebook holding left, right and color pages. */

	PDef		pane[2];
	PPaning		paning;
	PHistory	history;

	MainInfo	*min;
};

static P_DirPane	the_page;	/* There can be only one. */

static const gchar	*config_name[] = { "DirPaneLeft", "DirPaneRight" },
			*mode_name[] = { "dirs_first", "dirs_last", "dirs_mixed" },
			*sbarpos_name[] = { "system", "left", "right" },
			*pane_orient_name[] = { "horizontal", "vertical" },
			*pane_splitmode_name[] = { "free", "ratio", "absleft", "absright" };

/* ----------------------------------------------------------------------------------------- */

static void	evt_sort_set_content(GtkWidget *wid, gpointer user);

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-15 -	Set the state of the various widgets of <def> according to its 'edit' field. */
static void set_pane_widgets(PDef *def)
{
	guint		i;
	GtkTreeIter	iter;

	gtk_list_store_clear(def->sstore);
	for(i = 0; i < def->edit.num_columns; i++)
	{
		gtk_list_store_insert_with_values(def->sstore, &iter, -1,
				CONTENT_COLUMN_TITLE, def->edit.format[i].title,
				CONTENT_COLUMN_DESCRIPTION, dpf_get_content_name(def->edit.format[i].content),
				CONTENT_COLUMN_CONTENT, def->edit.format[i].content,
				-1);
	}
	for(i = 0; i < sizeof def->sbutton / sizeof def->sbutton[0]; i++)
		gtk_widget_set_sensitive(def->sbutton[i], FALSE);

	gtk_combo_box_set_active(GTK_COMBO_BOX(def->scmenu), def->edit.sort.content);
	gtk_combo_box_set_active(GTK_COMBO_BOX(def->smmenu), (guint) def->edit.sort.mode);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(def->sinvert), def->edit.sort.invert);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(def->snocase), def->edit.sort.nocase);

	gtk_entry_set_text(GTK_ENTRY(def->dentry), def->edit.def_path);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(def->fabove),   def->edit.path_above);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(def->fhide),    def->edit.hide_allowed);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(def->fscroll),  def->edit.scrollbar_always);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(def->fhparent), def->edit.huge_parent);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(def->ffont),    def->edit.set_font);
	if(def->edit.font_name[0] != '\0')
		gtk_font_button_set_font_name(GTK_FONT_BUTTON(def->fontbutton), def->edit.font_name);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(def->frband), def->edit.rubber_banding);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(def->spos[(guint)  def->edit.sbar_pos]), TRUE);

	def->srow = -1;
}

static void set_paning_widgets(PPaning *p)
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->orientation[p->edit.orientation]), TRUE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->mode[p->edit.mode]), TRUE);
	switch(p->edit.mode)
	{
		case DPSPLIT_FREE:
			break;
		case DPSPLIT_RATIO:
			gtk_adjustment_set_value(gtk_range_get_adjustment(GTK_RANGE(p->value)), p->edit.value);
			break;
		case DPSPLIT_ABS_LEFT:
		case DPSPLIT_ABS_RIGHT:
			{
				gchar	buf[32];

				g_snprintf(buf, sizeof buf, "%d", (gint) p->edit.value);
				gtk_entry_set_text(GTK_ENTRY(p->value), buf);
			}
			break;
	}
}

static void set_history_widgets(PHistory *p)
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->select), p->edit.select);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->save), p->edit.save);
}

/* ----------------------------------------------------------------------------------------- */

/* 2009-04-24 -	An available-content row was activated, i.e. double-clicked. Add it to the selected list. */
static void evt_aview_row_activated(GtkWidget *wid, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user)
{
	PDef		*def = user;
	GtkTreeIter	iter;

	if(def->edit.num_columns >= DP_MAX_COLUMNS)
		return;

	if(gtk_tree_model_get_iter(GTK_TREE_MODEL(def->astore), &iter, path))
	{
		gint		content;
		GtkTreePath	*sp;

		gtk_tree_model_get(GTK_TREE_MODEL(def->astore), &iter, CONTENT_COLUMN_CONTENT, &content, -1);
		dpf_set_default_format(&def->edit.format[def->edit.num_columns], (DPContent) content);
		gtk_list_store_insert_with_values(def->sstore, &iter, -1,
				CONTENT_COLUMN_TITLE, def->edit.format[def->edit.num_columns].title,
				CONTENT_COLUMN_DESCRIPTION, dpf_get_content_name((DPContent) content),
				CONTENT_COLUMN_CONTENT, content,
				-1);
		def->edit.num_columns++;
		/* Select and scroll-to. */
		gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(def->sview)), &iter);
		sp = gtk_tree_model_get_path(GTK_TREE_MODEL(def->sstore), &iter);
		gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(def->sview), sp, NULL, TRUE, 0.5f, 0.0f);
		gtk_tree_path_free(sp);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-06-24 -	Build the widgets needed to get a content format string. This consists of
**		a table with a label showing <ltext> and an entry widget containing
**		<dtext> initially. The created entry widget is stored at <wid>, and the
**		entire shabang is packed into the <vbox> given.
*/
static void build_content_format(GtkWidget *vbox, GtkWidget **wid, const gchar *ltext, const gchar *dtext)
{
	GtkWidget	*grid, *label;

	grid = gtk_grid_new();
	label = gtk_label_new(ltext);
	*wid = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(*wid), DP_DATEFMT_SIZE - 1);
	gtk_entry_set_text(GTK_ENTRY(*wid), dtext);
	gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), *wid,  1, 0, 1, 1);
	gtk_box_pack_start(GTK_BOX(vbox), grid, TRUE, TRUE, 0);
}

static void evt_size_type_toggled(GtkWidget *wid, gpointer user)
{
	P_Edit	*edit = user;

	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)))
	{
		const SzUnit	unit = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "type"));

		gtk_widget_set_sensitive(edit->extra.size.options, unit != SZE_NONE);
		edit->extra.size.unit = unit;
	}
}

static void evt_size_ticks_toggled(GtkWidget *wid, gpointer user)
{
	P_Edit	*edit = user;

	gtk_widget_set_sensitive(edit->extra.size.tick, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)));
}

/* 1998-06-23 -	Add widgetry to dialog being defined, if any is needed. */
static GtkWidget * add_content_widgets(P_Edit *edit)
{
	gchar		*tmp = "", tickstr[2] = "?";
	DpCFmt		*fmt = &edit->def->edit.format[edit->def->srow];
	GtkAdjustment	*adj;
	GtkWidget	*vbox, *frame, *grid, *label, *rb, *trb[SZE_NUM_UNITS], *sep;
	ExtraType	type = ET_NONE;
	GSList		*group;
	guint		i, ti = 0;

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	switch(edit->content)
	{
		case DPC_NAME:
			edit->extra.name.show_type = gtk_check_button_new_with_label(_("Append Type Character?"));
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(edit->extra.name.show_type), fmt->extra.name.show_type);
			gtk_box_pack_start(GTK_BOX(vbox), edit->extra.name.show_type, TRUE, TRUE, 0);
			edit->extra.name.show_linkname = gtk_check_button_new_with_label(_("Append \"\xe2\x86\x92 destination\" on Links?"));
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(edit->extra.name.show_linkname), fmt->extra.name.show_linkname);
			gtk_box_pack_start(GTK_BOX(vbox), edit->extra.name.show_linkname, TRUE, TRUE, 0);
			type = ET_NAME;
			break;
		case DPC_SIZE:
			grid = gtk_grid_new();
			for(i = 0, group = NULL; i < SZE_NUM_UNITS; i++)
			{
				const SzUnit	unit = (SzUnit) i;

				trb[i] = rb = gtk_radio_button_new_with_label(group, _(sze_get_unit_label(unit)));
				g_object_set_data(G_OBJECT(rb), "type", GINT_TO_POINTER(unit));
				g_signal_connect(G_OBJECT(rb), "toggled", G_CALLBACK(evt_size_type_toggled), edit);
				gtk_grid_attach(GTK_GRID(grid), rb, 0, i, 2, 1);
				if(fmt->extra.size.unit == unit)
					ti = i;
				group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(rb));
			}
			gtk_box_pack_start(GTK_BOX(vbox), grid, TRUE, TRUE, 0);

			edit->extra.size.options = gtk_grid_new();
			edit->extra.size.ticks = gtk_check_button_new_with_label(_("Place Tick Every 3 Digits?"));
			gtk_grid_attach(GTK_GRID(edit->extra.size.options), edit->extra.size.ticks, 0, 0, 1, 1);
			edit->extra.size.tick = gtk_entry_new();
			gtk_entry_set_max_length(GTK_ENTRY(edit->extra.size.tick), 1);
			gtk_entry_set_width_chars(GTK_ENTRY(edit->extra.size.tick), 1);
			tickstr[0] = fmt->extra.size.tick;
			gtk_entry_set_text(GTK_ENTRY(edit->extra.size.tick), tickstr);
			gtk_grid_attach(GTK_GRID(edit->extra.size.options), edit->extra.size.tick, 1, 0, 1, 1);
			g_signal_connect(G_OBJECT(edit->extra.size.ticks), "toggled", G_CALLBACK(evt_size_ticks_toggled), edit);
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(edit->extra.size.ticks), fmt->extra.size.ticks);

			label = gtk_label_new(_("Digits of Precision"));
			gtk_grid_attach(GTK_GRID(edit->extra.size.options), label, 0, 1, 1, 1);
			adj = gtk_adjustment_new(0.0, 0.0, 6.0, 1.0, 1.0, 0.0);
			edit->extra.size.digits = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1.0, 0);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(edit->extra.size.digits), fmt->extra.size.digits);
			gtk_grid_attach(GTK_GRID(edit->extra.size.options), edit->extra.size.digits, 1, 1, 1, 1);
			gtk_box_pack_start(GTK_BOX(vbox), edit->extra.size.options, TRUE, TRUE, 0);

			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(trb[ti]), TRUE);
			gtk_widget_set_sensitive(edit->extra.size.options, ti != 0);

			sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
			gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);
			edit->extra.size.dir_show_fs_size = gtk_check_button_new_with_label(_("Show Dir's File System Size?"));
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(edit->extra.size.dir_show_fs_size), fmt->extra.size.dir_show_fs_size);
			gtk_box_pack_start(GTK_BOX(vbox), edit->extra.size.dir_show_fs_size, FALSE, FALSE, 0);
			type = ET_SIZE;
			break;
		case DPC_BLOCKS:
			build_content_format(vbox, &edit->extra.format.format, _("Format"), fmt->extra.blocks.format);
			type = ET_FORMAT;
			break;
		case DPC_BLOCKSIZE:
			build_content_format(vbox, &edit->extra.format.format, _("Format"), fmt->extra.blocksize.format);
			type = ET_FORMAT;
			break;
		case DPC_MODENUM:
			build_content_format(vbox, &edit->extra.format.format, _("Format"), fmt->extra.mode.format);
			type = ET_FORMAT;
			break;
		case DPC_NLINK:
			build_content_format(vbox, &edit->extra.format.format, _("Format"), fmt->extra.nlink.format);
			type = ET_FORMAT;
			break;
		case DPC_UIDNUM:
			build_content_format(vbox, &edit->extra.format.format, _("Format"), fmt->extra.uidnum.format);
			type = ET_FORMAT;
			break;
		case DPC_GIDNUM:
			build_content_format(vbox, &edit->extra.format.format, _("Format"), fmt->extra.gidnum.format);
			type = ET_FORMAT;
			break;
		case DPC_DEVICE:
			build_content_format(vbox, &edit->extra.format.format, _("Format"), fmt->extra.device.format);
			type = ET_FORMAT;
			break;
		case DPC_DEVMAJ:
			build_content_format(vbox, &edit->extra.format.format, _("Format"), fmt->extra.devmaj.format);
			type = ET_FORMAT;
			break;
		case DPC_DEVMIN:
			build_content_format(vbox, &edit->extra.format.format, _("Format"), fmt->extra.devmin.format);
			type = ET_FORMAT;
			break;
		case DPC_ATIME:
		case DPC_MTIME:
		case DPC_CHTIME:
		case DPC_CRTIME:
			grid = gtk_grid_new();
			label = gtk_label_new(_("Format"));
			edit->extra.date.format = gtk_entry_new();
			gtk_widget_set_hexpand(edit->extra.date.format, TRUE);
			gtk_widget_set_halign(edit->extra.date.format, GTK_ALIGN_FILL);
			gtk_entry_set_max_length(GTK_ENTRY(edit->extra.date.format), DP_DATEFMT_SIZE - 1);
			if(edit->content == DPC_ATIME)
				tmp = fmt->extra.a_time.format;
			else if(edit->content == DPC_MTIME)
				tmp = fmt->extra.m_time.format;
			else if(edit->content == DPC_CHTIME)
				tmp = fmt->extra.ch_time.format;
			else if(edit->content == DPC_CRTIME)
				tmp = fmt->extra.cr_time.format;
			gtk_entry_set_text(GTK_ENTRY(edit->extra.date.format), tmp);
			gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
			gtk_grid_attach(GTK_GRID(grid), edit->extra.date.format, 1, 0, 1, 1);
			gtk_box_pack_start(GTK_BOX(vbox), grid, TRUE, TRUE, 0);
			type = ET_DATE;
		case DPC_TYPE:
			break;
		default:
			;
	}
	edit->extype = type;
	if(type != ET_NONE)
	{
		gchar	text[64];

		g_snprintf(text, sizeof text, _("%s Settings"), dpf_get_content_name(edit->content));
		frame = gtk_frame_new(text);
		gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
		gtk_container_add(GTK_CONTAINER(frame), vbox);
		gtk_widget_show_all(vbox);
		return frame;
	}
	gtk_widget_destroy(vbox);
	return NULL;
}

/* 1998-06-24 -	This gets called when the user selects a new justification for the
**		column being edited. We better remember exactly which mode that is.
*/
static gint evt_edit_justification(GtkWidget *wid, guint index, gpointer user)
{
	P_Edit	*edit = user;

	edit->curr_just = index;

	return TRUE;
}

/* 2002-07-18 -	This used to be a button event handler. Now remade to just store accepted values. */
static void accept_format(P_Edit *edit)
{
	const gchar	*nfmt = "";
	DpCFmt		*fmt = &edit->def->edit.format[edit->def->srow];
	GtkTreeIter	iter;

	strcpy(fmt->title, gtk_entry_get_text(GTK_ENTRY(edit->title)));
	fmt->just = edit->curr_just;
	fmt->width = (gint) gtk_adjustment_get_value(edit->wadj);
	if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(edit->def->sview)), NULL, &iter))
		gtk_list_store_set(edit->def->sstore, &iter, CONTENT_COLUMN_TITLE, fmt->title, -1);

	/* If the content uses the general numerical formatting entry, get its text. */
	if(edit->extype == ET_FORMAT)
		nfmt = gtk_entry_get_text(GTK_ENTRY(edit->extra.format.format));

	switch(edit->content)
	{
		case DPC_NAME:
			fmt->extra.name.show_type = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(edit->extra.name.show_type));
			fmt->extra.name.show_linkname = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(edit->extra.name.show_linkname));
			break;
		case DPC_SIZE:
			fmt->extra.size.unit = edit->extra.size.unit;
			fmt->extra.size.digits = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(edit->extra.size.digits));
			g_snprintf(fmt->extra.size.dformat, sizeof fmt->extra.size.dformat, "%%#.%uf", fmt->extra.size.digits);
			fmt->extra.size.ticks = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(edit->extra.size.ticks));
			fmt->extra.size.tick  = gtk_entry_get_text(GTK_ENTRY(edit->extra.size.tick))[0];
			if(fmt->extra.size.tick == '\0')
				fmt->extra.size.tick = ',';
			fmt->extra.size.dir_show_fs_size = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(edit->extra.size.dir_show_fs_size));
			break;
		case DPC_BLOCKS:
			strcpy(fmt->extra.blocks.format, nfmt);
			break;
		case DPC_BLOCKSIZE:
			strcpy(fmt->extra.blocksize.format, nfmt);
			break;
		case DPC_MODENUM:
			strcpy(fmt->extra.mode.format, nfmt);
			break;
		case DPC_NLINK:
			strcpy(fmt->extra.nlink.format, nfmt);
			break;
		case DPC_UIDNUM:
			strcpy(fmt->extra.uidnum.format, nfmt);
			break;
		case DPC_GIDNUM:
			strcpy(fmt->extra.gidnum.format, nfmt);
			break;
		case DPC_DEVICE:
			strcpy(fmt->extra.device.format, nfmt);
			break;
		case DPC_DEVMIN:
			strcpy(fmt->extra.devmin.format, nfmt);
			break;
		case DPC_DEVMAJ:
			strcpy(fmt->extra.devmaj.format, nfmt);
			break;
		case DPC_ATIME:
			strcpy(fmt->extra.a_time.format, gtk_entry_get_text(GTK_ENTRY(edit->extra.date.format)));
			break;
		case DPC_MTIME:
			strcpy(fmt->extra.m_time.format, gtk_entry_get_text(GTK_ENTRY(edit->extra.date.format)));
			break;
		case DPC_CRTIME:
			strcpy(fmt->extra.cr_time.format, gtk_entry_get_text(GTK_ENTRY(edit->extra.date.format)));
			break;
		case DPC_CHTIME:
			strcpy(fmt->extra.ch_time.format, gtk_entry_get_text(GTK_ENTRY(edit->extra.date.format)));
			break;
		case DPC_TYPE:
			break;
		default:
			;
	}
}

/* 1998-06-23 -	Do the complex thing, and let the user edit the details of the selected
**		selected (!) row.
** 1998-06-24 -	Redesigned the gimp way, i.e. without checking for success of GTK calls.
**		I really don't like it, but it saves a bunch of indents. I will not ever
**		take this as a hint that I should reduce my indent size... :^)
**		The dialog created by this function has a very simple structure: the top
**		area (the vbox) contains two frames. The top frame holds settings that
**		are available for all content types (type, name, width, justification).
**		The bottom frame (which may be missing) contains content-specific settings.
*/
static gint evt_sview_edit_clicked(GtkWidget *wid, gpointer user)
{
	PDef		*def = user;
	DpCFmt		*fmt = &def->edit.format[def->srow];
	GtkWidget	*vbox, *frame1, *frame2, *grid, *label;
	const gchar	*lab[]   = { N_("Content"), N_("Title"), N_("Justification"), N_("Width") },
			*jtext[] = { N_("Left"), N_("Right"), N_("Center"), NULL };
	static P_Edit	edit;
	Dialog		*dlg;
	gint		i;

	edit.def     = def;
	edit.content = fmt->content;

	vbox   = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	frame1 = gtk_frame_new(_("Basic Settings"));

	grid = gtk_grid_new();
	for(i = 0; i < sizeof lab / sizeof lab[0]; i++)
	{
		label = gtk_label_new(_(lab[i]));
		gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
	}
	label = gtk_label_new(dpf_get_content_name(edit.content));
	gtk_widget_set_hexpand(label, TRUE);
	gtk_widget_set_halign(label, GTK_ALIGN_FILL);
	gtk_grid_attach(GTK_GRID(grid), label, 1, 0, 1, 1);

	edit.title = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(edit.title), DP_TITLE_SIZE - 1);
	gtk_entry_set_text(GTK_ENTRY(edit.title), fmt->title);
	gtk_grid_attach(GTK_GRID(grid), edit.title, 1, 1, 1, 1);

	edit.just = gui_build_combo_box(jtext, G_CALLBACK(evt_edit_justification), &edit);
	switch(fmt->just)
	{
		case GTK_JUSTIFY_LEFT:
		case GTK_JUSTIFY_FILL:
			edit.curr_just = 0;
			break;
		case GTK_JUSTIFY_RIGHT:
			edit.curr_just = 1;
			break;
		case GTK_JUSTIFY_CENTER:
			edit.curr_just = 2;
			break;
	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(edit.just), edit.curr_just);
	gtk_grid_attach(GTK_GRID(grid), edit.just, 1, 2, 1, 1);

	edit.wadj  = gtk_adjustment_new(fmt->width, 10.0, 800.0, 1.0f, 50.0, 0.0);
	edit.width = gtk_spin_button_new(GTK_ADJUSTMENT(edit.wadj), 0, 0);
	gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(edit.width), FALSE);
	gtk_grid_attach(GTK_GRID(grid), edit.width, 1, 3, 1, 1);

	gtk_container_set_border_width(GTK_CONTAINER(frame1), 5);
	gtk_container_add(GTK_CONTAINER(frame1), grid);

	gtk_box_pack_start(GTK_BOX(vbox), frame1, TRUE, TRUE, 0);

	if((frame2 = add_content_widgets(&edit)) != NULL)
		gtk_box_pack_start(GTK_BOX(vbox), frame2, TRUE, TRUE, 0);
	dlg = dlg_dialog_sync_new(vbox, _("Edit Column Content"), NULL);
	if(dlg_dialog_sync_wait(dlg) == DLG_POSITIVE)
		accept_format(&edit);
	dlg_dialog_sync_destroy(dlg);

	return TRUE;
}

/* 2010-10-02 -	The list of selected content types had a row double-clicked. */
static void evt_sview_row_activated(GtkWidget *wid, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user)
{
	/* Just pretend like there was a click on the Edit button, instead. */
	evt_sview_edit_clicked(wid, user);
}

/* ----------------------------------------------------------------------------------------- */

/* 2009-04-24 -	Selected content selection changed, update the editing buttons. This is a bit irky. */
static void evt_sview_selection_changed(GtkTreeSelection *sel, gpointer user)
{
	PDef		*def = user;
	GtkTreeIter	iter;

	if(gtk_tree_selection_get_selected(sel, NULL, &iter))
	{
		GtkTreePath	*path;

		gtk_widget_set_sensitive(def->sbutton[0], TRUE);
		gtk_widget_set_sensitive(def->sbutton[1], TRUE);
		/* We need to dynamically create the GtkTreePath, to get a row index. */
		if((path = gtk_tree_model_get_path(GTK_TREE_MODEL(def->sstore), &iter)) != NULL)
		{
			const gint	*idx = gtk_tree_path_get_indices(path);

			def->srow = *idx;
			gtk_widget_set_sensitive(def->sbutton[2], def->srow > 0);
			gtk_widget_set_sensitive(def->sbutton[3], def->srow < def->edit.num_columns - 1);
			gtk_tree_path_free(path);
		}
	}
	else
	{
		gtk_widget_set_sensitive(def->sbutton[0], FALSE);
		gtk_widget_set_sensitive(def->sbutton[1], FALSE);
		gtk_widget_set_sensitive(def->sbutton[2], FALSE);
		gtk_widget_set_sensitive(def->sbutton[3], FALSE);
	}
}

#if 0
/* 2000-02-29 -	Weird leap day! :) Reorder selected content by direct dragging. */
static void evt_sclist_move_row(GtkWidget *wid, gint arg1, gint arg2, gpointer user)
{
	PDef	*def = user;
	DpCFmt	tmp;
	guint	i;

	/* GTK+ seems not to report this case, but because the code below wouldn't
	** handle it, it seems nice to really make sure. Call me a coward.
	*/
	if(arg1 == arg2)
		return;

	tmp = def->edit.format[arg1];

	if(arg1 > arg2)		/* Drag upwards? */
	{
		for(i = arg1; i > arg2; i--)
			def->edit.format[i] = def->edit.format[i - 1];
	}
	else
	{
		for(i = arg1; i < arg2; i++)
			def->edit.format[i] = def->edit.format[i + 1];
	}
	def->edit.format[arg2] = tmp;
}
#endif

/* 1999-05-15 -	User clicked the "Remove" button below selected column list, so kill a column. */
static void evt_sview_remove_clicked(GtkWidget *wid, gpointer user)
{
	PDef		*def = user;
	GtkTreeIter	iter;

	if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(def->sview)), NULL, &iter))
	{
		guint	i;

		for(i = def->srow; i < def->edit.num_columns - 1; i++)
			def->edit.format[i] = def->edit.format[i + 1];
		def->edit.num_columns--;
		gtk_list_store_remove(def->sstore, &iter);
	}
}

/* 1999-05-15 -	The "up" arrow has been hit. Move the currently selected column.
** 2009-04-24 -	Pretty much all rewritten, porting to GtkTreeView and friends.
*/
static void evt_sview_up_clicked(GtkWidget *wid, gpointer user)
{
	PDef		*def = user;
	gint		row = def->srow;
	GtkTreeIter	iter, prev;

	if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(def->sview)), NULL, &iter))
	{
		gchar	pstr[8];

		g_snprintf(pstr, sizeof pstr, "%d", row - 1);
		if(gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(def->sstore), &prev, pstr))
		{
			DpCFmt	tmp;

			gtk_list_store_swap(def->sstore, &iter, &prev);
			gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(def->sview)));
			gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(def->sview)), &iter);
			/* We need to update the real model, too ... Sigh. */
			tmp = def->edit.format[row - 1];
			def->edit.format[row - 1] = def->edit.format[row];
			def->edit.format[row] = tmp;
		}
	}
}

/* 1999-05-15 -	The "down" arrow has been hit. Move the currently selected column.
** 2009-04-24 -	Pretty much all rewritten, porting to GtkTreeView and friends.
*/
static void evt_sview_down_clicked(GtkWidget *wid, gpointer user)
{
	PDef		*def = user;
	gint		row = def->srow;
	GtkTreeIter	iter, next;

	if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(def->sview)), NULL, &iter))
	{
		gchar	pstr[8];

		g_snprintf(pstr, sizeof pstr, "%d", row + 1);
		if(gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(def->sstore), &next, pstr))
		{
			DpCFmt	tmp;

			gtk_list_store_swap(def->sstore, &iter, &next);
			gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(def->sview)));
			gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(def->sview)), &iter);
			/* We need to update the real model, too ... Sigh. */
			tmp = def->edit.format[row + 1];
			def->edit.format[row + 1] = def->edit.format[row];
			def->edit.format[row] = tmp;
		}
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-15 -	Set new sorting column, from option menu selection. */
static void evt_sort_set_content(GtkWidget *wid, gpointer user)
{
	PDef	*def = user;

	def->edit.sort.content = (DPContent) gtk_combo_box_get_active(GTK_COMBO_BOX(wid));
}

/* 1999-05-15 -	Set new sorting mode, from option menu selection. */
static void evt_sort_set_mode(GtkWidget *wid, guint index, gpointer user)
{
	PDef	*def = user;

	def->edit.sort.mode = (SortMode) index;
}

/* 1999-05-15 -	User hit "Inverse Sort?" toggle button, so update. */
static void evt_sort_invert_clicked(GtkWidget *wid, gpointer user)
{
	PDef	*def = user;

	def->edit.sort.invert = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));
}

/* 1999-05-15 -	User hit the case-insensitivity toggle. */
static void evt_sort_nocase_clicked(GtkWidget *wid, gpointer user)
{
	PDef	*def = user;

	def->edit.sort.nocase = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-15 -	User is editing the default path, so update in editing copy. */
static void evt_default_path_changed(GtkWidget *wid, gpointer user)
{
	PDef	*def = user;

	g_strlcpy(def->edit.def_path, gtk_entry_get_text(GTK_ENTRY(wid)), sizeof def->edit.def_path);
}

/* 2012-05-27 - Set default path to "current directory". */
static void evt_default_path_current(GtkWidget *wid, gpointer user)
{
	PDef    *def = user;

	gtk_entry_set_text(GTK_ENTRY(def->dentry), "@cwd");
}

/* 1999-05-15 -	User just hit the "Grab Current" button for the default path. Do it. */
static void evt_default_path_grab(GtkWidget *wid, gpointer user)
{
	PDef		*def = user;
	P_DirPane	*page;

	page = def->page;

	gtk_entry_set_text(GTK_ENTRY(def->dentry), page->min->gui->pane[def->index].dir.path);
}

/* 2004-02-25 - Set default path to "take from history". Not totally beatiful, but still. */
static void evt_default_path_history(GtkWidget *wid, gpointer user)
{
	PDef    *def = user;

	gtk_entry_set_text(GTK_ENTRY(def->dentry), "@history[0]");
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-15 -	Set state of "path above" flag. */
static void evt_flag_above_clicked(GtkWidget *wid, gpointer user)
{
	PDef	*def = user;

	def->edit.path_above = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));
}

/* 1999-05-15 -	Set state of "hide allowed" flag. */
static void evt_flag_hide_clicked(GtkWidget *wid, gpointer user)
{
	PDef	*def = user;

	def->edit.hide_allowed = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));
}

/* 1999-05-15 -	Set state of "scrollbar always" flag. */
static void evt_flag_scrollbar_clicked(GtkWidget *wid, gpointer user)
{
	PDef	*def = user;

	def->edit.scrollbar_always = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));
}

/* 2000-02-03 -	User toggled "huge parent" flag. */
static void evt_flag_hugeparent_clicked(GtkWidget *wid, gpointer user)
{
	PDef	*def = user;

	def->edit.huge_parent = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));
}

/* 2010-10-24 -	The "set font" flag was toggled. */
static void evt_flag_setfont_clicked(GtkWidget *wid, gpointer user)
{
	PDef	*def = user;

	def->edit.set_font = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));
	gtk_widget_set_sensitive(def->fontbutton, def->edit.set_font);
}

/* 2010-10-24 -	The font button had its font changed, so let's remember that. */
static void evt_font_set(GtkWidget *wid, gpointer user)
{
	PDef	*def = user;

	g_snprintf(def->edit.font_name, sizeof def->edit.font_name, "%s", gtk_font_button_get_font_name(GTK_FONT_BUTTON(def->fontbutton)));
}

static void evt_flag_rubberbanding_clicked(GtkWidget *wid, gpointer user)
{
	PDef	*def = user;

	def->edit.rubber_banding = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-15 -	User clicked on one of the "scrollbar position" widgets. Update. */
static void evt_scrollbarpos_clicked(GtkWidget *wid, gpointer user)
{
	PDef	*def = user;

	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)))
		def->edit.sbar_pos = (SBarPos) GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(wid), "user"));
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-16 -	Copy this pane's definition to the other one. */
static void evt_copyto_clicked(GtkWidget *wid, gpointer user)
{
	PDef	*def = user;

	def->page->pane[1 - def->index].edit = def->edit;
	set_pane_widgets(&def->page->pane[1 - def->index]);
}

/* 1999-05-16 -	Copy the other pane's definition to this pane. */
static void evt_copyfrom_clicked(GtkWidget *wid, gpointer user)
{
	PDef	*def = user;

	def->edit = def->page->pane[1 - def->index].edit;
	set_pane_widgets(def);
}

/* 1999-05-16 -	Swap the two pane definitions. */
static void evt_swap_clicked(GtkWidget *wid, gpointer user)
{
	PDef		*def = user;
	DPFormat	tmp;

	tmp = def->edit;
	def->edit = def->page->pane[1 - def->index].edit;
	def->page->pane[1 - def->index].edit = tmp;
	set_pane_widgets(def);
}

/* ----------------------------------------------------------------------------------------- */

/* 2009-04-24 -	Create a list store suitable for dirpane content columns. */
static GtkListStore * init_content_list_store(gboolean populate)
{
	GtkListStore	*store;

	store = gtk_list_store_new(CONTENT_COLUMN_COUNT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
	if(store != NULL && populate)
	{
		GtkTreeIter	iter;
		guint		i;

		for(i = 0; i < DPC_NUM_TYPES; i++)
		{
			if(i == DPC_BLOCKS || i == DPC_BLOCKSIZE)
				continue;
			gtk_list_store_insert_with_values(store, &iter, -1,
						CONTENT_COLUMN_TITLE, dpf_get_content_title((DPContent) i),
						CONTENT_COLUMN_DESCRIPTION, dpf_get_content_name((DPContent) i),
						CONTENT_COLUMN_CONTENT, i,
						-1);
		}
	}
	return store;
}

/* 2009-04-24 -	Create a GtkTreeView suitable for displaying a bunch of column content rows. */
static GtkWidget * init_content_list_view(GtkListStore *store, gboolean available)
{
	gchar		*atitle[] = { N_("Default Title"), N_("Content") },
			*stitle[] = { N_("Title"), N_("Content") }, **title;
	GtkWidget	*wid;
	GtkCellRenderer	*cr;
	GtkTreeViewColumn *vc;

	wid = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(wid)), GTK_SELECTION_BROWSE);
	cr = gtk_cell_renderer_text_new();
	title = available ? atitle : stitle;
	vc = gtk_tree_view_column_new_with_attributes(_(title[0]), cr, "text", CONTENT_COLUMN_TITLE, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(wid), vc);
	vc = gtk_tree_view_column_new_with_attributes(_(title[1]), cr, "text", CONTENT_COLUMN_DESCRIPTION, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(wid), vc);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(wid), TRUE);

 	return wid;
}

/* 1999-05-15 -	Initialize widgets for configuring a pane. This function is horribly long,
**		but conceptually very simple: it just builds widgets.
*/
static void init_pane_page(P_DirPane *page, guint index, const gchar *other)
{
	PDef		*def;
	GtkWidget	*label, *scwin, *vbox, *frame, *hbox, *grid, *button, *sep, *fhbox;
	const gchar	*smitem[] = { N_("Directories First"), N_("Directories Last"), N_("Directories Mixed"), NULL },
				*spos[] = { N_("System Default"), N_("Left of List"), N_("Right of List") },
				*tfmt[] = { N_("Copy To %s"), N_("Copy From %s"), N_("Swap With %s") };
	gchar		buf[64];
	guint		i;
	const GCallback	scfunc[] = { G_CALLBACK(evt_sview_edit_clicked),
					G_CALLBACK(evt_sview_remove_clicked),
					G_CALLBACK(evt_sview_up_clicked),
					G_CALLBACK(evt_sview_down_clicked) };
	const GCallback	tool[] = { G_CALLBACK(evt_copyto_clicked), G_CALLBACK(evt_copyfrom_clicked),
					G_CALLBACK(evt_swap_clicked) };

	def = &page->pane[index];
	def->page  = page;
	def->index = index;

	def->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	/* Columns part. */
	frame = gtk_frame_new(_("Columns"));
	hbox  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	/*  Available content types. */
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	label = gtk_label_new(_("Available Content Types"));
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
	def->astore = init_content_list_store(TRUE);
	def->aview = init_content_list_view(def->astore, TRUE);
	g_signal_connect(G_OBJECT(def->aview), "row_activated", G_CALLBACK(evt_aview_row_activated), def);
	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scwin), def->aview);
	gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

	/*  Separation between available & selected content types. */
	sep = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
	gtk_box_pack_start(GTK_BOX(hbox), sep, FALSE, FALSE, 5);

	/*  Selected content types. */
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	label = gtk_label_new(_("Selected Content Types"));
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
	def->sstore = init_content_list_store(FALSE);
	def->sview = init_content_list_view(def->sstore, FALSE);
	gtk_tree_selection_set_mode(GTK_TREE_SELECTION(gtk_tree_view_get_selection(GTK_TREE_VIEW(def->sview))), GTK_SELECTION_SINGLE);
	g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(def->sview))), "changed", G_CALLBACK(evt_sview_selection_changed), def);
	g_signal_connect(G_OBJECT(def->sview), "row_activated", G_CALLBACK(evt_sview_row_activated), def);
	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scwin), def->sview);
	gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);
	grid = gtk_grid_new();
	def->sbutton[0] = gtk_button_new_with_label(_("Edit..."));
	def->sbutton[1] = gtk_button_new_with_label(_("Remove"));
	def->sbutton[2] = gtk_button_new_from_icon_name("go-up", GTK_ICON_SIZE_MENU);
	def->sbutton[3] = gtk_button_new_from_icon_name("go-down", GTK_ICON_SIZE_MENU);
	for(i = 0; i < sizeof def->sbutton / sizeof def->sbutton[0]; i++)
	{
		g_signal_connect(G_OBJECT(def->sbutton[i]), "clicked", G_CALLBACK(scfunc[i]), def);
		gtk_grid_attach(GTK_GRID(grid), def->sbutton[i], i, 0, 1, 1);
	}
	gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(frame), hbox);
	gtk_box_pack_start(GTK_BOX(def->vbox), frame, TRUE, TRUE, 0);

	/* Sorting. */
	frame = gtk_frame_new(_("Sorting"));
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new(_("Sort On"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	def->scmenu = dpf_get_content_combo_box(G_CALLBACK(evt_sort_set_content), def);
	gtk_box_pack_start(GTK_BOX(hbox), def->scmenu, TRUE, TRUE, 0);
	label = gtk_label_new(_("Mode"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);
	def->smmenu = gui_build_combo_box(smitem, G_CALLBACK(evt_sort_set_mode), def);
	gtk_box_pack_start(GTK_BOX(hbox), def->smmenu, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	def->sinvert = gtk_check_button_new_with_label(_("Inverse Sorting?"));
	g_signal_connect(G_OBJECT(def->sinvert), "clicked", G_CALLBACK(evt_sort_invert_clicked), def);
	gtk_box_pack_start(GTK_BOX(hbox), def->sinvert, TRUE, FALSE, 0);
	def->snocase = gtk_check_button_new_with_label(_("Ignore Case?"));
	g_signal_connect(G_OBJECT(def->snocase), "clicked", G_CALLBACK(evt_sort_nocase_clicked), def);
	gtk_box_pack_start(GTK_BOX(hbox), def->snocase, TRUE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(frame), vbox);
	gtk_box_pack_start(GTK_BOX(def->vbox), frame, FALSE, FALSE, 0);

	/* Default directory. */
	frame = gtk_frame_new(_("Default Directory"));
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	def->dentry = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(def->dentry), PATH_MAX - 1);
	g_signal_connect(G_OBJECT(def->dentry), "changed", G_CALLBACK(evt_default_path_changed), def);
	gtk_box_pack_start(GTK_BOX(hbox), def->dentry, TRUE, TRUE, 0);
	button = gtk_button_new_with_label(_("Starting Directory"));
	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(evt_default_path_current), def);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	button = gtk_button_new_with_label(_("Grab Current"));
	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(evt_default_path_grab), def);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	button = gtk_button_new_with_label(_("From History"));
	g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(evt_default_path_history), def);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(frame), hbox);
	gtk_box_pack_start(GTK_BOX(def->vbox), frame, FALSE, FALSE, 0);

	/* General section below default directory (flags, scrollbar, and 'ctrl' modification. */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	/* Flags. */
	frame = gtk_frame_new(_("Flags"));
	grid = gtk_grid_new();

	def->fabove = gtk_check_button_new_with_label(_("Path Above?"));
	g_signal_connect(G_OBJECT(def->fabove), "clicked", G_CALLBACK(evt_flag_above_clicked), def);
	gtk_grid_attach(GTK_GRID(grid), def->fabove, 0, 0, 1, 1);

	def->fhide = gtk_check_button_new_with_label(_("Hide Allowed?"));
	g_signal_connect(G_OBJECT(def->fhide), "clicked", G_CALLBACK(evt_flag_hide_clicked), def);
	gtk_grid_attach(GTK_GRID(grid), def->fhide, 0, 1, 1, 1);

	def->fscroll = gtk_check_button_new_with_label(_("Scrollbar Always?"));
	g_signal_connect(G_OBJECT(def->fscroll), "clicked", G_CALLBACK(evt_flag_scrollbar_clicked), def);
	gtk_grid_attach(GTK_GRID(grid), def->fscroll, 1, 0, 1, 1);

	def->fhparent = gtk_check_button_new_with_label(_("Huge Parent Button?"));
	g_signal_connect(G_OBJECT(def->fhparent), "clicked", G_CALLBACK(evt_flag_hugeparent_clicked), def);
	gtk_grid_attach(GTK_GRID(grid), def->fhparent, 1, 1, 1, 1);

	fhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	def->ffont = gtk_check_button_new_with_label(_("Set Custom Font?"));
	g_signal_connect(G_OBJECT(def->ffont), "clicked", G_CALLBACK(evt_flag_setfont_clicked), def);
	gtk_box_pack_start(GTK_BOX(fhbox), def->ffont, FALSE, FALSE, 0);
	def->fontbutton = gtk_font_button_new();
	g_signal_connect(G_OBJECT(def->fontbutton), "font_set", G_CALLBACK(evt_font_set), def);
	gtk_widget_set_sensitive(def->fontbutton, FALSE);
	gtk_box_pack_start(GTK_BOX(fhbox), def->fontbutton, TRUE, TRUE, 0);
	gtk_grid_attach(GTK_GRID(grid), fhbox, 0, 2, 1, 1);

	def->frband = gtk_check_button_new_with_label(_("Rubber banding Selection?"));
	g_signal_connect(G_OBJECT(def->frband), "clicked", G_CALLBACK(evt_flag_rubberbanding_clicked), def);
	gtk_grid_attach(GTK_GRID(grid), def->frband, 0, 3, 1, 1);

	gtk_container_add(GTK_CONTAINER(frame), grid);
	gtk_box_pack_start(GTK_BOX(hbox), frame, TRUE, TRUE, 0);

	/* Scrollbar position (new). */
	frame = gtk_frame_new(_("Scrollbar Position"));
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	def->splist = gui_radio_group_new(sizeof def->spos / sizeof def->spos[0], spos, def->spos);
	for(i = 0; i < sizeof def->spos / sizeof def->spos[0]; i++)
	{
		g_signal_connect(G_OBJECT(def->spos[i]), "clicked", G_CALLBACK(evt_scrollbarpos_clicked), def);
		gtk_box_pack_start(GTK_BOX(vbox), def->spos[i], FALSE, FALSE, 0);
	}
	gtk_container_add(GTK_CONTAINER(frame), vbox);
	gtk_box_pack_start(GTK_BOX(hbox), frame, TRUE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(def->vbox), hbox, FALSE, FALSE, 0);

	/* Pane "tools" (i.e., copy/swap) buttons (formerly on top). */
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	for(i = 0; i < sizeof tfmt / sizeof tfmt[0]; i++)
	{
		g_snprintf(buf, sizeof buf, _(tfmt[i]), other);
		button = gtk_button_new_with_label(buf);
		g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(tool[i]), def);
		gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 5);
	}
	gtk_box_pack_start(GTK_BOX(def->vbox), hbox, FALSE, FALSE, 5);
	gtk_widget_show_all(def->vbox);
}

/* ----------------------------------------------------------------------------------------- */

static void evt_pane_orientation_toggled(GtkWidget *wid, gpointer user)
{
	P_DirPane	*page = user;

	page->paning.edit.orientation = (g_object_get_data(G_OBJECT(wid), "horiz") != NULL) ? DPORIENT_HORIZ : DPORIENT_VERT;
}

static void evt_pane_mode_toggled(GtkWidget *wid, gpointer user)
{
	P_DirPane	*page = user;
	GtkWidget	*row;

	if((row = g_object_get_data(G_OBJECT(wid), "row")) != NULL)
		gtk_widget_set_sensitive(row, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)));
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)))
	{
		page->paning.edit.mode = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "index"));
		page->paning.value = g_object_get_data(G_OBJECT(wid), "value");
	}
}

static void init_paning_page(P_DirPane *page)
{
	PPaning		*p = &page->paning;
	GtkWidget	*grid, *rb, *lrb, *row, *value = NULL, *frame, *vbox;
	const gchar	*orientname[] = { N_("Horizontal"), N_("Vertical") },
			*modename[] = { N_("Don't Track"), N_("Ratio"), N_("Size, Left Pane"), N_("Size, Right Pane") };
	guint		i;

	p->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	frame   = gtk_frame_new(_("Pane Orientation"));
	vbox    = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	for(i = 0; i < 2; i++)
	{
		p->orientation[i] = gtk_radio_button_new_with_label(i == 1 ?
				    gtk_radio_button_get_group(GTK_RADIO_BUTTON(p->orientation[0])) :
				    NULL, _(orientname[i]));
		if(i == 0)
			g_object_set_data(G_OBJECT(p->orientation[i]), "horiz", GINT_TO_POINTER(1));
		g_signal_connect(G_OBJECT(p->orientation[i]), "toggled", G_CALLBACK(evt_pane_orientation_toggled), page);
		gtk_box_pack_start(GTK_BOX(vbox), p->orientation[i], FALSE, FALSE, 0);
	}
	gtk_container_add(GTK_CONTAINER(frame), vbox);
	gtk_box_pack_start(GTK_BOX(p->vbox), frame, FALSE, FALSE, 0);
	frame = gtk_frame_new(_("Split Tracking"));
	grid = gtk_grid_new();
	for(i = 0, lrb = NULL; i < sizeof modename / sizeof modename[0]; i++, lrb = rb)
	{
		p->mode[i] = rb = gtk_radio_button_new_with_label_from_widget(lrb ? GTK_RADIO_BUTTON(lrb) : NULL, _(modename[i]));
		g_object_set_data(G_OBJECT(rb), "index", GINT_TO_POINTER(i));
		g_signal_connect(G_OBJECT(rb), "toggled", G_CALLBACK(evt_pane_mode_toggled), page);
		gtk_grid_attach(GTK_GRID(grid), rb, 0, i, 1, 1);
		row = NULL;
		if(i == 1)
		{
			GtkAdjustment	*adj;

			adj = gtk_adjustment_new(0.5, 0.0, 1.0, 0.0125, 0.125, 0);
			row = value = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, GTK_ADJUSTMENT(adj));
			gtk_scale_set_digits(GTK_SCALE(value), 4);
			gtk_scale_set_value_pos(GTK_SCALE(value), GTK_POS_RIGHT);
		}
		else if(i == 2 || i == 3)
		{
			GtkWidget	*label;

			row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			value = gtk_entry_new();
			gtk_entry_set_text(GTK_ENTRY(value), "256");	/* Better than being empty. */
			gtk_box_pack_start(GTK_BOX(row), value, TRUE, TRUE, 0);
			label = gtk_label_new(_("pixels"));
			gtk_box_pack_start(GTK_BOX(row), label, FALSE, FALSE, 5);
		}
		if(row != NULL)
		{
			gtk_widget_set_hexpand(row, TRUE);
			gtk_widget_set_halign(row, GTK_ALIGN_FILL);
			g_object_set_data(G_OBJECT(rb), "row", row);
			g_object_set_data(G_OBJECT(rb), "value", value);
			gtk_widget_set_sensitive(row, FALSE);
			gtk_grid_attach(GTK_GRID(grid), row, 1, i, 1, 1);
		}
	}
	gtk_container_add(GTK_CONTAINER(frame), grid);
	gtk_box_pack_start(GTK_BOX(p->vbox), frame, FALSE, FALSE, 0);
}

/* 2004-04-25 -	Store boolean when toggle button toggles. Re-usable. */
static void evt_history_toggled(GtkWidget *wid, gpointer user)
{
	*(gboolean *) user = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));
}

static void init_history_page(P_DirPane *page)
{
	page->history.vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	page->history.select = gtk_check_button_new_with_label(_("Remember Selected Rows?"));
	g_signal_connect(G_OBJECT(page->history.select), "toggled", G_CALLBACK(evt_history_toggled), &page->history.edit.select);
	gtk_box_pack_start(GTK_BOX(page->history.vbox), page->history.select, FALSE, FALSE, 0);
	page->history.save = gtk_check_button_new_with_label(_("Save History Lists?"));
	g_signal_connect(G_OBJECT(page->history.save), "toggled", G_CALLBACK(evt_history_toggled), &page->history.edit.save);
	gtk_box_pack_start(GTK_BOX(page->history.vbox), page->history.save, FALSE, FALSE, 0);
}

/* 1998-06-22 -	Called by the main configuration code as it opens up. Asks this module
**		to initialize itself, and return a pointer to the root of its widgetry.
**		This module should also set the <name> pointer to its name ("DirPane").
** 1998-06-25 -	Gimpified this function, too. Terrible.
*/
static GtkWidget * cdp_init(MainInfo *min, gchar **name)
{
	P_DirPane	*page = &the_page;

	if(name == NULL)
		return NULL;

	*name = _("Dir Panes");

	page->min   = min;

	init_pane_page(page, 0, _("Right"));
	init_pane_page(page, 1, _("Left"));
	init_paning_page(page);
	init_history_page(page);

	cfg_tree_level_begin(_("Dir Panes"));
	cfg_tree_level_append(_("Left"), page->pane[0].vbox);
	cfg_tree_level_append(_("Right"), page->pane[1].vbox);
	cfg_tree_level_append(_("Pane Split"), page->paning.vbox);
	cfg_tree_level_append(_("History"), page->history.vbox);
	cfg_tree_level_end();
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-06-26 -	This gets called when the config UI opens up, and has already been up.
**		It gives this module a chance to update it's page.
*/
static void cdp_update(MainInfo *min)
{
	guint	i;

	for(i = 0; i < sizeof the_page.pane / sizeof the_page.pane[0]; i++)
		the_page.pane[i].edit = min->cfg.dp_format[i];
	the_page.paning.edit = min->cfg.dp_paning;
	the_page.history.edit = min->cfg.dp_history;

	set_pane_widgets(&the_page.pane[0]);
	set_pane_widgets(&the_page.pane[1]);
	set_paning_widgets(&the_page.paning);
	set_history_widgets(&the_page.history);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-15 -	Check if any pane was modified, and if so update the "real" options.
** 1999-05-16 -	Added check on colors, too.
*/
static void cdp_accept(MainInfo *min)
{
	guint	i;

	for(i = 0; i < sizeof the_page.pane / sizeof the_page.pane[0]; i++)
	{
		if(memcmp(&the_page.pane[i].edit, &min->cfg.dp_format[i], sizeof the_page.pane[i].edit) != 0)
		{
			min->cfg.dp_format[i] = the_page.pane[i].edit;
			cfg_set_flags(CFLG_REBUILD_MIDDLE | CFLG_RESCAN_LEFT | CFLG_RESCAN_RIGHT);
		}
	}

	switch(the_page.paning.edit.mode)
	{
		case DPSPLIT_FREE:
			break;
		case DPSPLIT_RATIO:
			the_page.paning.edit.value = gtk_range_get_value(GTK_RANGE(the_page.paning.value));
			break;
		case DPSPLIT_ABS_LEFT:
		case DPSPLIT_ABS_RIGHT:
			the_page.paning.edit.value = strtol(gtk_entry_get_text(GTK_ENTRY(the_page.paning.value)), NULL, 10);
			break;
	}
	if(memcmp(&the_page.paning.edit, &min->cfg.dp_paning, sizeof the_page.paning.edit) != 0)
	{
		min->cfg.dp_paning = the_page.paning.edit;
		cfg_set_flags(CFLG_REBUILD_MIDDLE);
	}
	if(memcmp(&the_page.history.edit, &min->cfg.dp_history, sizeof the_page.history.edit) != 0)
		min->cfg.dp_history = the_page.history.edit;
}

/* ----------------------------------------------------------------------------------------- */

static void save_extra(FILE *out, DPContent content, DpCExtra *extra)
{
	gchar	tstr[2] = "?";

	xml_put_node_open(out, "DPExtra");
	switch(content)
	{
		case DPC_NAME:
			xml_put_boolean(out, "show_type", extra->name.show_type);
			xml_put_boolean(out, "show_linkname", extra->name.show_linkname);
			break;
		case DPC_SIZE:
			xml_put_text(out, "unit", sze_get_unit_config_name(extra->size.unit));
			xml_put_boolean(out, "ticks", extra->size.ticks);
			tstr[0] = extra->size.tick;
			xml_put_text(out, "tick", tstr);
			xml_put_integer(out, "digits", extra->size.digits);
			xml_put_boolean(out, "dir_fs", extra->size.dir_show_fs_size);
			break;
		case DPC_BLOCKS:		/* Fall-through. */
		case DPC_BLOCKSIZE:
		case DPC_MODENUM:
		case DPC_NLINK:
		case DPC_UIDNUM:
		case DPC_GIDNUM:
		case DPC_DEVICE:
		case DPC_DEVMAJ:
		case DPC_DEVMIN:
			xml_put_text(out, "numformat", extra->blocks.format);
			break;
		case DPC_ATIME:
			xml_put_text(out, "dateformat", extra->a_time.format);
			break;
		case DPC_MTIME:
			xml_put_text(out, "dateformat", extra->m_time.format);
			break;
		case DPC_CRTIME:
			xml_put_text(out, "dateformat", extra->cr_time.format);
			break;
		case DPC_CHTIME:
			xml_put_text(out, "dateformat", extra->ch_time.format);
			break;
		case DPC_TYPE:
			break;
		default:
			;
	}
	xml_put_node_close(out, "DPExtra");
}

static void save_column(FILE *out, DpCFmt *column, guint index)
{
	xml_put_node_open(out, "DPColumn");
	xml_put_integer(out, "index", index);
	xml_put_text(out, "title", column->title);
	xml_put_text(out, "content", dpf_get_content_mnemonic(column->content));
	save_extra(out, column->content, &column->extra);
	xml_put_integer(out, "justification", column->just);
	xml_put_integer(out, "width", column->width);
	xml_put_node_close(out, "DPColumn");
}

/* 1998-07-27 -	Rewritten to use the new xml_put_XXXX() funtions. Loads cleaner. */
static void save_sort(FILE *out, DPSort *sort)
{
	xml_put_node_open(out, "DPSort");
	xml_put_text(out, "content", dpf_get_content_mnemonic(sort->content));
	xml_put_text(out, "mode", mode_name[sort->mode]);
	xml_put_boolean(out, "invert", sort->invert);
	xml_put_boolean(out, "nocase", sort->nocase);
	xml_put_node_close(out, "DPSort");
}

static void save_pane(FILE *out, DPFormat *fmt, const gchar *node_name)
{
	guint	i;

	xml_put_node_open(out, node_name);
	xml_put_integer(out, "columns", fmt->num_columns);
	for(i = 0; i < fmt->num_columns; i++)
		save_column(out, &fmt->format[i], i);
	save_sort(out, &fmt->sort);
	xml_put_text(out, "defpath", fmt->def_path);
	xml_put_boolean(out, "path_above", fmt->path_above);
	xml_put_boolean(out, "hide_allowed", fmt->hide_allowed);
	xml_put_boolean(out, "scrollbar_always", fmt->scrollbar_always);
	xml_put_boolean(out, "huge_parent", fmt->huge_parent);
	xml_put_boolean(out, "set_font", fmt->set_font);
	xml_put_text(out, "font_name", fmt->font_name);
	xml_put_boolean(out, "rubber_banding", fmt->rubber_banding);
	xml_put_text(out, "sbar_pos", sbarpos_name[fmt->sbar_pos]);
	xml_put_node_close(out, node_name);
}

static void save_paning(FILE *out, DPPaning *p)
{
	xml_put_node_open(out, "DirPanePaning");
	xml_put_text(out, "orientation", pane_orient_name[p->orientation]);
	xml_put_text(out, "mode", pane_splitmode_name[p->mode]);
	xml_put_real(out, "value", p->value);
	xml_put_node_close(out, "DirPanePaning");
}

static void save_history(FILE *out, DPHistory *p)
{
	xml_put_node_open(out, "DirPaneHistory");
	xml_put_boolean(out, "select", p->select);
	xml_put_boolean(out, "save", p->save);
	xml_put_node_close(out, "DirPaneHistory");
}

/* 1998-07-25 -	Save out the dirpane configuration information, in XML format. Might tend
**		to be kind of huge.
*/
static gint cdp_save(MainInfo *min, FILE *out)
{
	guint	i, num = sizeof min->cfg.dp_format / sizeof min->cfg.dp_format[0];

	xml_put_node_open(out, NODE);
	xml_put_integer(out, "count", num);
	for(i = 0; i < num; i++)
		save_pane(out, &min->cfg.dp_format[i], config_name[i]);
	save_paning(out, &min->cfg.dp_paning);
	save_history(out, &min->cfg.dp_history);
	xml_put_node_close(out, NODE);

	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-07-26 -	Load any "extraneous" information a column might have. Note that these
**		really rely on the fact (?) that fields of a union all have the same
**		offset from it (namely 0).
*/
static void load_extra(const XmlNode *node, DPContent content, DpCExtra *extra)
{
	gint	tmp;

	switch(content)
	{
		case DPC_NAME:
			if(xml_get_boolean(node, "show_type", &tmp))
				extra->name.show_type = tmp;
			else
				extra->name.show_type = FALSE;
			if(xml_get_boolean(node, "show_linkname", &tmp))
				extra->name.show_linkname = tmp;
			else
				extra->name.show_linkname = FALSE;
			break;
		case DPC_SIZE:
			{
				const gchar	*str;

				if(xml_get_text(node, "unit", &str) || xml_get_text(node, "type", &str))
					extra->size.unit = sze_parse_unit_config_name(str);
				else
					extra->size.unit = SZE_BYTES;
				if(xml_get_boolean(node, "ticks", &tmp))
					extra->size.ticks = tmp;
				if(xml_get_text(node, "tick", &str))
					extra->size.tick = str[0];
				if(xml_get_integer(node, "digits", &tmp))
					extra->size.digits = tmp;
				if(extra->size.digits < 0 || extra->size.digits > 6)
					extra->size.digits = 0;
				g_snprintf(extra->size.dformat, sizeof extra->size.dformat, "%%#.%uf", extra->size.digits);

				tmp = TRUE;
				xml_get_boolean(node, "dir_fs", &tmp);
				extra->size.dir_show_fs_size = tmp;	/* If load fails, use default in tmp. */
			}
			break;
		case DPC_BLOCKS:
		case DPC_BLOCKSIZE:
		case DPC_MODENUM:
		case DPC_NLINK:
		case DPC_UIDNUM:
		case DPC_GIDNUM:
		case DPC_DEVICE:
		case DPC_DEVMAJ:
		case DPC_DEVMIN:
			xml_get_text_copy(node, "numformat", extra->blocks.format, sizeof extra->blocks.format);
			break;
		case DPC_ATIME:
		case DPC_MTIME:
		case DPC_CRTIME:
		case DPC_CHTIME:
			xml_get_text_copy(node, "dateformat", extra->a_time.format, sizeof extra->a_time.format);
			break;
		case DPC_TYPE:
			break;
		default:
			;
	}
}

/* 1998-07-26 -	Load sorting configuration. */
static void load_sort(const XmlNode *node, DPSort *sort)
{
	const gchar	*content, *mode;
	gint		tmp;

	if(xml_get_text(node, "content", &content))
	{
		if((sort->content = dpf_get_content_from_mnemonic(content)) >= DPC_NUM_TYPES &&
		   (sort->content = dpf_get_content_from_name(content)) >= DPC_NUM_TYPES)
		{
			sort->content = DPC_NAME;
		}
	}
	if(xml_get_text(node, "mode", &mode))
	{
		gboolean	found = FALSE;
		guint		i;

		for(i = 0; i < sizeof mode_name / sizeof mode_name[0]; i++)
		{
			if(strcmp(mode, mode_name[i]) == 0)
			{
				sort->mode = (SortMode) i;
				found = TRUE;
				break;
			}
		}
		if(!found)
			g_warning("**DPCFG: Unknown sort mode %s\n", mode);
	}
	if(xml_get_boolean(node, "invert", &tmp))
		sort->invert = tmp;
	if(xml_get_boolean(node, "nocase", &tmp))
		sort->nocase = tmp;
}

/* 1998-07-26 -	Load a column configuration. Since this is called as a callback from the
**		xml_node_visit_children() function, we must make sure that the given node
**		really is for a column.
*/
static void load_column(const XmlNode *node, gpointer user)
{
	DPFormat	*fmt = user;
	DpCFmt		*colfmt;
	const XmlNode	*data;
	const gchar	*cn;
	gint		index;
	DPContent	cid;

	union {
		GtkJustification	justification;
		gint			integer;
	} jtmp;

	if(!xml_node_has_name(node, "DPColumn"))	/* Not actually a column? */
		return;

	if(xml_get_integer(node, "index", &index))
	{
		colfmt = &fmt->format[index];
		xml_get_text_copy(node, "title", colfmt->title, sizeof colfmt->title);
		if(xml_get_text(node, "content", &cn))
		{
			if(((cid = dpf_get_content_from_mnemonic(cn)) < DPC_NUM_TYPES) ||
			    (cid = dpf_get_content_from_name(cn)) < DPC_NUM_TYPES)
			{
				colfmt->content = cid;
			}
			else
			{
				g_warning("Unknown content type \"%s\" detected--set to \"Size\"", cn);
				colfmt->content = DPC_SIZE;
			}
		}
		if((data = xml_tree_search(node, "DPExtra")) != NULL)
			load_extra(data, colfmt->content, &colfmt->extra);
		xml_get_integer(node, "justification", &jtmp.integer);
		colfmt->just = jtmp.justification;
		xml_get_integer(node, "width", &colfmt->width);
	}
	else
		g_warning("**DPCFG: Config node missing 'index' leaf");
}

static void load_pane(const XmlNode *node, DPFormat *fmt)
{
	const XmlNode	*data;
	const gchar	*fontname, *sbarpos;
	gint		tmp;

	if(xml_get_integer(node, "columns", (gint *) &fmt->num_columns))
	{
		xml_node_visit_children(node, load_column, fmt);

		if((data = xml_tree_search(node, "DPSort")) != NULL)
			load_sort(data, &fmt->sort);
		xml_get_text_copy(node, "defpath", fmt->def_path, sizeof fmt->def_path);
		if(xml_get_boolean(node, "path_above", &tmp))
			fmt->path_above = tmp;
		if(xml_get_boolean(node, "hide_allowed", &tmp))
			fmt->hide_allowed = tmp;
		if(xml_get_boolean(node, "scrollbar_always", &tmp))
			fmt->scrollbar_always = tmp;
		if(xml_get_boolean(node, "huge_parent", &tmp))
			fmt->huge_parent = tmp;
		if(xml_get_boolean(node, "set_font", &tmp))
			fmt->set_font = tmp;
		if(xml_get_text(node, "font_name", &fontname))
			g_snprintf(fmt->font_name, sizeof fmt->font_name, "%s", fontname);
		if(xml_get_boolean(node, "rubber_banding", &tmp))
			fmt->rubber_banding = tmp;
		if(xml_get_text(node, "sbar_pos", &sbarpos))
		{
			gboolean	found = FALSE;
			guint		i;

			for(i = 0; i < sizeof sbarpos_name / sizeof sbarpos_name[0]; i++)
			{
				if(strcmp(sbarpos, sbarpos_name[i]) == 0)
				{
					fmt->sbar_pos = (SBarPos) i;
					found = TRUE;
					break;
				}
			}
			if(!found)
				g_warning("**DPCFG: Unknown scrollbar position '%s'", sbarpos);
		}
	}
}

static void load_paning(const XmlNode *node, DPPaning *paning)
{
	const gchar	*str;
	gfloat		tmp;

	if(xml_get_text(node, "orientation", &str))
		paning->orientation = stu_strcmp_vector(str, pane_orient_name, sizeof pane_orient_name / sizeof *pane_orient_name, DPORIENT_HORIZ);
	if(xml_get_text(node, "mode", &str))
		paning->mode = stu_strcmp_vector(str, pane_splitmode_name, sizeof pane_splitmode_name / sizeof *pane_splitmode_name, DPSPLIT_FREE);
	if(xml_get_real(node, "value", &tmp))
		paning->value = tmp;
}

static void load_history(const XmlNode *node, DPHistory *history)
{
	gboolean	tmp;

	if(xml_get_boolean(node, "select", &tmp))
		history->select = tmp;
	if(xml_get_boolean(node, "save", &tmp))
		history->save = tmp;
}

/* 2010-02-07 -	Check if the deprecated "blocks" and "blocksize" content type was used. If it was, remove it and nag. */
static void remove_deprecated_content(MainInfo *min, int index)
{
	int		i, tail;
	gboolean	keep;
	DPFormat	*fmt = &min->cfg.dp_format[index];

	for(i = 0; i < fmt->num_columns; /* No increase here! */)
	{
		keep = FALSE;

		if (fmt->format[i].content == DPC_BLOCKS)
		{
			ndl_dialog_sync_new_wait(min, "blocks-deprecated", _("'Blocks' Content Deprecated"),
					_("The 'Blocks' column content type is no longer supported,\n"
					  "but your configuration is still making use of it. It will be\n"
					  "automatically removed."));
		}
		else if(fmt->format[i].content == DPC_BLOCKSIZE)
		{
			ndl_dialog_sync_new_wait(min, "blocksize-deprecated", _("'Block Size' Content Deprecated"),
					_("The 'Block Size' column content type is no longer supported,\n"
					  "but your configuration is still making use of it. It will be\n"
					  "automatically removed."));
		}
		else
			keep = TRUE;

		/* Check what the resolution was, do we keep the column or not? */
		if(!keep)
		{
			/* Just move the trailing elements down, and reduce count. */
			tail = min->cfg.dp_format[index].num_columns - i - 1;
			memmove(&fmt->format[i], &fmt->format[i + 1], tail * sizeof fmt->format[i]);
			fmt->num_columns--;
			cfg_modified_set(min);
		}
		else
			i++;
	}
}

/* 1998-07-26 -	Load (e.g. parse) the DirPane data found in the XML tree <node>. */
static void cdp_load(MainInfo *min, const XmlNode *node)
{
	const XmlNode	*pane, *sub;
	gint		num, i;

	if(xml_get_integer(node, "count", &num))
	{
		for(i = 0; i < num; i++)
		{
			if((pane = xml_tree_search(node, config_name[i])) != NULL)
			{
				load_pane(pane, &min->cfg.dp_format[i]);
				remove_deprecated_content(min, i);
			}
			else
				g_warning("**DPCFG: Couldn't find '%s' in config file!\n", config_name[i]);
		}
	}
	if((sub = xml_tree_search(node, "DirPanePaning")) != NULL)
		load_paning(sub, &min->cfg.dp_paning);
	if((sub = xml_tree_search(node, "DirPaneHistory")) != NULL)
		load_history(sub, &min->cfg.dp_history);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-06-26 -	Return a description of this module. */
const CfgModule * cdp_describe(MainInfo *min)
{
	static const CfgModule	desc = { NODE, cdp_init, cdp_update, cdp_accept, cdp_save, cdp_load, NULL };

	return &desc;
}
