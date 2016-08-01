/*
** 1998-08-12 -	The file typing systems seems to work OK; better give it a GUI too.
** 1999-05-27 -	Changed linkage between this module and cfg_styles. Somewhat cleaner now.
*/

#include "gentoo.h"

#include <stdlib.h>

#include "types.h"
#include "styles.h"
#include "style_dialog.h"
#include "strutil.h"
#include "guiutil.h"
#include "xmlutil.h"
#include "dirpane.h"
#include "iconutil.h"

#include "configure.h"
#include "cfg_module.h"
#include "cfg_styles.h"

#include "cfg_types.h"

#define	NODE	"FileTypes"

/* ----------------------------------------------------------------------------------------- */

typedef struct {		/* Just a little helper structure for the identification. */
	GtkWidget	*check;
	GtkWidget	*entry;
	GtkWidget	*glob;		/* For the name and file RE, this gives glob->RE translation. */
	GtkWidget	*nocase;	/* Do case-insensitive RE matching? */
} CEntry;

typedef struct {
	GtkWidget	*vbox;		/* The usual mandatory root vbox. */
	GuiHandlerGroup	*handlers;	/* Collects editing widgets, for signal blocking. */
	GtkListStore	*store;		/* Tree model holding existing types. */
	GtkWidget	*view;		/* Tree view. */

	GtkWidget	*ngrid;		/* Grid holding the type name widgetry. */
	GtkWidget	*name;		/* Entry widget for type name. */

	GtkWidget	*iframe;	/* Identification frame. */
	GtkWidget	*igrid;		/* This holds all the identification widgets. */
	GtkWidget	*ithbox;	/* Hbox for the 'type' id row. */
	GtkWidget	*itype[7];	/* Radio buttons for the type selection. Boring. */
	GSList		*itlist;	/* Radio button grouping list. */
	mode_t		*itmode;	/* The actual mode values (e.g. S_IFREG etc) for types. */
	GtkWidget	*irperm;	/* Require permissions match? */
	GtkWidget	*iphbox;	/* Hbox for all the toggle buttons. */
	GtkWidget	*iperm[6];	/* Toggle buttons for permissions (set uid, gid, sticky, read, write, execute). */
	CEntry		ident[3];	/* The suffix, name and 'file' id widgets. */

	GtkWidget	*sframe;	/* Style frame. */
	GtkWidget	*shbox;		/* Just something to hold the style stuff. */
	GtkWidget	*sbtn;		/* Button to change style (also displays current). */
	GtkWidget	*sbhbox;	/* Hbox containing _button_ contents. */
	GtkWidget	*sicon;		/* A pixmap displayed inside the "sbtn" button. */
	GtkWidget	*slabel;	/* A "loose" label displayed inside the "sbtn" button. */

	GtkWidget	*bhbox;		/* A box for the "Add", "Up", "Down" & "Delete" buttons. */
	GtkWidget	*add, *up,	/* And here are the buttons themselves. */
			*down, *del;

	MainInfo	*min;
	GList		*type;		/* List of editing types. */
	FType		*curr_type;	/* Currently selected type. */
	gboolean	modified;	/* Did user change anything? */
} P_Types;

enum {
	COLUMN_NAME,
	COLUMN_TYPE,
	COLUMN_COUNT
};

/* ----------------------------------------------------------------------------------------- */

static P_Types	the_page;

/* ----------------------------------------------------------------------------------------- */

/* 2009-03-15 -	Helper routine, aiming to remove the 'curr_type' field in the page since it
**		feels a bit smelly. This just queries the tree view for the selected row,
**		and extracts the type column which is returned. The iter to the selection is
**		returned too, if non-NULL. If no selection exists, NULL is returned.
*/
static FType * get_current_type(P_Types *page, GtkTreeIter *iter)
{
	if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->view)), NULL, iter))
	{
		FType	*type = NULL;

		gtk_tree_model_get(GTK_TREE_MODEL(page->store), iter, COLUMN_TYPE, &type, -1);
		return type;
	}
	return NULL;
}

static void set_movement_widgets(P_Types *page, GtkTreeIter *iter)
{
	gchar	*istr;

	/* Set Up/Down arrow states depending on index of current selection.
	** Ask GTK+ for the numeric "address" of the current selection, then figure it out.
	*/
	if((istr = gtk_tree_model_get_string_from_iter(GTK_TREE_MODEL(page->store), iter)) != NULL)
	{
		gint	n = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(page->store), NULL), m;

		m = strtol(istr, NULL, 10);
		gtk_widget_set_sensitive(page->up, m > 0 && m < n - 1);
		gtk_widget_set_sensitive(page->down, m < n - 2);
		g_free(istr);
	}
}

