/*
** 1998-12-19 -	Let's pretend we're one of those big, bad, desktop environments, and provide
**		users with an "Information" window in which we can show cool stuff... This
**		will basically repeat what stat() has told us, plus a few extras.
** 1999-03-06 -	Changed for the new selection/dirrow handling.
** 1999-03-28 -	Now uses the progress-indicating version of the fut_dir_size() routine.
** 1999-04-05 -	Added use of the new command configuration system, to allow user to have
**		some control over how this command operates. Nice.
** 2000-07-02 -	Internationalized. Lost some hard-coded plural suffix stuff (was English-only). :(
** 2002-02-08 -	Now uses the dialog module. Why it wasn't, nobody knows.
*/

#include "gentoo.h"

#include <time.h>

#include "errors.h"
#include "file.h"
#include "dialog.h"
#include "dirpane.h"
#include "iconutil.h"
#include "sizeutil.h"
#include "strutil.h"
#include "fileutil.h"
#include "userinfo.h"
#include "progress.h"
#include "cmdseq_config.h"

#include "cmd_info.h"

#define	CMD_ID	"info"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	gboolean modified;
	gboolean use_file;			/* Show 'file' output? */
	gboolean recurse_dirs;			/* Recurse directories? */
	gchar	 df_access[DP_DATEFMT_SIZE];	/* Formatter for dates of last access, */
	gchar	 df_modify[DP_DATEFMT_SIZE];	/*  modification, */
	gchar	 df_change[DP_DATEFMT_SIZE];	/*  and change. */
	gchar	 tick[2];
} OptInfo;

static OptInfo	info_options;
static CmdCfg	*info_cmc = NULL;

/* Special little struct used to hold information during background directory sizing. */
typedef struct {
	GtkWidget	*size, *contents;	/* Label widgets, updated. */
	gpointer	handle;			/* Handle from fut_dir_size_start(). */
} DirSizeInfo;

/* ----------------------------------------------------------------------------------------- */

static void dir_size_update(const FUCount *count, gpointer user)
{
	DirSizeInfo	*dsi = user;
	gchar		buf[64], sbuf[64], stbuf[64], dbuf[64], fbuf[64];

	if(!count)
		return;

	/* Replace contents of previously built size-label. */
	sze_put_offset(sbuf, sizeof sbuf, count->num_bytes, SZE_AUTO, 3, ',');
	sze_put_offset(stbuf, sizeof stbuf, count->num_bytes, SZE_BYTES, 3, ',');
			
	g_snprintf(buf, sizeof buf, _("%s (%s)"), sbuf, stbuf);
	gtk_label_set_text(GTK_LABEL(dsi->size), buf);

	/* And tell a little something about the contents. */
	sze_put_offset(dbuf, sizeof dbuf, count->num_dirs, SZE_BYTES_NO_UNIT, 3, ',');
	sze_put_offset(fbuf, sizeof fbuf, count->num_files, SZE_BYTES_NO_UNIT, 3, ',');
	g_snprintf(buf, sizeof buf, _("%s dirs, %s files,\n%u symlinks, %u special files"),
			dbuf, fbuf, count->num_links, count->num_specials);
	gtk_label_set_text(GTK_LABEL(dsi->contents), buf);
}

static GtkWidget * label_pack(GtkWidget *grid, const gchar *text, gint x, gint y, gboolean left)
{
	GtkWidget	*lab;

	lab  = gtk_label_new(text);
	gtk_label_set_xalign(GTK_LABEL(lab), left ? 0.f : 0.9f);
	gtk_widget_set_hexpand(lab, TRUE);
	gtk_grid_attach(GTK_GRID(grid), lab, x, y, 1, 1);

	return lab;
}

static gint build_date(const gchar *label, gint y, guint64 date, const gchar *fmt, GtkWidget *grid)
{
	gchar		buf[64];
	time_t		tvalue;
	struct tm	*tm;

	tvalue = (time_t) date;
	if(tvalue != 0 && (tm = localtime(&tvalue)) != NULL)
	{
		label_pack(grid, label, 0, y, FALSE);
		if(strftime(buf, sizeof buf, fmt, tm) > 0)
			label_pack(grid, buf, 1, y++, TRUE);
	}
	return y;
}

