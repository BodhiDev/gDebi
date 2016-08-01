/*
** 1998-09-26 -	A brand new command configuration page!
** 1998-10-03 -	There's a weird sensitivity bug with the command row type option menu...
** 1999-03-09 -	Redesigned the Before/After flag GUI. It's less stupid now, and vertically shorter.
** 1999-06-19 -	Adapted for the new dialog module.
** 2000-07-02 -	Initialized translation by marking strings and stuff.
*/

#include "gentoo.h"
#include "guiutil.h"
#include "cmdseq.h"
#include "dialog.h"
#include "hash_dialog.h"

#include "configure.h"
#include "cfg_module.h"
#include "cfg_cmdseq.h"

#define	NODE	"CmdSeqs"

/* ----------------------------------------------------------------------------------------- */

typedef struct {		/* Extra widgetry for built-in rows. Not very much. */
	GtkWidget	*vbox;
	GtkWidget	*label;		/* Just a big fat label saying that there's nothing here. */
} PX_Builtin;

typedef struct {		/* External command general flags. */
	GtkWidget	*vbox;		/* A box that holds the page. */
	GtkWidget	*runbg;		/* Run in background? */
	GtkWidget	*killprev;	/* Kill previous instance? */
	GtkWidget	*survive;	/* Survive quit? */
	GtkWidget	*graboutput;	/* Grab output? */
} PXE_General;

typedef struct {
	GtkWidget	*vbox;
	GtkWidget	*hbox;
	GtkWidget	*reqsel_src;
	GtkWidget	*reqsel_dst;
	GtkWidget	*cdsrc;
	GtkWidget	*cddst;
} PXE_BF;

typedef struct {
	GtkWidget	*vbox;
	GtkWidget	*rssrc;
	GtkWidget	*rsdst;
} PXE_AF;

typedef struct {		/* Extra widgetry for external commands. Plenty of flags. */
	GtkWidget	*nbook;		/* A notebook. */
	PXE_General	gflags;		/* General flags page. */
	PXE_BF		bf;
	PXE_AF		af;
} PX_External;

typedef struct {
	GtkWidget	*vbox;		/* Standard. */
	GtkListStore	*store;		/* Main list store, holding defined command sequences. */
	GtkWidget	*view;		/* Main tree view. */

	GtkWidget	*nhbox;		/* A hbox holding name label and entry. */
	GtkWidget	*name;		/* Name entry widget. */

	GtkWidget	*dframe;	/* Definition frame. */
	GtkWidget	*dbtn[3];	/* Definition row commands. */
	GtkListStore	*dstore;	/* Tree model for the definition. */
	gulong		sig_delete;	/* Handler for the "row_deleted" signal. */
	GtkWidget	*dview;		/* Tree view for the definition. */
	GtkWidget	*dhbox;		/* Hbox for type, definition, and pick button. */
	GtkWidget	*dtype;		/* Type option menu, select built-in/external. */
	GtkWidget	*ddef;		/* Row definition. */
	GtkWidget	*dpick;		/* Pick button, for definition help. */

	GtkWidget	*dextra;	/* Notebook for type-specific info (flags etc). */

	GtkWidget	*drepeat;	/* Repeat-flag, global for sequence. */

	PX_Builtin	px_builtin;
	PX_External	px_external;

	GtkWidget	*bhbox;		/* Command button hbox ("Add" & "Delete"). */
	GtkWidget	*badd, *bdel;

	MainInfo	*min;		/* Very handy. */
	GHashTable	*cmdseq;	/* Copies of all command sequences live here. */
	gboolean	modified;	/* Has the page been modified? */
} P_CmdSeq;

static P_CmdSeq	the_page;

enum {
	CMDSEQ_COLUMN_NAME,
	CMDSEQ_COLUMN_CMDSEQ,

	CMDSEQ_COLUMN_COUNT
};

enum {
	DEF_COLUMN_TYPE,
	DEF_COLUMN_DEF,
	DEF_COLUMN_ROW,

	DEF_COLUMN_COUNT
};

/* ----------------------------------------------------------------------------------------- */

static void	set_row_widgets(P_CmdSeq *page);
static void	reset_row_widgets(P_CmdSeq *page);
static void	evt_type_selected(GtkWidget *wid, guint index, gpointer user);

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-26 -	Copy the command sequence pointed to by <data>, and insert it into
**		both the "editing hash table" and the ListStore for the main list.
*/
static void copy_and_insert_cmdseq(gpointer key, gpointer data, gpointer user)
{
	P_CmdSeq	*page = user;
	CmdSeq		*seq;

	if((seq = csq_cmdseq_copy(data)) != NULL)
	{
		GtkTreeIter	titer;

		csq_cmdseq_hash(&page->cmdseq, seq);

		gtk_list_store_insert_with_values(page->store, &titer, -1, CMDSEQ_COLUMN_NAME, seq->name, CMDSEQ_COLUMN_CMDSEQ, seq, -1);
	}
}

/* 1998-09-26 -	Copy all current commands into our editing version, and also populate the
**		main list with a list of the available commands.
*/
static void populate_list(MainInfo *min, P_CmdSeq *page)
{
	page->cmdseq = NULL;
	if(min->cfg.commands.cmdseq != NULL)
	{
		gtk_list_store_clear(page->store);
		g_hash_table_foreach(min->cfg.commands.cmdseq, copy_and_insert_cmdseq, page);
	}
}

/* ----------------------------------------------------------------------------------------- */

static CmdSeq * cmdseq_get_selected(P_CmdSeq *page, GtkTreeIter *iter)
{
	GtkTreeIter	local;
	CmdSeq		*cmdseq = NULL;

	if(iter == NULL)
		iter = &local;

	if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->view)), NULL, iter))
		gtk_tree_model_get(GTK_TREE_MODEL(page->store), iter, CMDSEQ_COLUMN_CMDSEQ, &cmdseq, -1);

	return cmdseq;
}

/* 2009-03-20 -	Find the currently selected command row, and return pointer to it. */
static CmdRow * cmdrow_get_selected(P_CmdSeq *page, GtkTreeIter *iter)
{
	GtkTreeIter	local;
	CmdRow		*row = NULL;

	if(page == NULL)
		return NULL;
	if(iter == NULL)
		iter = &local;

	if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->dview)), NULL, iter))
		gtk_tree_model_get(GTK_TREE_MODEL(page->dstore), iter, DEF_COLUMN_ROW, &row, -1);

	return row;
}