/* 1998-08-13 -	Set the widgets so they reflect the settings for the given file type. */
static void set_widgets(P_Types *page, FType *tpe, GtkTreeIter *iter)
{
	gchar	*itext[3];
	gint	iflags[] = { FTFL_REQSUFFIX, FTFL_NAMEMATCH, FTFL_FILEMATCH },
		pflags[] = { FTPM_SETUID, FTPM_SETGID, FTPM_STICKY, FTPM_READ, FTPM_WRITE, FTPM_EXECUTE },
		igflags[] = { 0, FTFL_NAMEGLOB, FTFL_FILEGLOB },
		incflags[] = { 0, FTFL_NAMENOCASE, FTFL_FILENOCASE }, i;

	if(tpe == NULL)
		return;

	/* Block all signal handlers, to ignore the changed-events. */
	gui_handler_group_block(page->handlers);

	itext[0] = tpe->suffix;
	itext[1] = tpe->name_re_src;
	itext[2] = tpe->file_re_src;

	gtk_entry_set_text(GTK_ENTRY(page->name), tpe->name);
	gtk_widget_set_sensitive(page->ngrid, TRUE);

	for(i = 0; i < 7; i++)
	{
		if(page->itmode[GPOINTER_TO_INT(g_object_get_data(G_OBJECT(page->itype[i]), "user"))] == tpe->mode)
		{
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->itype[i]), TRUE);
			break;
		}
	}

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->irperm), tpe->flags & FTFL_REQPERM);
	for(i = 0; i < 6; i++)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->iperm[i]), tpe->perm & pflags[i]);
	gtk_widget_set_sensitive(page->iphbox, tpe->flags & FTFL_REQPERM);

	for(i = 0; i < 3; i++)
	{
		gtk_entry_set_text(GTK_ENTRY(page->ident[i].entry), itext[i]);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->ident[i].check), tpe->flags & iflags[i]);
		gtk_widget_set_sensitive(page->ident[i].entry, tpe->flags & iflags[i]);
		if(i >= 1)
		{
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->ident[i].glob), tpe->flags & igflags[i]);
			gtk_widget_set_sensitive(page->ident[i].glob, tpe->flags & iflags[i]);
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->ident[i].nocase), tpe->flags & incflags[i]);
			gtk_widget_set_sensitive(page->ident[i].nocase, tpe->flags & iflags[i]);
		}
	}
	gtk_widget_set_sensitive(page->iframe, tpe->mode != 0);

	if(tpe->style != NULL)
	{
		GdkPixbuf	*pbuf;
		gchar		buf[128];
		const gchar	*iname;

		if((iname = stl_style_property_get_icon(tpe->style, SPN_ICON_UNSEL)) != NULL)
		{
			if((pbuf = ico_icon_get_pixbuf(page->min, iname)) != NULL)
			{
				if(page->sicon == NULL)
				{
					page->sicon = gtk_image_new_from_pixbuf(pbuf);
					gtk_box_pack_start(GTK_BOX(page->sbhbox), page->sicon, FALSE, FALSE, 10);
					gtk_box_reorder_child(GTK_BOX(page->sbhbox), page->sicon, 0);
					gtk_widget_show(page->sicon);
				}
				else
					gtk_image_set_from_pixbuf(GTK_IMAGE(page->sicon), pbuf);
			}
		}
		g_snprintf(buf, sizeof buf, _("%s - Click to Change..."), stl_style_get_name(tpe->style));
		gtk_label_set_text(GTK_LABEL(page->slabel), buf);
	}
	gtk_widget_set_sensitive(page->sframe, TRUE);
	gtk_widget_set_sensitive(page->del, tpe->mode != 0);
	set_movement_widgets(page, iter);
	gui_handler_group_unblock(page->handlers);
}

/* 1998-08-13 -	Reset the state of all the type-editing widgets. */
static void reset_widgets(P_Types *page)
{
	gint	i;

	page->curr_type = NULL;
	gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->view)));
	gtk_entry_set_text(GTK_ENTRY(page->name), "");
	gtk_widget_set_sensitive(page->ngrid, FALSE);

	gtk_widget_set_sensitive(page->iphbox, FALSE);
	for(i = 0; i < 6; i++)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->iperm[i]), FALSE);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->itype[0]), TRUE);
	for(i = 0; i < 3; i++)
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->ident[i].check), FALSE);
		gtk_entry_set_text(GTK_ENTRY(page->ident[i].entry), "");
		if(i >= 1)
		{
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->ident[i].glob), FALSE);
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(page->ident[i].nocase), FALSE);
		}
	}
	gtk_widget_set_sensitive(page->iframe, FALSE);

	if(page->sicon != NULL)
	{
		gtk_widget_destroy(page->sicon);
		page->sicon = NULL;
	}
	gtk_label_set_text(GTK_LABEL(page->slabel), _("(None)"));
	gtk_widget_set_sensitive(page->sframe, FALSE);

	gtk_widget_set_sensitive(page->up, FALSE);
	gtk_widget_set_sensitive(page->down, FALSE);
	gtk_widget_set_sensitive(page->del, FALSE);
}

/* 1998-12-13 -	Broke out the actual list updating code, since it's now needed more. */
static void update_list(P_Types *page)
{
	const GList	*iter;

	gtk_list_store_clear(page->store);
	for(iter = page->type; iter != NULL; iter = g_list_next(iter))
	{
		GtkTreeIter	ti;
		gtk_list_store_insert_with_values(page->store, &ti, -1, COLUMN_NAME, ((FType *) iter->data)->name, COLUMN_TYPE, iter->data, -1);
	}
}