/* 2010-03-02 -	Build a second page (!) of the Information window, showing a large set of GIO attributes. */
static GtkWidget * build_gio_info(const DirPane *dp, const DirRow2 *row)
{
	GtkListStore		*lm = NULL;
	GtkWidget		*view, *scwin;
	GtkCellRenderer		*cr;
	GtkTreeViewColumn	*vc;
	GFile			*file;
	GFileInfo		*info;
	const gchar		*namespaces[] = { "standard", "access", "mountable", "unix", "time", "owner", "filesystem", NULL };
	gchar			**atts;
	guint			i, j;

	if((file = dp_get_file_from_row(dp, row)) == NULL)
		return NULL;
	info = g_file_query_info(file, "*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
	g_object_unref(file);
	if(info == NULL)
		return NULL;

	lm = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	for(i = 0; namespaces[i] != NULL; i++)
	{
		atts = g_file_info_list_attributes(info, namespaces[i]);
		if(atts != NULL)
		{
			GtkTreeIter	iter;

			for(j = 0; atts[j] != NULL; j++)
			{
				gchar	*value;

				if((value = g_file_info_get_attribute_as_string(info, atts[j])) != NULL)
				{
					gtk_list_store_insert_with_values(lm, &iter, -1, 0, atts[j], 1, value, -1);
					g_free(value);
				}
			}
			g_free(atts);
		}
	}

	view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(lm));
	cr = gtk_cell_renderer_text_new();
	vc = gtk_tree_view_column_new_with_attributes(_("Name"), cr, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), vc);
	vc = gtk_tree_view_column_new_with_attributes(_("Value"), cr, "text", 1, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), vc);

	scwin = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scwin), view);

	return scwin;
}

static GtkWidget * build_info(MainInfo *min, const DirPane *dp, const DirRow2 *row, DirSizeInfo **dsi)
{
	const GtkTreeModel	*m = dp_get_tree_model(dp);
	gchar			buf[128], sbuf[32], *ptr;
	guint			y;
	const gchar		*iname;
	GtkWidget		*nbook, *grid, *szlab, *sep, *entry, *gp;

	nbook = gtk_notebook_new();

	grid = gtk_grid_new();
	gtk_widget_set_margin_start(grid, 5);
	gtk_widget_set_margin_end(grid, 5);
	gtk_widget_set_margin_top(grid, 5);
	gtk_widget_set_margin_bottom(grid, 5);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 5);

	y = 0;
	if(dp_row_get_ftype(m, row)->style != NULL)
	{
		if((iname = stl_style_property_get_icon(dp_row_get_ftype(m, row)->style, SPN_ICON_UNSEL)) != NULL)
		{
			GdkPixbuf	*pbuf;

			if((pbuf = ico_icon_get_pixbuf(min, iname)) != NULL)
			{
				GtkWidget	*img;

				img = gtk_image_new_from_pixbuf(pbuf);
				gtk_grid_attach(GTK_GRID(grid), img, 0, y, 1, 1);
			}
		}
	}
	label_pack(grid, dp_row_get_name_display(m, row), 1, y++, TRUE);

	/* For symbolic links, display name of the target, at least. */
	if(dp_row_get_file_type(m, row, FALSE) == G_FILE_TYPE_SYMBOLIC_LINK)
	{
		GtkWidget	*hbox, *icon;

		label_pack(grid, _("Link To"), 0, y, FALSE);
		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		entry = gtk_entry_new();
		gtk_widget_set_sensitive(entry, FALSE);	/* Used to be just non-editable, but ... this looks better. */
		gtk_entry_set_text(GTK_ENTRY(entry), dp_row_get_link_target(m, row));
		gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
		icon = gtk_image_new_from_icon_name(dp_row_get_flags(m, row, DPRF_LINK_EXISTS) ? "face-smile" : "face-sad", GTK_ICON_SIZE_BUTTON);
		gtk_box_pack_start(GTK_BOX(hbox), icon, FALSE, FALSE, 0);
		gtk_widget_set_hexpand(hbox, TRUE);
		gtk_widget_set_halign(hbox, GTK_ALIGN_FILL);
		gtk_grid_attach(GTK_GRID(grid), hbox, 1, y, 1, 1);
		y++;
	}
	sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_grid_attach(GTK_GRID(grid), sep, 0, y, 2, 1);
	y++;

	label_pack(grid, _("Type"), 0, y, FALSE);
	label_pack(grid, dp_row_get_ftype(m, row)->name, 1, y++, TRUE);

	label_pack(grid, _("Location"), 0, y, FALSE);
	label_pack(grid, dp->dir.path, 1, y++, TRUE);

	label_pack(grid, _("Size"), 0, y, FALSE);
	sze_put_offset(sbuf, sizeof sbuf, dp_row_get_size(m, row), SZE_AUTO, 3, ',');
	if(dp_row_get_size(m, row) < 1024)
		g_strlcpy(buf, sbuf, sizeof buf);
	else
	{
		gchar	stbuf[64], *st;

		stbuf[sizeof stbuf - 1] = '\0';
		st = stu_tickify(stbuf + sizeof stbuf - 1, dp_row_get_size(m, row), info_options.tick[0]);
		g_snprintf(buf, sizeof buf, _("%s (%s bytes)"), sbuf, st);
	}
	szlab = label_pack(grid, buf, 1, y++, TRUE);
	/* Building info for a directory, and recursing enabled? */
	if(dp_row_get_file_type(m, row, TRUE) == G_FILE_TYPE_DIRECTORY && info_options.recurse_dirs)
	{
		GFile	*dir = dp_get_file_from_row(dp, row);

		/* Start asynchronous directory sizing. Nice. */
		*dsi = g_malloc(sizeof *dsi);
		(*dsi)->size     = szlab;
		label_pack(grid, _("Contains"), 0, y, FALSE);
		(*dsi)->contents = label_pack(grid, "(content info)", 1, y++, TRUE);
		(*dsi)->handle   = fut_dir_size_gfile_start(dir, dir_size_update, *dsi);
	}

	/* Is the "Show 'file' Output?" option enabled? Requires "file -n -f -" to work. */