static gboolean get_selections(P_CmdSeq *page, CmdSeq **cmdseq, CmdRow **row)
{
	if(cmdseq != NULL)
		*cmdseq = cmdseq_get_selected(page, NULL);
	if(row != NULL)
		*row = cmdrow_get_selected(page, NULL);
	return (cmdseq == NULL || *cmdseq != NULL) && (row == NULL || *row != NULL);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-26 -	We have a selection, so go ahead and populate the definition clist. */
static void populate_dlist(P_CmdSeq *page)
{
	GtkTreeIter	ti;
	const GList	*iter;
	const CmdSeq	*cs;
	const CmdRow	*row;

	g_signal_handler_block(G_OBJECT(page->dstore), page->sig_delete);
	gtk_list_store_clear(page->dstore);
	if((cs = cmdseq_get_selected(page, &ti)) != NULL)
	{
		for(iter = cs->rows; iter != NULL; iter = g_list_next(iter))
		{
			row = iter->data;
			gtk_list_store_insert_with_values(page->dstore, &ti, -1,
						DEF_COLUMN_TYPE, csq_cmdrow_type_to_string(row->type),
						DEF_COLUMN_DEF,  row->def->str,
						DEF_COLUMN_ROW,  row,
						-1);
		}
	}
	g_signal_handler_unblock(G_OBJECT(page->dstore), page->sig_delete);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-26 -	Reset all widgets to their most passive state. */
static void reset_widgets(P_CmdSeq *page)
{
	gtk_entry_set_text(GTK_ENTRY(page->name), "");
	gtk_widget_set_sensitive(page->nhbox, FALSE);

	g_signal_handler_block(G_OBJECT(page->dstore), page->sig_delete);
	gtk_list_store_clear(page->dstore);
	g_signal_handler_unblock(G_OBJECT(page->dstore), page->sig_delete);

	gtk_combo_box_set_active(GTK_COMBO_BOX(page->dtype), 0);
	gtk_entry_set_text(GTK_ENTRY(page->ddef), "");
	gtk_notebook_set_current_page(GTK_NOTEBOOK(page->dextra), 0);
	reset_row_widgets(page);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->drepeat), FALSE);
	gtk_widget_set_sensitive(page->dframe, FALSE);

	gtk_widget_set_sensitive(page->bdel, FALSE);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-08 -	Set the state of the command's repeat flag. Only available if no command row
**		runs in the background, will be automatically disabled as soon as a row is
**		given the background flag.
*/
static void set_repeat(P_CmdSeq *page)
{
	CmdSeq	*cs;
	CmdRow	*row;
	GList	*iter;

	if((cs = cmdseq_get_selected(page, NULL)) == NULL)
		return;

	for(iter = cs->rows; iter != NULL; iter = g_list_next(iter))
	{
		row = iter->data;
		if((row->type == CRTP_EXTERNAL) && (row->extra.external.gflags & CGF_RUNINBG))
			break;
	}
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->drepeat),
					(cs->flags & CSFLG_REPEAT) && (iter == NULL));
	gtk_widget_set_sensitive(page->drepeat, iter == NULL);
}

/* 1998-09-26 -	Set widgets in a state suitable to the current selection. */
static void set_widgets(P_CmdSeq *page)
{
	CmdSeq	*cs;

	if((cs = cmdseq_get_selected(page, NULL)) == NULL)
		return;

	gtk_entry_set_text(GTK_ENTRY(page->name), cs->name);
	gtk_widget_set_sensitive(page->nhbox, TRUE);

	populate_dlist(page);
	reset_row_widgets(page);
	gtk_widget_set_sensitive(page->dframe, TRUE);
	gtk_widget_set_sensitive(page->bdel, TRUE);
	set_repeat(page);
}

/* ----------------------------------------------------------------------------------------- */

static void evt_cmdseq_selection_changed(GtkTreeSelection *sel, gpointer user)
{
	P_CmdSeq	*page = user;
	GtkTreeIter	iter;
	CmdSeq		*cmdseq;

	if((cmdseq = cmdseq_get_selected(page, &iter)) != NULL)
		set_widgets(page);
	else
		reset_widgets(page);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-26 -	User is editing the name of the current sequence.
** 2009-03-15 -	Rewritten for GTK+ 2-based list handling. It resorts live, here in the future.
*/
static gint evt_name_changed(GtkWidget *wid, gpointer user)
{
	P_CmdSeq	*page = user;
	GtkTreeIter	iter;
	CmdSeq		*cs;
	const gchar	*text;

	if((cs = cmdseq_get_selected(page, &iter)) == NULL)
		return TRUE;
	if((text = gtk_entry_get_text(GTK_ENTRY(wid))) == NULL)
		return TRUE;
	csq_cmdseq_set_name(page->cmdseq, cs, text);
	gtk_list_store_set(page->store, &iter,
				CMDSEQ_COLUMN_NAME, text,
				-1);
	page->modified = TRUE;
	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-27 -	Set general flag widgets in <pxeg> to match <flags>. */
static void set_cx_g_flags(PXE_General *pxeg, guint32 flags)
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pxeg->runbg), (flags & CGF_RUNINBG) != 0);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pxeg->killprev), (flags & CGF_KILLPREV) != 0);
	gtk_widget_set_sensitive(pxeg->killprev, (flags & CGF_RUNINBG) != 0);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pxeg->graboutput), (flags & CGF_GRABOUTPUT) != 0);
	gtk_widget_set_sensitive(pxeg->survive, (flags & CGF_RUNINBG) != 0);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pxeg->survive), (flags & CGF_SURVIVE) != 0);
}

/* 1999-03-09 -	Set visual state of before/after checkbuttons, according to <flags>. Sets for
**		the before group if <after> is FALSE, otherwise for the after group. Geddit?
*/
static void set_cx_ba_flags(PXE_BF *pxbf, PXE_AF *pxaf, guint after, guint32 flags)
{
	if(!after)		/* Set "before" type flags, i.e. CD source XOR dest. */
	{
		guint	st_src = FALSE, st_dst = FALSE;		/* Guess a few states. */

		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pxbf->reqsel_src), (flags & CBAF_REQSEL_SOURCE) != 0);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pxbf->reqsel_dst), (flags & CBAF_REQSEL_DEST) != 0);
		if(flags & CBAF_CD_SOURCE)
			st_src = TRUE;
		else if(flags & CBAF_CD_DEST)
			st_dst = TRUE;
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pxbf->cdsrc), st_src);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pxbf->cddst), st_dst);
	}
	else			/* Set "after" flags, i.e. rescan source IOR dest. */
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pxaf->rssrc), (flags & CBAF_RESCAN_SOURCE) != 0);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pxaf->rsdst), (flags & CBAF_RESCAN_DEST) != 0);
	}
}