/* 2009-03-15 -	Old code refactored a bit. Just copy an FType and insert in page's editing list. */
static void copy_type(gpointer data, gpointer user)
{
	P_Types	*page = user;
	FType	*tpe = data, *nt;

	if(tpe == NULL)
		return;

	if((nt = typ_type_copy(tpe)) != NULL)
		page->type = typ_type_insert(page->type, NULL, nt);
}

/* 1998-08-13 -	Copy the current type definitions and display the copies in the main list. */
static void populate_list(MainInfo *min, P_Types *page)
{
	reset_widgets(page);

	g_list_foreach(min->cfg.type, copy_type, page);
	update_list(page);
}

/* ----------------------------------------------------------------------------------------- */

/* 2009-03-15 -	Selection changed in main type list, so update editing widgetry. */
static void evt_selection_changed(GtkTreeSelection *ts, gpointer user)
{
	P_Types		*page = user;
	GtkTreeIter	iter;
	FType		*type = NULL;

	if((type = get_current_type(page, &iter)) != NULL)
	{
		set_widgets(page, type, &iter);
		page->curr_type = type;
	}
	else
		reset_widgets(page);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-13 -	User is editing the name. Copy it as soon as it changes. Note that there is
**		no unique-ness requirement for type names. That simplifies things.
*/
static gint evt_name_changed(GtkWidget *wid, gpointer user)
{
	P_Types		*page = user;
	const gchar	*text;
	GtkTreeIter	iter;
	FType		*type = NULL;

	if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->view)), NULL, &iter))
		gtk_tree_model_get(GTK_TREE_MODEL(page->store), &iter, COLUMN_TYPE, &type, -1);
	if(type != NULL)
	{
		text = gtk_entry_get_text(GTK_ENTRY(wid));
		page->type = typ_type_set_name(page->type, type, text);
		gtk_list_store_set(page->store, &iter, COLUMN_NAME, text, -1);
		page->modified = TRUE;
	}
	return TRUE;
}

/* 1998-08-13 -	This is the callback for (all seven) mode check buttons. */
static gint evt_mode_clicked(GtkWidget *wid, gpointer user)
{
	P_Types	*page = user;
	gint	index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "user"));

	if(page->curr_type == NULL)
		return TRUE;

	page->modified = TRUE;

	page->curr_type->mode = page->itmode[index];

	return TRUE;
}

/* 1998-09-07 -	User hit the "require permissions" check button. */
static gint evt_id_reqperm_clicked(GtkWidget *wid, gpointer user)
{
	P_Types	*page = user;
	gint	state;

	if(page->curr_type == NULL)
		return TRUE;

	page->modified = TRUE;
	state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));

	gtk_widget_set_sensitive(page->iphbox, state);
	if(state)
		page->curr_type->flags |= FTFL_REQPERM;
	else
		page->curr_type->flags &= ~FTFL_REQPERM;

	return TRUE;
}

/* 1998-09-07 -	User clicked one of the permission toggle buttons. Remember. */
static gint evt_id_perm_clicked(GtkWidget *wid, gpointer user)
{
	P_Types	*page = user;
	gint	flag;

	if(page->curr_type == NULL)
		return TRUE;

	page->modified = TRUE;
	flag = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "user"));
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)))
		page->curr_type->perm |= flag;
	else
		page->curr_type->perm &= ~flag;

	return TRUE;
}

/* 1998-08-13 -	User clicked on of the check buttons for identification (suffix, name RE, 'file' RE). Act. */
static gint evt_id_check_clicked(GtkWidget *wid, gpointer user)
{
	P_Types	*page = user;
	gint	index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "user"));
	gint	iflags[] = { FTFL_REQSUFFIX, FTFL_NAMEMATCH, FTFL_FILEMATCH };
	gint	state;

	if(page->curr_type == NULL)
		return TRUE;

	page->modified = TRUE;

	state = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));

	gtk_widget_set_sensitive(page->ident[index].entry, state);
	if(state)
	{
		page->curr_type->flags |= iflags[index];
		gtk_widget_grab_focus(page->ident[index].entry);
	}
	else
		page->curr_type->flags &= ~iflags[index];
	if(index >= 1)
	{
		gtk_widget_set_sensitive(page->ident[index].glob, state);
		gtk_widget_set_sensitive(page->ident[index].nocase, state);
	}
	return TRUE;
}

static gint evt_id_entry_changed(GtkWidget *wid, gpointer user)
{
	P_Types		*page = user;
	const gchar	*text;
	gint		index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "user"));

	if(page->curr_type == NULL)
		return TRUE;

	page->modified = TRUE;

	text = gtk_entry_get_text(GTK_ENTRY(wid));

	/* This is soo crude... I guess the FStyle organization really sucks. :( */
	switch(index)
	{
		case 0:
			g_strlcpy(page->curr_type->suffix, text, sizeof page->curr_type->suffix);
			break;
		case 1:
			g_strlcpy(page->curr_type->name_re_src, text, sizeof page->curr_type->name_re_src);
			break;
		case 2:
			g_strlcpy(page->curr_type->file_re_src, text, sizeof page->curr_type->file_re_src);
			break;
	}
	return TRUE;
}