#if defined HAVE_GOOD_FILE
/* FIXME: This just isn't a good idea, for potentially remote or archived files ...*/
	if(info_options.use_file)
	{
		gchar	*path;

		if((path = g_file_get_path(dp_get_file_from_row(dp, row))) != NULL)
		{
			sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
			gtk_grid_attach(GTK_GRID(grid), sep, 0, y, 2, 1);
			y++;
			label_pack(grid, _("'File' Info"), 0, y, FALSE);
			label_pack(grid, fle_file(min, path), 1, y++, TRUE);
			g_free(path);
		}
	}
#endif
	sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_grid_attach(GTK_GRID(grid), sep, 0, y, 2, 1);
	y++;

	label_pack(grid, _("Owner"), 0, y, FALSE);
	if((ptr = (gchar *) usr_lookup_uname(dp_row_get_uid(m, row))) != NULL)
		g_snprintf(buf, sizeof buf, "%s (%d)", ptr, (gint) dp_row_get_uid(m, row));
	else
		g_snprintf(buf, sizeof buf, "%d", (gint) dp_row_get_uid(m, row));
	label_pack(grid, buf, 1, y++, TRUE);

	label_pack(grid, _("Group"), 0, y, FALSE);
	if((ptr = (gchar *) usr_lookup_gname(dp_row_get_gid(m, row))) != NULL)
		g_snprintf(buf, sizeof buf, "%s (%d)", ptr, (gint) dp_row_get_gid(m, row));
	else
		g_snprintf(buf, sizeof buf, "%d", (gint) dp_row_get_gid(m, row));
	label_pack(grid, buf, 1, y++, TRUE);

	sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_grid_attach(GTK_GRID(grid), sep, 0, y, 2, 1);
	y++;

	y = build_date(_("Accessed"), y, dp_row_get_time_accessed(m, row), info_options.df_access, grid);
	y = build_date(_("Modified"), y, dp_row_get_time_modified(m, row), info_options.df_modify, grid);
	y = build_date(_("Changed"),  y, dp_row_get_time_changed(m, row), info_options.df_change, grid);
	y = build_date(_("Created"),  y, dp_row_get_time_created(m, row), info_options.df_change, grid);

	gtk_notebook_append_page(GTK_NOTEBOOK(nbook), grid, gtk_label_new(_("Basic")));

	if((gp = build_gio_info(dp, row)) != NULL)
	{
		gtk_notebook_append_page(GTK_NOTEBOOK(nbook), gp, gtk_label_new(_("Attributes")));
	}
	return nbook;
}