/* 1998-09-27 -	Set the "extra" widgets for the current row, which is known to be external. */
static void set_cx_external_widgets(P_CmdSeq *page)
{
	CmdRow	*row;
	CX_Ext	*ext;

	if((row = cmdrow_get_selected(page, NULL)) == NULL)
		return;

	ext = &row->extra.external;

	set_cx_g_flags(&page->px_external.gflags, ext->gflags);

	set_cx_ba_flags(&page->px_external.bf, &page->px_external.af, FALSE, ext->baflags[0]);
	set_cx_ba_flags(&page->px_external.bf, &page->px_external.af, TRUE,  ext->baflags[1]);
}

/* 1998-09-27 -	Set up widgets for row, assuming there is a row selected. */
static void set_row_widgets(P_CmdSeq *page)
{
	CmdRow	*row;
	guint	i;

	if((row = cmdrow_get_selected(page, NULL)) == NULL)
		return;
	for(i = 1; i < sizeof page->dbtn / sizeof page->dbtn[0]; i++)
		gtk_widget_set_sensitive(page->dbtn[i], TRUE);
	gui_combo_box_set_blocked(page->dtype, TRUE);
	gtk_combo_box_set_active(GTK_COMBO_BOX(page->dtype), row->type);
	gui_combo_box_set_blocked(page->dtype, FALSE);

	gtk_entry_set_text(GTK_ENTRY(page->ddef), row->def->str);
	gtk_notebook_set_current_page(GTK_NOTEBOOK(page->dextra), row->type);
	gtk_widget_set_sensitive(page->dhbox,  TRUE);
	gtk_widget_set_sensitive(page->dextra, TRUE);
	gtk_widget_set_sensitive(page->dframe, TRUE);

	if(row->type == CRTP_EXTERNAL)
		set_cx_external_widgets(page);
}

/* 1998-09-27 -	Reset the row widgets. */
static void reset_row_widgets(P_CmdSeq *page)
{
	guint	i;

	if(page != NULL)
	{
		for(i = 1; i < sizeof page->dbtn / sizeof page->dbtn[0]; i++)
			gtk_widget_set_sensitive(page->dbtn[i], FALSE);
		gtk_entry_set_text(GTK_ENTRY(page->ddef), "");
		gtk_widget_set_sensitive(page->dhbox, FALSE);
		gtk_widget_set_sensitive(page->dextra, FALSE);
	}
}

/* 2009-03-20 -	Row selection changed, so update widgetry. */
static void evt_cmdrow_selection_changed(GtkTreeSelection *sel, gpointer user)
{
	P_CmdSeq	*page = user;
	GtkTreeIter	iter;

	if(page == NULL)
		return;
	if(cmdrow_get_selected(page, &iter) != NULL)
		set_row_widgets(page);
	else
		reset_row_widgets(page);
}

/* 1999-03-08 -	User hit the "Repeat" check button, grab it and set sequence's flag
**		accordingly.
*/
static void evt_repeat_clicked(GtkWidget *wid, gpointer user)
{
	P_CmdSeq	*page = user;
	CmdSeq		*cs;

	if((cs = cmdseq_get_selected(page, NULL)) == NULL)
		return;

	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)))
		cs->flags |= CSFLG_REPEAT;
	else
		cs->flags &= ~CSFLG_REPEAT;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-27 -	User just hit the "Add Row" button, so let's do just that. */
static void evt_addrow_clicked(GtkWidget *wid, gpointer user)
{
	P_CmdSeq	*page = user;
	CmdSeq		*cs;
	CmdRow		*nr;
	CRType		type;

	if((cs = cmdseq_get_selected(page, NULL)) == NULL)
		return;
	if((nr = cmdrow_get_selected(page, NULL)) != NULL)
		type = nr->type;
	else
		type = CRTP_BUILTIN;

	if((nr = csq_cmdrow_new(type, "", 0UL)) != NULL)
	{
		GtkTreeIter	iter;
		GtkTreePath	*path;

		csq_cmdseq_row_append(cs, nr);
		gtk_list_store_insert_with_values(page->dstore, &iter, -1,
					DEF_COLUMN_TYPE, csq_cmdrow_type_to_string(nr->type),
					DEF_COLUMN_DEF,  nr->def->str,
					DEF_COLUMN_ROW,  nr,
					-1);
		gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->dview)), &iter);
		if((path = gtk_tree_model_get_path(GTK_TREE_MODEL(page->dstore), &iter)) != NULL)
		{
			gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(page->dview), path, NULL, TRUE, 0.5f, 0.5f);
			gtk_tree_path_free(path);
		}
		gtk_widget_grab_focus(page->ddef);
		page->modified = TRUE;
	}
}

/* 1998-09-27 -	Delete the current row from the current command sequence. */
static void evt_delrow_clicked(GtkWidget *wid, gpointer user)
{
	P_CmdSeq	*page = user;
	CmdSeq		*cs;
	GtkTreeIter	riter;
	CmdRow		*row;

	if((cs = cmdseq_get_selected(page, NULL)) == NULL)
		return;
	if((row = cmdrow_get_selected(page, &riter)) == NULL)
		return;
	csq_cmdseq_row_delete(cs, row);
	gtk_list_store_remove(page->dstore, &riter);
	page->modified = TRUE;
}

/* 1998-09-27 -	User wants to duplicate the current row. Fine. */
static void evt_duprow_clicked(GtkWidget *wid, gpointer user)
{
	P_CmdSeq	*page = user;
	CmdSeq		*cs;
	GtkTreeIter	riter;
	CmdRow		*row, *nr;
	GtkTreePath	*path;

	if((cs = cmdseq_get_selected(page, NULL)) == NULL)
		return;
	if((row = cmdrow_get_selected(page, &riter)) == NULL)
		return;

	if((nr = csq_cmdrow_copy(row)) != NULL)
	{
		csq_cmdseq_row_append(cs, nr);
		gtk_list_store_insert_with_values(page->dstore, &riter, -1,
					DEF_COLUMN_TYPE, csq_cmdrow_type_to_string(nr->type),
					DEF_COLUMN_DEF,  nr->def->str,
					DEF_COLUMN_ROW,  nr,
					-1);
		gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->dview)), &riter);
		if((path = gtk_tree_model_get_path(GTK_TREE_MODEL(page->dstore), &riter)) != NULL)
		{
			gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(page->dview), path, NULL, TRUE, 0.5f, 0.5f);
			gtk_tree_path_free(path);
		}
		page->modified = TRUE;
	}
}