/* 1998-08-30 -	User just clicked the glob translation check button. Do the things. */
static gint evt_id_glob_changed(GtkWidget *wid, gpointer user)
{
	P_Types	*page = user;
	gint	index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "user"));
	gint	flag[] = { 0, FTFL_NAMEGLOB, FTFL_FILEGLOB };

	if(page->curr_type == NULL)
		return TRUE;

	page->modified = TRUE;
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)))
		page->curr_type->flags |= flag[index];
	else
		page->curr_type->flags &= ~flag[index];

	return TRUE;
}

/* 1998-09-15 -	User just clicked the nocase toggle button. React! */
static gint evt_id_nocase_changed(GtkWidget *wid, gpointer user)
{
	P_Types	*page = user;
	gint	index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "user"));
	gint	flag[] = { 0, FTFL_NAMENOCASE, FTFL_FILENOCASE };

	if(page->curr_type == NULL)
		return TRUE;

	page->modified = TRUE;
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)))
		page->curr_type->flags |= flag[index];
	else
		page->curr_type->flags &= ~flag[index];

	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-27 -	Select style for current type. Rewritten, to work with new style config module. */
static gint evt_style_clicked(GtkWidget *wid, gpointer user)
{
	P_Types		*page = user;
	GtkTreeIter	iter;
	FType		*type;

	if((type = get_current_type(page, &iter)) != NULL)
	{
		Style	*stl;

		if((stl = sdl_dialog_sync_new_wait(cst_get_styleinfo(), NULL)) != NULL)
		{
			type->style = stl;
			set_widgets(page, type, &iter);
			page->modified = TRUE;
		}
	}
	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-14 -	User just clicked the 'Add' button. Let's add a new type to play with!
** 1998-12-14 -	Rewritten to work better with the new priority-less type editing system.
*/
static gint evt_add_clicked(GtkWidget *wid, gpointer user)
{
	P_Types	*page = user;
	CfgInfo	*cfg  = g_object_get_data(G_OBJECT(wid), "user");
	FType	*tpe, *otpe;

	if((tpe = typ_type_new(cfg, _("(New Type)"), S_IFREG, 0, NULL, NULL, NULL)) != NULL)
	{
		GtkTreeIter	oiter, iter;	/* Current and new. */
		GtkTreePath	*path;

		/* Do some fairly advanced hackery-pokery to avoid having to rebuild the
		** entire ListStore. Instead add in the proper place in both Type list and
		** the ListStore.
		*/
		if((otpe = get_current_type(page, &oiter)) != NULL)
		{
			/* Current selection exists; add after it. */
			page->type = typ_type_insert(page->type, otpe, tpe);
			gtk_list_store_insert_after(page->store, &iter, &oiter);
		}
		else
		{
			/* No current selection; insert right before the UNKNOWN at the end. */
			GtkTreeIter	unknown;
			gint		len;

			/* Figure out how many types there are. */
			len = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(page->store), NULL);
			/* Get an iter to the last one, which is "Unknown". This is (I guess) O(n),
			** but that 'n' will be quite short so this is easily quick enough.
			*/
			gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(page->store), &unknown, NULL, len - 1);
			page->type = typ_type_insert(page->type, NULL, tpe);
			gtk_list_store_insert_before(page->store, &iter, &unknown);
		}
		/* Set the proper values in the ListStore. */
		gtk_list_store_set(page->store, &iter, COLUMN_NAME, tpe->name, COLUMN_TYPE, tpe, -1);

		/* Select the new type. */
		gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->view)), &iter);
		/* Scroll list to new type. */
		path = gtk_tree_model_get_path(GTK_TREE_MODEL(page->store), &iter);
		gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(page->view), path, NULL, TRUE, 0.5f, 0.0f);
		gtk_tree_path_free(path);

		typ_type_set_style(page->type, tpe, cst_get_styleinfo(), NULL);
		gtk_list_store_set(page->store, &iter, COLUMN_NAME, tpe->name, COLUMN_TYPE, tpe, -1);

		/* Put focus in the Name field, highly likely a rename is in order. */
		gtk_widget_grab_focus(page->name);

		page->modified = TRUE;
	}
	return TRUE;
}

/* 1998-08-14 -	Delete button was hit, so kill the currently selected type. Tragic.
** 1998-12-14 -	Rewritten. Didn't use the types-module, and lost selection.
*/
static gint evt_del_clicked(GtkWidget *wid, gpointer user)
{
	P_Types		*page = user;
	GtkTreeIter	iter;

	/* Don't rebuild ListStore; surgically remove just the proper row. */
	if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(page->view)), NULL, &iter))
	{
		FType	*type;

		gtk_tree_model_get(GTK_TREE_MODEL(page->store), &iter, COLUMN_TYPE, &type, -1);
		page->type = typ_type_remove(page->type, type);
		gtk_list_store_remove(page->store, &iter);
		page->modified = TRUE;
	}
	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 2009-03-16 -	Rewritten to be a bit more efficient, while using the new GTK+ 2 tree stuff, too. */