/* ----------------------------------------------------------------------------------------- */

/* 2003-10-16 -	Callback when Information window is closing. Stop asynch. dir sizing, if in use. */
static gboolean evt_delete(GtkWidget *wid, GdkEvent *event, gpointer user)
{
	DirSizeInfo	*dsi = user;

	if(dsi != NULL)
		fut_dir_size_gfile_stop(dsi->handle);
	g_free(dsi);
	return FALSE;
}

/* 1999-03-06 -	Create window showing all about <row>, which sits in <dp>. */
static gint information(MainInfo *min, const DirPane *dp, const DirRow2 *row)
{
	DirSizeInfo	*dsi = NULL;
	GtkWidget	*fr;

	if((fr = build_info(min, dp, row, &dsi)) != NULL)
	{
		GtkWidget	*win;

		win = win_window_open(min->cfg.wininfo, WIN_INFO);
		win_window_set_title(win, dp_row_get_name_display(dp_get_tree_model(dp), row));
		g_signal_connect(G_OBJECT(win), "delete_event", G_CALLBACK(evt_delete), dsi);
		gtk_container_add(GTK_CONTAINER(win), fr);
		win_window_show(win);
	}
	return fr != NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-12-19 -	Show information about files in <src>. Knows about double-click, and handles
**		it like everybody else.
*/
gint cmd_information(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	GSList		*slist, *iter;
	gboolean	ok = TRUE;

	if((slist = dp_get_selection(src)) == NULL)
		return 1;

	pgs_progress_begin(min, _("Getting Information..."), PFLG_BUSY_MODE);
	for(iter = slist; ok && (iter != NULL); iter = g_slist_next(iter))
	{
		if((ok = information(min, src, iter->data)))
			dp_unselect(src, iter->data);
	}
	pgs_progress_end(min);

	dp_free_selection(slist);

	return ok;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-05 -	Initialize configuration stuff. */
void cfg_information(MainInfo *min)
{
	if(info_cmc == NULL)
	{
		/* Initialize default option values. */
		info_options.modified	  = FALSE;
		info_options.use_file	  = FALSE;
		info_options.recurse_dirs = TRUE;
		g_strlcpy(info_options.df_access, "%Y-%m-%d %H:%M.%S", sizeof info_options.df_access);
		g_strlcpy(info_options.df_modify, "%Y-%m-%d %H:%M.%S", sizeof info_options.df_modify);
		g_strlcpy(info_options.df_change, "%Y-%m-%d %H:%M.%S", sizeof info_options.df_change);
		g_strlcpy(info_options.tick, ",", sizeof info_options.tick);

		info_cmc = cmc_config_new("Information", &info_options);
		cmc_field_add_boolean(info_cmc, "modified", NULL, offsetof(OptInfo, modified));
#if defined HAVE_GOOD_FILE
		cmc_field_add_boolean(info_cmc, "use_file", _("Show 'file' Output?"), offsetof(OptInfo, use_file));
#endif
		cmc_field_add_boolean(info_cmc, "recurse_dirs", _("Recurse Directories?"), offsetof(OptInfo, recurse_dirs));
		cmc_field_add_string(info_cmc, "df_access", _("Access Date Format"), offsetof(OptInfo, df_access), sizeof info_options.df_access);
		cmc_field_add_string(info_cmc, "df_modify", _("Modify Date Format"), offsetof(OptInfo, df_modify), sizeof info_options.df_modify);
		cmc_field_add_string(info_cmc, "df_change", _("Change Date Format"), offsetof(OptInfo, df_change), sizeof info_options.df_change);
		cmc_field_add_string(info_cmc, "tick", _("Size Tick Mark"), offsetof(OptInfo, tick), sizeof info_options.tick);

		cmc_config_register(info_cmc);
	}
}