static void evt_row_deleted(GtkTreeModel *model, GtkTreePath *path, gpointer user)
{
	P_CmdSeq	*page = user;
	CmdSeq		*cs;
	GtkTreeIter	csiter, riter;
	gboolean	valid;
	GList		*rows = NULL;

	if((cs = cmdseq_get_selected(page, &csiter)) == NULL)
		return;

	/* Massive simplification, that still feels slightly ugly: rather than
	** being all clever and replicating the move on the real model, we just
	** zzap the CmdSeq's rows and re-build based on the GtkListStore. No
	** point in freeing the rows though, so build a new list and "relink".
	*/
	for(valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(page->dstore), &riter);
	    valid;
	    valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(page->dstore), &riter))
	{
		CmdRow	*row;
		gtk_tree_model_get(GTK_TREE_MODEL(page->dstore), &riter, DEF_COLUMN_ROW, &row, -1);
		rows = g_list_append(rows, row);
	}
	csq_cmdseq_rows_relink(cs, rows);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-26 -	User choose a new type for the current row. */
static void evt_type_selected(GtkWidget *wid, guint index, gpointer user)
{
	P_CmdSeq	*page = user;
	CmdRow		*row;
	GtkTreeIter	iter;

	if((row = cmdrow_get_selected(page, &iter)) == NULL)
		return;

	csq_cmdrow_set_type(row, (CRType) index);
	gtk_list_store_set(page->dstore, &iter, DEF_COLUMN_TYPE, csq_cmdrow_type_to_string(row->type), -1);
	gtk_notebook_set_current_page(GTK_NOTEBOOK(page->dextra), (CRType) index);
	page->modified = TRUE;
}

/* 1998-09-27 -	User changed the command definition, so we need to store the new one.
**		Notice how this routine avoids to rebuild the entire clist; this is
**		probably a good idea since the changes may be rapid.
*/
static void evt_def_changed(GtkWidget *wid, gpointer user)
{
	P_CmdSeq	*page = user;
	CmdRow		*row;
	GtkTreeIter	riter;
	const gchar	*def;

	if((row = cmdrow_get_selected(page, &riter)) == NULL)
		return;

	if((def = gtk_entry_get_text(GTK_ENTRY(wid))) != NULL)
	{
		csq_cmdrow_set_def(row, def);
		gtk_list_store_set(page->dstore, &riter, DEF_COLUMN_DEF, def, -1);
		page->modified = TRUE;
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-27 -	Pop up a requester where the user can choose among all the built-in commands. */
static void pick_builtin(P_CmdSeq *page)
{
	const gchar	*cmd;

	if((cmd = hdl_dialog_sync_new_wait(page->min->cfg.commands.builtin, _("Select Builtin"))) != NULL)
		gtk_entry_set_text(GTK_ENTRY(page->ddef), cmd);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-27 -	Let user pick a special code sequence for use in external commands. This
**		is very simple, but becomes complex because I want to use a clist.
*/
static void pick_external(P_CmdSeq *page)
{
	Dialog			*dlg;
	GtkListStore		*store;
	GtkCellRenderer		*cr;
	GtkTreeViewColumn	*vc;
	GtkWidget		*scwin, *view;
	const gchar	*code[] = {	"\\{",	N_("Opening brace"),
					"\\}",	N_("Closing brace"),
					"f",	N_("First selected"),
					"fu",	N_("First selected, unselect"),
					"fp",	N_("First selected, with path"),
					"fpu",	N_("First selected, with path, unselect"),
					"fd",	N_("First selected (destination pane)"),
					"fQ",	N_("First selected, no quotes"),
					"fE",	N_("First selected, no extension"),
					"F",	N_("All selected"),
					"Fu",	N_("All selected, unselect"),
					"Fp",	N_("All selected, with paths"),
					"Fpu",	N_("All selected, with paths, unselect"),
					"Fd",	N_("All selected (destination pane)"),
					"FQ",	N_("All selected, no quotes"),
					"FE",	N_("All selected, no extensions"),
					"Ps",	N_("Path to source pane's directory"),
					"Pd",	N_("Path to destination pane's directory"),
					"Ph",	N_("Path to home directory"),
					"Pl",	N_("Path of left pane"),
					"Pr",	N_("Path of right pane"),
					"u",	N_("URI of first selected"),
					"uu",	N_("URI of first selected, unselect"),
					"uQ",	N_("URI of first selected, no quotes"),
					"U",	N_("URIs of all selected"),
					"Uu",	N_("URIs of all selected, unselect"),
					"UQ",	N_("URIs of all selected, no quotes"),
					"UuQ",	N_("URIs of all selected, unselect, no quotes"),
					"ud",	N_("URI of first selected (destination pane)"),
					"Ic:\"label\"=\"choice1\",...",	N_("Input combo box"),
					"Im:\"label\"=\"text1:choice1\",...",	N_("Input using menu"),
					"Is:\"label\"=\"default\"",	N_("Input string"),
					"Ix:\"label\"",			N_("Input check button (gives TRUE or FALSE)"),
					"It:\"label\"",			N_("Add label to input window"),
					"It:\"-\"",			N_("Add a separator bar to input window"),
					N_("$NAME"),			N_("Value of $NAME (environment)"),
					"#",				N_("gentoo's PID"),
					N_("~NAME"),			N_("Home directory for user NAME"),
					NULL };
	GtkTreeIter	iter;
	guint		i;

	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	for(i = 0; code[i] != NULL; i+= 2)
	{
		gtk_list_store_insert_with_values(store, &iter, -1, 0, code[i], 1, code[i + 1], -1);
	}
	view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), GTK_SELECTION_BROWSE);
	cr = gtk_cell_renderer_text_new();
	vc = gtk_tree_view_column_new_with_attributes("(code)", cr, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), vc);
	vc = gtk_tree_view_column_new_with_attributes("(description)", cr, "text", 1, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), vc);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
	gtk_widget_set_size_request(view, 448, 320);
	gtk_container_add(GTK_CONTAINER(scwin), view);

	dlg = dlg_dialog_sync_new(scwin, _("Pick Code"), NULL);
	if(dlg_dialog_sync_wait(dlg) == DLG_POSITIVE)
	{
		if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), NULL, &iter))
		{
			gchar	*code = NULL, buf[1024];

			gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 0, &code, -1);
			if(code != NULL)
			{
				gint	pos;
				gsize	len;

				if(*code == '\\')
					len = g_snprintf(buf, sizeof buf, "%s", code);
				else
					len = g_snprintf(buf, sizeof buf, " {%s}", code);
				pos = gtk_entry_get_text_length(GTK_ENTRY(page->ddef));
				gtk_editable_insert_text(GTK_EDITABLE(page->ddef), buf, len, &pos);
				g_free(code);
			}
		}
	}
	dlg_dialog_sync_destroy(dlg);
}