static gint evt_up_clicked(GtkWidget *wid, gpointer user)
{
	P_Types		*page = user;
	GtkTreeIter	iter, piter;
	FType		*type;

	if((type = get_current_type(page, &iter)) != NULL)
	{
		GtkTreePath	*path;

		/* Moving up in the list requires a path, for the prev() stepping. */
		path = gtk_tree_model_get_path(GTK_TREE_MODEL(page->store), &iter);
		if(gtk_tree_path_prev(path))
		{
			/* Do the move. */
			page->type = typ_type_move(page->type, page->curr_type, -1);
			gtk_tree_model_get_iter(GTK_TREE_MODEL(page->store), &piter, path);
			gtk_list_store_move_before(page->store, &iter, &piter);
			gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(page->view), path, NULL, TRUE, 0.5f, 0.0f);
			page->modified = TRUE;
			set_movement_widgets(page, &iter);
		}
		gtk_tree_path_free(path);
	}
	return TRUE;
}

/* 2009-03-16 -	Rewritten to be a bit more efficient, while using the new GTK+ 2 tree stuff, too. */
static gint evt_down_clicked(GtkWidget *wid, gpointer user)
{
	P_Types		*page = user;
	GtkTreeIter	iter, niter;
	FType		*type;

	if((type = get_current_type(page, &iter)) != NULL)
	{
		GtkTreePath	*path;

		/* Scrolling requires a path, so let's do it that way. */
		path = gtk_tree_model_get_path(GTK_TREE_MODEL(page->store), &iter);
		gtk_tree_path_next(path);
		/* Then do the move. */
		page->type = typ_type_move(page->type, page->curr_type, 1);
		if(gtk_tree_model_get_iter(GTK_TREE_MODEL(page->store), &niter, path))
		{
			gtk_list_store_move_after(page->store, &iter, &niter);
			gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(page->view), path, NULL, TRUE, 0.5f, 0.0f);
			page->modified = TRUE;
			set_movement_widgets(page, &iter);
		}
		gtk_tree_path_free(path);
	}
	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