/* 1998-09-27 -	The details (formerly "...") button has been clicked; pop up a quick-selection
**		window where the user can select something s?he's too lazy to type.
*/
static void evt_pick_clicked(GtkWidget *wid, gpointer user)
{
	P_CmdSeq	*page = user;
	CmdRow		*row;

	if((row = cmdrow_get_selected(page, NULL)) == NULL)
		return;

	if(row->type == CRTP_BUILTIN)
		pick_builtin(page);
	else if(row->type == CRTP_EXTERNAL)
		pick_external(page);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-27 -	Add a new command sequence to the happy bunch. */
static void evt_add_clicked(GtkWidget *wid, gpointer user)
{
	P_CmdSeq	*page = user;
	CmdSeq		*ns;
	GtkTreeIter	iter;
	GtkTreePath	*path;

	if((ns = csq_cmdseq_new(csq_cmdseq_unique_name(page->cmdseq), 0UL)) == NULL)
		return;
	csq_cmdseq_hash(&page->cmdseq, ns);
	gtk_list_store_insert_with_values(page->store, &iter, -1,
				CMDSEQ_COLUMN_NAME, ns->name,
				CMDSEQ_COLUMN_CMDSEQ, ns,
				-1);
	gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->view)), &iter);
	path = gtk_tree_model_get_path(GTK_TREE_MODEL(page->store), &iter);
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(page->view), path, NULL, TRUE, 0.5f, 0.0f);
	gtk_tree_path_free(path);

	gtk_editable_select_region(GTK_EDITABLE(page->name), 0, -1);
	gtk_widget_grab_focus(page->name);
}

/* 1998-09-27 -	Delete the currently selected command sequence. */
static void evt_del_clicked(GtkWidget *wid, gpointer user)
{
	P_CmdSeq	*page = user;
	CmdSeq		*cs;
	GtkTreeIter	iter;

	if((cs = cmdseq_get_selected(page, &iter)) != NULL)
	{
		g_hash_table_remove(page->cmdseq, cs->name);
		csq_cmdseq_destroy(cs);
		gtk_list_store_remove(page->store, &iter);
		reset_widgets(page);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-26 -	Build the (currently very pointless) extra controls for builtin rows. */
static void build_px_builtin(P_CmdSeq *page, PX_Builtin *pxb)
{
	pxb->vbox  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	pxb->label = gtk_label_new(_("(No Options Available)"));
	gtk_box_pack_start(GTK_BOX(pxb->vbox), pxb->label, TRUE, TRUE, 0);
}

/* 1998-09-27 -	One of the general flag check buttons was clicked. */
static void evt_gf_clicked(GtkWidget *wid, gpointer user)
{
	P_CmdSeq	*page = user;
	guint32		flags = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(wid), "user"));
	CmdSeq		*cs;
	CmdRow		*row;

	if(!get_selections(page, &cs, &row))
		return;

	if(row->type == CRTP_EXTERNAL)
	{
		if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)))
			row->extra.external.gflags |= flags;
		else
			row->extra.external.gflags &= ~flags;
		gtk_widget_set_sensitive(page->px_external.gflags.killprev, row->extra.external.gflags & CGF_RUNINBG);
		gtk_widget_set_sensitive(page->px_external.gflags.survive, row->extra.external.gflags & CGF_RUNINBG);
		set_repeat(page);
		page->modified = TRUE;
	}
}

/* 1998-09-26 -	Build the page for external commands that deals with general flags. */
static void build_pxe_general(P_CmdSeq *page, PXE_General *pxg)
{
	GtkWidget	*hbox;

	pxg->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	pxg->runbg = gtk_check_button_new_with_label(_("Run in Background?"));
	g_object_set_data(G_OBJECT(pxg->runbg), "user", GINT_TO_POINTER(CGF_RUNINBG));
	g_signal_connect(G_OBJECT(pxg->runbg), "clicked", G_CALLBACK(evt_gf_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), pxg->runbg, FALSE, FALSE, 0);
	pxg->killprev = gtk_check_button_new_with_label(_("Kill Previous Instance?"));
	g_object_set_data(G_OBJECT(pxg->killprev), "user", GINT_TO_POINTER(CGF_KILLPREV));
	g_signal_connect(G_OBJECT(pxg->killprev), "clicked", G_CALLBACK(evt_gf_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), pxg->killprev, FALSE, FALSE, 0);
	pxg->survive = gtk_check_button_new_with_label(_("Survive Quit?"));
	g_object_set_data(G_OBJECT(pxg->survive), "user", GINT_TO_POINTER(CGF_SURVIVE));
	g_signal_connect(G_OBJECT(pxg->survive), "clicked", G_CALLBACK(evt_gf_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), pxg->survive, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(pxg->vbox), hbox, FALSE, FALSE, 0);

	pxg->graboutput = gtk_check_button_new_with_label(_("Capture Output?"));
	g_object_set_data(G_OBJECT(pxg->graboutput), "user", GINT_TO_POINTER(CGF_GRABOUTPUT));
	g_signal_connect(G_OBJECT(pxg->graboutput), "clicked", G_CALLBACK(evt_gf_clicked), page);
	gtk_box_pack_start(GTK_BOX(pxg->vbox), pxg->graboutput, FALSE, FALSE, 0);
}

/* 2005-01-31 -	Handle clicks on source & destination selection requirement toggle buttons, and change flags. */
static void evt_baf_reqsel_clicked(GtkWidget *wid, gpointer user)
{
	P_CmdSeq	*page = user;
	guint32		mask;
	CmdRow		*row;

	if(!get_selections(page, NULL, &row))
		return;

	mask = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(wid), "user"));
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)))
		row->extra.external.baflags[0] |= mask;
	else
		row->extra.external.baflags[0] &= ~mask;
}

/* 1999-03-09 -	One of the CD check buttons was clicked. Do the pseudo-radio logic, and alter the
**		flag setting of the current command row. Note that this is kind'a sneakily written
**		(some would say ugly (uglily?)), since it actually relies on being called twice for
**		a single click in some cases (since it modifies the "other" widget, thus causing an
**		event).
*/
static void evt_baf_cd_clicked(GtkWidget *wid, gpointer user)
{
	P_CmdSeq	*page = user;
	CmdRow		*row;
	GtkWidget	*other;
	guint32		mask;

	if(!get_selections(page, NULL, &row))
		return;

	/* Figure out if we're in source or dest (same handler). */
	if(g_object_get_data(G_OBJECT(wid), "destination"))
	{
		other = page->px_external.bf.cdsrc;
		mask  = CBAF_CD_DEST;
	}
	else
	{
		other = page->px_external.bf.cddst;
		mask  = CBAF_CD_SOURCE;
	}
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)))
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(other), FALSE);
		row->extra.external.baflags[0] |= mask;
	}
	else
		row->extra.external.baflags[0] &= ~mask;
}

/* 1999-03-09 -	Handle click on one of the rescanning check buttons. */
static void evt_baf_rescan_clicked(GtkWidget *wid, gpointer user)
{
	P_CmdSeq	*page = user;
	CmdRow		*row;
	guint32		mask = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "user"));

	if(!get_selections(page, NULL, &row))
		return;

	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)))
		row->extra.external.baflags[1] |= mask;
	else
		row->extra.external.baflags[1] &= ~mask;
}

/* 1999-03-09 -	Build new simplified widgetry for editing before and after flags. Note that the actual
**		flags have not changed, they're still muy redundant-capable. It's just the GUI that's
**		become a bit simpler to look at. At least less vertically tall.
*/
static void build_px_bf(P_CmdSeq *page, PXE_BF *pxbf)
{
	GtkWidget	*grid;

	pxbf->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	grid = gtk_grid_new();
	gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
	gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
	pxbf->reqsel_src = gtk_check_button_new_with_label(_("Require Source Selection?"));
	g_object_set_data(G_OBJECT(pxbf->reqsel_src), "user", GINT_TO_POINTER(CBAF_REQSEL_SOURCE));
	g_signal_connect(G_OBJECT(pxbf->reqsel_src), "clicked", G_CALLBACK(evt_baf_reqsel_clicked), page);
	gtk_grid_attach(GTK_GRID(grid), pxbf->reqsel_src, 0, 0, 1, 1);
	pxbf->reqsel_dst = gtk_check_button_new_with_label(_("Require Destination Selection?"));
	g_object_set_data(G_OBJECT(pxbf->reqsel_dst), "user", GINT_TO_POINTER(CBAF_REQSEL_DEST));
	g_signal_connect(G_OBJECT(pxbf->reqsel_dst), "clicked", G_CALLBACK(evt_baf_reqsel_clicked), page);
	gtk_grid_attach(GTK_GRID(grid), pxbf->reqsel_dst, 1, 0, 1, 1);

	pxbf->cdsrc = gtk_check_button_new_with_label(_("CD Source?"));
	g_signal_connect(G_OBJECT(pxbf->cdsrc), "clicked", G_CALLBACK(evt_baf_cd_clicked), page);
	gtk_grid_attach(GTK_GRID(grid), pxbf->cdsrc, 0, 1, 1, 1);
	pxbf->cddst = gtk_check_button_new_with_label(_("CD Destination?"));
	g_object_set_data(G_OBJECT(pxbf->cddst), "destination", GINT_TO_POINTER(1));	/* Just a silly flag. */
	g_signal_connect(G_OBJECT(pxbf->cddst), "clicked", G_CALLBACK(evt_baf_cd_clicked), page);
	gtk_grid_attach(GTK_GRID(grid), pxbf->cddst, 1, 1, 1, 1);
	gtk_box_pack_start(GTK_BOX(pxbf->vbox), grid, TRUE, TRUE, 0);
}

static void build_px_af(P_CmdSeq *page, PXE_AF *pxaf)
{
	GtkWidget	*hbox;

	pxaf->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	pxaf->rssrc = gtk_check_button_new_with_label(_("Rescan Source?"));
	g_object_set_data(G_OBJECT(pxaf->rssrc), "user", GINT_TO_POINTER(CBAF_RESCAN_SOURCE));
	g_signal_connect(G_OBJECT(pxaf->rssrc), "clicked", G_CALLBACK(evt_baf_rescan_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), pxaf->rssrc, TRUE, TRUE, 0);
	pxaf->rsdst = gtk_check_button_new_with_label(_("Rescan Destination?"));
	g_object_set_data(G_OBJECT(pxaf->rsdst), "user", GINT_TO_POINTER(CBAF_RESCAN_DEST));
	g_signal_connect(G_OBJECT(pxaf->rsdst), "clicked", G_CALLBACK(evt_baf_rescan_clicked), page);
	gtk_box_pack_start(GTK_BOX(hbox), pxaf->rsdst, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(pxaf->vbox), hbox, FALSE, FALSE, 0);
}

/* 1998-09-26 -	Build the extra configuration widgetry for external commands. Plenty of stuff. */
static void build_px_external(P_CmdSeq *page, PX_External *pxe)
{
	pxe->nbook = gtk_notebook_new();
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(pxe->nbook), GTK_POS_LEFT);
	build_pxe_general(page, &pxe->gflags);
	gtk_notebook_append_page(GTK_NOTEBOOK(pxe->nbook), pxe->gflags.vbox, gtk_label_new(_("General")));

	build_px_bf(page, &pxe->bf);
	gtk_notebook_append_page(GTK_NOTEBOOK(pxe->nbook), pxe->bf.vbox, gtk_label_new(_("Before")));
	build_px_af(page, &pxe->af);
	gtk_notebook_append_page(GTK_NOTEBOOK(pxe->nbook), pxe->af.vbox, gtk_label_new(_("After")));
}