static GtkWidget * ctp_init(MainInfo *min, gchar **name)
{
	P_Types			*page = &the_page;
	GtkWidget		*scwin, *label, *vbox, *sep;
        GtkCellRenderer		*cr;
        GtkTreeViewColumn	*vc;
        GtkTreeSelection	*ts;
	const gchar		*tlab[] = { N_("File"), N_("Dir"), N_("Link"), N_("B-Dev"), N_("C-Dev"), N_("FIFO"), N_("Socket") },
				*iplab[] = { N_("SetUID"), N_("SetGID"), N_("Sticky"),  N_("Readable"),
						N_("Writable"), N_("Executable") },
						*idlab[] = { N_("Require Suffix"), N_("Match Name (RE)"), N_("Match 'file' (RE)") };
	gint			idmax[] = { FT_SUFFIX_SIZE, FT_NAMERE_SIZE, FT_FILERE_SIZE },
				ifperm[] = { FTPM_SETUID, FTPM_SETGID, FTPM_STICKY, FTPM_READ, FTPM_WRITE, FTPM_EXECUTE }, i, y;
	static mode_t		type[] = { S_IFREG, S_IFDIR, S_IFLNK, S_IFBLK, S_IFCHR, S_IFIFO, S_IFSOCK };

	page->min = min;

	page->type = NULL;
	page->curr_type = NULL;
	page->modified  = FALSE;

	page->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	page->handlers = gui_handler_group_new();

	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	page->store = gtk_list_store_new(COLUMN_COUNT, G_TYPE_STRING, G_TYPE_POINTER);
	page->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(page->store));
        cr = gtk_cell_renderer_text_new();
        vc = gtk_tree_view_column_new_with_attributes("(name)", cr, "text", COLUMN_NAME, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(page->view), vc);
        gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(page->view), FALSE);
        ts = gtk_tree_view_get_selection(GTK_TREE_VIEW(page->view));
        g_signal_connect(G_OBJECT(ts), "changed", G_CALLBACK(evt_selection_changed), page);
        gtk_container_add(GTK_CONTAINER(scwin), page->view);
        gtk_box_pack_start(GTK_BOX(page->vbox), scwin, TRUE, TRUE, 0);

	page->ngrid = gtk_grid_new();
	label = gtk_label_new(_("Name"));
	gtk_grid_attach(GTK_GRID(page->ngrid), label, 0, 0, 1, 1);
	page->name = gtk_entry_new();
	gtk_widget_set_hexpand(page->name, TRUE);
	gtk_widget_set_halign(page->name, GTK_ALIGN_FILL);
	gtk_entry_set_max_length(GTK_ENTRY(page->name), STL_STYLE_NAME_SIZE - 1);
	gui_handler_group_connect(page->handlers, G_OBJECT(page->name), "changed", G_CALLBACK(evt_name_changed), page);
	gtk_grid_attach(GTK_GRID(page->ngrid), page->name, 1, 0, 1, 1);

	gtk_box_pack_start(GTK_BOX(page->vbox), page->ngrid, FALSE, FALSE, 0);

	page->iframe = gtk_frame_new(_("Identification"));
	page->igrid = gtk_grid_new();
	label = gtk_label_new(_("Require Type"));
	gtk_grid_attach(GTK_GRID(page->igrid), label, 0, 0, 1, 1);
	page->ithbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	page->itlist = gui_radio_group_new(7, tlab, page->itype);
	page->itmode = type;
	for(i = 0; i < 7; i++)
	{
		gui_handler_group_connect(page->handlers, G_OBJECT(page->itype[i]), "clicked", G_CALLBACK(evt_mode_clicked), page);
		g_object_set_data(G_OBJECT(page->itype[i]), "user", GINT_TO_POINTER(i));
		gtk_box_pack_start(GTK_BOX(page->ithbox), page->itype[i], TRUE, TRUE, 0);
	}
	gtk_grid_attach(GTK_GRID(page->igrid), page->ithbox, 1, 0, 2, 1);

	page->irperm = gtk_check_button_new_with_label(_("Require Protection"));
	gui_handler_group_connect(page->handlers, G_OBJECT(page->irperm), "clicked", G_CALLBACK(evt_id_reqperm_clicked), page);
	gtk_grid_attach(GTK_GRID(page->igrid), page->irperm, 0, 1, 1, 1);
	page->iphbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	for(i = 0; i < 6; i++)
	{
		page->iperm[i] = gtk_toggle_button_new_with_label(_(iplab[i]));
		g_object_set_data(G_OBJECT(page->iperm[i]), "user", GINT_TO_POINTER(ifperm[i]));
		gui_handler_group_connect(page->handlers, G_OBJECT(page->iperm[i]), "clicked", G_CALLBACK(evt_id_perm_clicked), page);
		gtk_box_pack_start(GTK_BOX(page->iphbox), page->iperm[i], TRUE, TRUE, 0);
	}
	gtk_widget_set_hexpand(page->iphbox, TRUE);
	gtk_widget_set_halign(page->iphbox, GTK_ALIGN_FILL);
	gtk_grid_attach(GTK_GRID(page->igrid), page->iphbox, 1, 1, 2, 1);

	sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_grid_attach(GTK_GRID(page->igrid), sep, 0, 4, 3, 1);

	for(i = 0; i < 3; i++)
	{
		y = (i == 2) ? i + 3 : i + 2;

		page->ident[i].check = gtk_check_button_new_with_label(_(idlab[i]));
		g_object_set_data(G_OBJECT(page->ident[i].check), "user", GINT_TO_POINTER(i));
		gui_handler_group_connect(page->handlers, G_OBJECT(page->ident[i].check), "clicked", G_CALLBACK(evt_id_check_clicked), page);
		gtk_grid_attach(GTK_GRID(page->igrid), page->ident[i].check, 0, y, 1, 1);

		page->ident[i].entry = gtk_entry_new();
		gtk_entry_set_max_length(GTK_ENTRY(page->ident[i].entry), idmax[i] - 1);
		g_object_set_data(G_OBJECT(page->ident[i].entry), "user", GINT_TO_POINTER(i));
		gui_handler_group_connect(page->handlers, G_OBJECT(page->ident[i].entry), "changed", G_CALLBACK(evt_id_entry_changed), page);
		gtk_grid_attach(GTK_GRID(page->igrid), page->ident[i].entry, 1, y, (i == 0) ? 2 : 1, 1);

		if(i >= 1)
		{
			GtkWidget	*hbox;

			hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
			page->ident[i].glob = gtk_check_button_new_with_label(_("Glob?"));
			g_object_set_data(G_OBJECT(page->ident[i].glob), "user", GINT_TO_POINTER(i));
			gui_handler_group_connect(page->handlers, G_OBJECT(page->ident[i].glob), "clicked", G_CALLBACK(evt_id_glob_changed), page);
			gtk_box_pack_start(GTK_BOX(hbox), page->ident[i].glob, FALSE, FALSE, 0);

			page->ident[i].nocase = gtk_check_button_new_with_label(_("Ignore Case?"));
			g_object_set_data(G_OBJECT(page->ident[i].nocase), "user", GINT_TO_POINTER(i));
			gui_handler_group_connect(page->handlers, G_OBJECT(page->ident[i].nocase), "clicked", G_CALLBACK(evt_id_nocase_changed), page);
			gtk_box_pack_start(GTK_BOX(hbox), page->ident[i].nocase, FALSE, FALSE, 0);

			gtk_grid_attach(GTK_GRID(page->igrid), hbox, 2, y, 1, 1);
		}
	}
	gtk_container_add(GTK_CONTAINER(page->iframe), page->igrid);
	gtk_box_pack_start(GTK_BOX(page->vbox), page->iframe, FALSE, FALSE, 5);

	page->sframe = gtk_frame_new(_("Type's Style"));
	page->shbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new(_("Style"));
	gtk_box_pack_start(GTK_BOX(page->shbox), label, FALSE, FALSE, 5);
	page->sbtn = gtk_button_new();
	gui_handler_group_connect(page->handlers, G_OBJECT(page->sbtn), "clicked", G_CALLBACK(evt_style_clicked), page);

	page->sbhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	page->sicon  = NULL;
	page->slabel = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(page->sbhbox), page->slabel, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(page->sbtn), page->sbhbox);

	gtk_box_pack_start(GTK_BOX(page->shbox), page->sbtn, TRUE, TRUE, 5);
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(vbox), page->shbox, FALSE, FALSE, 5);
	gtk_container_add(GTK_CONTAINER(page->sframe), vbox);

	gtk_box_pack_start(GTK_BOX(page->vbox), page->sframe, FALSE, FALSE, 5);

	page->bhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	page->add = gtk_button_new_with_label(_("Add"));
	g_object_set_data(G_OBJECT(page->add), "user", &min->cfg);
	g_signal_connect(G_OBJECT(page->add), "clicked", G_CALLBACK(evt_add_clicked), page);
	gtk_box_pack_start(GTK_BOX(page->bhbox), page->add, TRUE, TRUE, 5);

	page->up = gtk_button_new_from_icon_name("go-up", GTK_ICON_SIZE_MENU);
	g_signal_connect(G_OBJECT(page->up), "clicked", G_CALLBACK(evt_up_clicked), page);
	gtk_box_pack_start(GTK_BOX(page->bhbox), page->up, FALSE, FALSE, 0);

	page->down = gtk_button_new_from_icon_name("go-down", GTK_ICON_SIZE_MENU);
	g_signal_connect(G_OBJECT(page->down), "clicked", G_CALLBACK(evt_down_clicked), page);
	gtk_box_pack_start(GTK_BOX(page->bhbox), page->down, FALSE, FALSE, 0);

	page->del = gtk_button_new_with_label(_("Delete"));
	g_signal_connect(G_OBJECT(page->del), "clicked", G_CALLBACK(evt_del_clicked), page);
	gtk_box_pack_start(GTK_BOX(page->bhbox), page->del, TRUE, TRUE, 5);

	gtk_box_pack_start(GTK_BOX(page->vbox), page->bhbox, FALSE, FALSE, 5);

	gtk_widget_show_all(page->vbox);

	cfg_tree_level_append(_("Types"), page->vbox);
	cfg_tree_level_end();

	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-14 -	Redisplay imminent, update looks. Simplistic. */