static GtkWidget * ccs_init(MainInfo *min, gchar **name)
{
	const gchar	*tlab[] = { N_("Built-In"), N_("External"), NULL },
			*dblab[] = { N_("Add Row"), N_("Duplicate"), N_("Delete Row") };
	GCallback	dbfunc[] = { G_CALLBACK(evt_addrow_clicked), G_CALLBACK(evt_duprow_clicked), G_CALLBACK(evt_delrow_clicked) };
	guint			i;
	GtkWidget		*scwin, *label, *vbox, *hbox, *vbox2, *sep, *paned;
	GtkCellRenderer		*cr;
	GtkTreeViewColumn	*vc;
	P_CmdSeq		*page = &the_page;

	page->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	page->store = gtk_list_store_new(CMDSEQ_COLUMN_COUNT, G_TYPE_STRING, G_TYPE_POINTER);
	/* Make sure the cmdseq list is sorted on name. This is rather easy, nowadays. */
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(page->store), CMDSEQ_COLUMN_NAME, GTK_SORT_ASCENDING);
	page->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(page->store));
	cr = gtk_cell_renderer_text_new();
	vc = gtk_tree_view_column_new_with_attributes("(name)", cr, "text", CMDSEQ_COLUMN_NAME, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(page->view), vc);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(page->view), FALSE);
	g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->view))), "changed", G_CALLBACK(evt_cmdseq_selection_changed), page);
	gtk_container_add(GTK_CONTAINER(scwin), page->view);
	gtk_widget_set_size_request(scwin, -1, 100);
	gtk_box_pack_start(GTK_BOX(vbox), scwin, TRUE, TRUE, 0);

	page->nhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new(_("Name"));
	gtk_box_pack_start(GTK_BOX(page->nhbox), label, FALSE, FALSE, 0);
	page->name = gtk_entry_new();
	gtk_entry_set_max_length(GTK_ENTRY(page->name), CSQ_NAME_SIZE - 1);
	g_signal_connect(G_OBJECT(page->name), "changed", G_CALLBACK(evt_name_changed), page);
	gtk_box_pack_start(GTK_BOX(page->nhbox), page->name, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), page->nhbox, FALSE, FALSE, 0);
	gtk_paned_pack1(GTK_PANED(paned), vbox, TRUE, TRUE);


	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	page->dframe = gtk_frame_new(_("Definition"));

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	vbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	for(i = 0; i < sizeof page->dbtn / sizeof page->dbtn[0]; i++)
	{
		page->dbtn[i] = gtk_button_new_with_label(_(dblab[i]));
		g_signal_connect(G_OBJECT(page->dbtn[i]), "clicked", G_CALLBACK(dbfunc[i]), page);
		gtk_box_pack_start(GTK_BOX(vbox2), page->dbtn[i], FALSE, FALSE, 0);
	}
	gtk_box_pack_start(GTK_BOX(hbox), vbox2, FALSE, FALSE, 0);

	vbox2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	page->dstore = gtk_list_store_new(DEF_COLUMN_COUNT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
	page->sig_delete = g_signal_connect(G_OBJECT(page->dstore), "row_deleted", G_CALLBACK(evt_row_deleted), page);
	page->dview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(page->dstore));
	vc = gtk_tree_view_column_new_with_attributes("(type)", cr, "text", DEF_COLUMN_TYPE, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(page->dview), vc);
	vc = gtk_tree_view_column_new_with_attributes("(type)", cr, "text", DEF_COLUMN_DEF, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(page->dview), vc);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(page->dview), FALSE);
	gtk_tree_view_set_reorderable(GTK_TREE_VIEW(page->dview), TRUE);
	g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->dview))), "changed", G_CALLBACK(evt_cmdrow_selection_changed), page);
	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scwin), page->dview);
	gtk_widget_set_size_request(scwin, -1, 100);
	gtk_box_pack_start(GTK_BOX(vbox2), scwin, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vbox2, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 5);

	page->dhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	page->dtype = gui_build_combo_box(tlab, G_CALLBACK(evt_type_selected), page);
	gtk_box_pack_start(GTK_BOX(page->dhbox), page->dtype, FALSE, FALSE, 0);
	page->ddef = gtk_entry_new();
	g_signal_connect(G_OBJECT(page->ddef), "changed", G_CALLBACK(evt_def_changed), page);
	gtk_box_pack_start(GTK_BOX(page->dhbox), page->ddef, TRUE, TRUE, 0);
	page->dpick = gui_details_button_new();
	g_signal_connect(G_OBJECT(page->dpick), "clicked", G_CALLBACK(evt_pick_clicked), page);
	gtk_box_pack_start(GTK_BOX(page->dhbox), page->dpick, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), page->dhbox, FALSE, FALSE, 0);

	page->dextra = gtk_notebook_new();
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(page->dextra), FALSE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(page->dextra), FALSE);

	build_px_builtin(page, &page->px_builtin);
	gtk_notebook_append_page(GTK_NOTEBOOK(page->dextra), page->px_builtin.vbox, NULL);

	build_px_external(page, &page->px_external);
	gtk_notebook_append_page(GTK_NOTEBOOK(page->dextra), page->px_external.nbook, NULL);
	gtk_box_pack_start(GTK_BOX(vbox), page->dextra, FALSE, FALSE, 0);

	sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 5);
	page->drepeat = gtk_check_button_new_with_label(_("Repeat Sequence Until No Source Selection?"));
	g_signal_connect(G_OBJECT(page->drepeat), "clicked", G_CALLBACK(evt_repeat_clicked), page);
	gtk_box_pack_start(GTK_BOX(vbox), page->drepeat, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(page->dframe), vbox);
	gtk_paned_pack2(GTK_PANED(paned), page->dframe, TRUE, TRUE);
	gtk_box_pack_start(GTK_BOX(page->vbox), paned, TRUE, TRUE, 0);

	page->bhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	page->badd = gtk_button_new_with_label(_("Add"));
	g_signal_connect(G_OBJECT(page->badd), "clicked", G_CALLBACK(evt_add_clicked), page);
	gtk_box_pack_start(GTK_BOX(page->bhbox), page->badd, TRUE, TRUE, 5);
	page->bdel = gtk_button_new_with_label(_("Delete"));
	g_signal_connect(G_OBJECT(page->bdel), "clicked", G_CALLBACK(evt_del_clicked), page);
	gtk_box_pack_start(GTK_BOX(page->bhbox), page->bdel, TRUE, TRUE, 5);
	gtk_box_pack_start(GTK_BOX(page->vbox), page->bhbox, FALSE, FALSE, 5);

	gtk_widget_show_all(page->vbox);

	cfg_tree_level_begin(_("Commands"));
	cfg_tree_level_append(_("Definitions"), page->vbox);	/* Rely on cfg_cmdcfg.c to close level. */
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-26 -	Update command sequence page. */
static void ccs_update(MainInfo *min)
{
	the_page.min = min;

	populate_list(min, &the_page);
	reset_widgets(&the_page);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-27 -	Just a simple g_hash_table_foreach() callback. */
static void destroy_cmdseq(gpointer key, gpointer data, gpointer user)
{
	csq_cmdseq_destroy((CmdSeq *) data);
}

/* 1998-09-27 -	Accept any changes, and put them in the "real" config. */
static void ccs_accept(MainInfo *min)
{
	P_CmdSeq	*page = &the_page;

	if(page->modified)
	{
		if(min->cfg.commands.cmdseq != NULL)
		{
			g_hash_table_foreach(min->cfg.commands.cmdseq, destroy_cmdseq, NULL);
			g_hash_table_destroy(min->cfg.commands.cmdseq);
		}
		min->cfg.commands.cmdseq = page->cmdseq;
		page->cmdseq = NULL;
		page->modified = FALSE;
	}
}

/* ----------------------------------------------------------------------------------------- */

static void save_external(CX_Ext *cext, FILE *out)
{
	xml_put_node_open(out, "CX_External");
	xml_put_integer(out, "gflags", cext->gflags);
	xml_put_integer(out, "bflags", cext->baflags[0]);
	xml_put_integer(out, "aflags", cext->baflags[1]);
	xml_put_node_close(out, "CX_External");
}

/* 1998-09-27 -	Save a command row. */
static void save_cmdrow(CmdRow *row, FILE *out)
{
	xml_put_node_open(out, "CmdRow");
	xml_put_text(out, "type", csq_cmdrow_type_to_string(row->type));
	xml_put_text(out, "def", row->def->str);
	xml_put_integer(out, "flags", row->flags);
	switch(row->type)
	{
		case CRTP_BUILTIN:
			break;
		case CRTP_EXTERNAL:
			save_external(&row->extra.external, out);
			break;
		default:
			break;
	}
	xml_put_node_close(out, "CmdRow");
}

/* 1998-09-27 -	Save out a single command sequence. */
static void save_cmdseq(gpointer key, gpointer data, gpointer user)
{
	CmdSeq	*cs = data;
	FILE	*out = user;
	GList	*iter;

	xml_put_node_open(out, "CmdSeq");
	xml_put_text(out, "name", cs->name);
	xml_put_integer(out, "flags", cs->flags);
	if(cs->rows != NULL)			/* Any rows in this sequence? */
	{
		xml_put_node_open(out, "CmdRows");
		for(iter = cs->rows; iter != NULL; iter = g_list_next(iter))
			save_cmdrow(iter->data, out);
		xml_put_node_close(out, "CmdRows");
	}
	xml_put_node_close(out, "CmdSeq");
}

/* 1998-09-28 -	Save the command sequence config data right out of min->cfg. */
static gint ccs_save(MainInfo *min, FILE *out)
{
	xml_put_node_open(out, NODE);
	if(min->cfg.commands.cmdseq != NULL)
		g_hash_table_foreach(min->cfg.commands.cmdseq, save_cmdseq, out);
	xml_put_node_close(out, NODE);
	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-27 -	Load extra data for external command. */
static void load_external(CX_Ext *cext, const XmlNode *node)
{
	xml_get_integer(node, "gflags", (gint *) &cext->gflags);
	xml_get_integer(node, "bflags", (gint *) &cext->baflags[0]);
	xml_get_integer(node, "aflags", (gint *) &cext->baflags[1]);
}

/* 1998-09-27 -	Load a single command sequence row. */
static void load_cmdrow(const XmlNode *node, gpointer user)
{
	CmdSeq		*cs = user;
	CmdRow		*row;
	const XmlNode	*data;
	CRType		type = CRTP_BUILTIN;
	const gchar	*def = NULL, *tname = NULL;
	guint32		flags = 0UL;

	if(xml_get_text(node, "type", &tname))
		type = csq_cmdrow_string_to_type(tname);
	xml_get_text(node, "def", &def);
	xml_get_integer(node, "flags", (gint *) &flags);

	if((row = csq_cmdrow_new(type, def, flags)) != NULL)
	{
		if((row->type == CRTP_EXTERNAL) && (data = xml_tree_search(node, "CX_External")) != NULL)
			load_external(&row->extra.external, data);
		csq_cmdseq_row_append(cs, row);
	}
}

/* 1998-09-27 -	Trampoline. */
static void load_cmdseq_rows(const XmlNode *node, CmdSeq *cs)
{
	xml_node_visit_children(node, load_cmdrow, cs);
}

/* 1998-09-27 -	Load a command sequence. */
static void load_cmdseq(const XmlNode *node, gpointer user)
{
	MainInfo	*min = user;
	const gchar	*name = "Unknown";
	guint32		flags = 0UL;
	CmdSeq		*cs;
	const XmlNode	*data;

	xml_get_text(node, "name", &name);
	xml_get_integer(node, "flags", (gint *) &flags);
	if((cs = csq_cmdseq_new(name, flags)) != NULL)
	{
		if((data = xml_tree_search(node, "CmdRows")) != NULL)
			load_cmdseq_rows(data, cs);
		csq_cmdseq_hash(&min->cfg.commands.cmdseq, cs);
	}
}

/* 1998-09-27 -	Load a tree full of command sequence data. */
static void ccs_load(MainInfo *min, const XmlNode *node)
{
	/* First destroy any existing commands. */
	if(min->cfg.commands.cmdseq != NULL)
	{
		g_hash_table_foreach(min->cfg.commands.cmdseq, destroy_cmdseq, NULL);
		g_hash_table_destroy(min->cfg.commands.cmdseq);
	}
	min->cfg.commands.cmdseq = NULL;
	xml_node_visit_children(node, load_cmdseq, min);
}

/* ----------------------------------------------------------------------------------------- */

const CfgModule * ccs_describe(MainInfo *min)
{
	static const CfgModule	desc = { NODE, ccs_init, ccs_update, ccs_accept, ccs_save, ccs_load, NULL };

	return &desc;
}

/* 1998-09-28 -	This returns a pointer to the current set of command sequnces hash. Completely
**		lethal if used in the wrong way, but very useful when selecting cmdseqs in
**		the config.
*/
GHashTable * ccs_get_current(void)
{
	return the_page.cmdseq;
}

/* 2014-12-26 -	Goto the named command sequence in the main list. Used by Style configuration editor. */
gboolean ccs_goto_cmdseq(const char *name)
{
	GtkTreeIter	iter;

	if(!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(the_page.store), &iter))
		return FALSE;
	while(TRUE)
	{
		gchar	*name_here;
		gtk_tree_model_get(GTK_TREE_MODEL(the_page.store), &iter, CMDSEQ_COLUMN_NAME, &name_here, -1);
		if(strcmp(name_here, name) == 0)
		{
			GtkTreePath	*path = gtk_tree_model_get_path(GTK_TREE_MODEL(the_page.store), &iter);
			gtk_tree_view_set_cursor(GTK_TREE_VIEW(the_page.view), path, NULL, FALSE);
			gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(the_page.view), path, NULL, FALSE, 0, 0);
			gtk_tree_path_free(path);
			return TRUE;
		}
		if(!gtk_tree_model_iter_next(GTK_TREE_MODEL(the_page.store), &iter))
			break;
	}
	return FALSE;
}