static void ctp_update(MainInfo *min)
{
	populate_list(min, &the_page);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-14 -	Accept the changes. Replace the cfg->type list with our the_page.list, and
**		also free the one being replaced.
*/
static void ctp_accept(MainInfo *min)
{
	P_Types	*page = &the_page;

	if(page->modified)
	{
		const GList	*iter;

		for(iter = min->cfg.type; iter != NULL; iter = g_list_next(iter))
			typ_type_destroy(iter->data);
		g_list_free(min->cfg.type);
		min->cfg.type = page->type;
		page->type	= NULL;		/* Make sure list gets rebuilt next time. */
		page->modified	= FALSE;
		cfg_set_flags(CFLG_RESCAN_LEFT | CFLG_RESCAN_RIGHT);
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-14 -	Save out a single file type. */
static void save_type(gpointer data, gpointer user)
{
	FType	*tpe = data;
	FILE	*out = user;

	if(tpe == NULL)
		return;

	xml_put_node_open(out, "FileType");
	xml_put_text(out, "name", tpe->name);
	xml_put_integer(out, "mode", tpe->mode);

	if(tpe->flags & FTFL_REQPERM)
		xml_put_integer(out, "perm", tpe->perm);
	if(tpe->flags & FTFL_REQSUFFIX)
		xml_put_text(out, "suffix", tpe->suffix);
	if(tpe->flags & FTFL_NAMEMATCH)
	{
		xml_put_text(out, "name_re", tpe->name_re_src);
		xml_put_boolean(out, "name_glob", tpe->flags & FTFL_NAMEGLOB);
		xml_put_boolean(out, "name_nocase", tpe->flags & FTFL_NAMENOCASE);
	}
	if(tpe->flags & FTFL_FILEMATCH)
	{
		xml_put_text(out, "file_re", tpe->file_re_src);
		xml_put_boolean(out, "file_glob", tpe->flags & FTFL_FILEGLOB);
		xml_put_boolean(out, "file_nocase", tpe->flags & FTFL_FILENOCASE);
	}
	if(tpe->style != NULL)
		xml_put_text(out, "style", stl_style_get_name(tpe->style));
	else
		fprintf(stderr, "**CFGTYPES: Type '%s' has NULL style!\n", tpe->name);
	xml_put_node_close(out, "FileType");
}

/* 1998-08-14 -	Save out all the filetype information. */
static gint ctp_save(MainInfo *min, FILE *out)
{
	xml_put_node_open(out, NODE);
	g_list_foreach(min->cfg.type, save_type, out);
	xml_put_node_close(out, NODE);

	return 1;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-14 -	Load a single type and put it into the list.
** 1999-03-07 -	Fixed huge bug, where a pointer to mode_t was cast to int *.
**		Not good, since mode_t is just 16 bit...
*/
static void load_type(const XmlNode *node, gpointer user)
{
	CfgInfo		*cfg = user;
	const gchar	*name = NULL, *suffix = NULL, *name_re = NULL, *file_re = NULL, *style = NULL;
	gint		perm = 0, tmp, mode;
	FType		*tpe;

	xml_get_text(node, "name", &name);
	xml_get_integer(node, "mode", &mode);
	xml_get_integer(node, "perm", &perm);
	xml_get_text(node, "suffix", &suffix);
	xml_get_text(node, "name_re", &name_re);
	xml_get_text(node, "file_re", &file_re);
	xml_get_text(node, "style", &style);

	if((tpe = typ_type_new(cfg, name, mode, perm, suffix, name_re, file_re)) != NULL)
	{
		if(xml_get_boolean(node, "name_glob", &tmp) && tmp)
			tpe->flags |= FTFL_NAMEGLOB;
		if(xml_get_boolean(node, "name_nocase", &tmp) && tmp)
			tpe->flags |= FTFL_NAMENOCASE;
		if(xml_get_boolean(node, "file_glob", &tmp) && tmp)
			tpe->flags |= FTFL_FILEGLOB;
		if(xml_get_boolean(node, "file_nocase", &tmp) && tmp)
			tpe->flags |= FTFL_FILENOCASE;
		cfg->type = typ_type_insert(cfg->type, NULL, tpe);
		cfg->type = typ_type_set_style(cfg->type, tpe, cfg->style, style);
	}
}

/* 1998-08-14 -	Load in the filetypes hanging off of <node>. */
static void ctp_load(MainInfo *min, const XmlNode *node)
{
	GList	*old, *next;

	/* First destroy any previously defined types (e.g. the built-in ones). */
	for(old = min->cfg.type; old != NULL; old = next)
	{
		next = g_list_next(old);
		if(old->data == NULL)
			continue;
		typ_type_destroy(old->data);
		min->cfg.type = g_list_remove(min->cfg.type, old->data);
	}
	xml_node_visit_children(node, load_type, &min->cfg);
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-14 -	When the GUI hides (i.e. is removed from screen by a reason different than
**		the user hitting the "OK" button), we free our editing copies.
*/
static void ctp_hide(MainInfo *min)
{
	P_Types	*page = &the_page;
	GList	*old, *next;

	for(old = page->type; old != NULL; old = next)
	{
		next = g_list_next(old);
		if(old->data == NULL)
			continue;
		typ_type_destroy(old->data);
		page->type = g_list_remove(page->type, old->data);
	}
	g_list_free(page->type);
	page->type = NULL;
}

/* ----------------------------------------------------------------------------------------- */

const CfgModule * ctp_describe(MainInfo *min)
{
	static const CfgModule	desc = { NODE, ctp_init, ctp_update, ctp_accept, ctp_save, ctp_load, ctp_hide };

	return &desc;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-08-25 -	Returns the list of current editing types. Handy when styles have changed.
**		This function is part of the rather nasty relationship between styles and
**		types; if you change enough on the styles page, the types will also think
**		they've been modified (thanks to the flag setting below). This is all due
**		to the fact that types link to their styles using direct pointer links.
*/
GList * ctp_get_types(void)
{
	the_page.modified = TRUE;

	return the_page.type;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-05-27 -	Go through current types, and if any type contains a link to a style named <from>,
**		replace that with a link to the given style. Called by the styles config module,
**		when a style is renamed or deleted (in the latter case, <to> will be "Root").
*/
void ctp_replace_style(const gchar *from, Style *to)
{
	GList	*iter;
	FType	*type;

	for(iter = the_page.type; iter != NULL; iter = g_list_next(iter))
	{
		type = iter->data;

		if(strcmp(stl_style_get_name(type->style), from) == 0)
		{
			type->style = to;
			the_page.modified = TRUE;
		}
	}
}

/* 1999-05-27 -	Change all type's style pointers to point at styles in <to>, rather than <from>.
**		This gets called by the cfg_styles module, just before it destroys the <from>
**		styles.
*/
void ctp_relink_styles(const StyleInfo *from, const StyleInfo *to)
{
	const GList	*iter;

	for(iter = the_page.type; iter != NULL; iter = g_list_next(iter))
	{
		FType	*type = iter->data;
		Style	*ns = stl_styleinfo_style_find(to, stl_style_get_name(type->style));

		if(type->style != ns)
		{
			type->style = ns;
			the_page.modified = TRUE;
		}
	}
}
