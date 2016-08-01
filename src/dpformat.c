/*
** 1998-05-25 -	This module deals with formatting directory pane entries for display.
**		That's all it does, but since that's a fairly involved thing to do
**		(we aim for plenty of flexibility here), it deserves a module of its
**		own.
** 1999-05-15 -	Added the utility dpf_set_default_format() funtion, for cfg_dirpane.
** 1999-05-20 -	Adapted for the new ultra-flexible & dynamic styles subsystem. Now
**		does three hash-lookups rather than three vector-dittos per row. :)
*/

#include "gentoo.h"

#include <time.h>

#include "dpformat.h"
#include "dirpane.h"
#include "userinfo.h"
#include "iconutil.h"
#include "sizeutil.h"
#include "strutil.h"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	DPContent	id;
	const gchar	*name_full;		/* Long, descriptive name. Multiple words, mixed caps. */
	const gchar	*name_short;		/* Short mnemonic name, no whitespace, all lower case. */
	const gchar	*column_title;		/* Single word, with caps. Handy for pane column titles. */
} ContentInfo;

/* This table is used for the various dpf_get_content_XXX() functions below. It's sorted
** in rising content ID order, but we typically ignore that and search it sequentially
** every time. Just keep those searches out from the inner loops, and things should be fine.
*/
static ContentInfo content_info[] = {
	{ DPC_NAME,		N_("Name"),			"name",		N_("Name") },
	{ DPC_SIZE,		N_("Size"),			"size",		N_("Size") },
	{ DPC_BLOCKS,		N_("Blocks"),			"blocks",	N_("Blocks") },
	{ DPC_BLOCKSIZE,	N_("Block Size"),		"blocksize",	N_("BSize") },
	{ DPC_MODENUM,		N_("Mode, numerical"),		"modenum",	N_("Mode") },
	{ DPC_MODESTR,		N_("Mode, string"),		"modestr",	N_("Mode") },
	{ DPC_NLINK,		N_("Number of links"),		"nlink",	N_("Nlink") },
	{ DPC_UIDNUM,		N_("Owner ID"),			"uid",		N_("Uid") },
	{ DPC_UIDSTR,		N_("Owner Name"),		"uname",	N_("Uname") },
	{ DPC_GIDNUM,		N_("Group ID"),			"gid",		N_("Gid") },
	{ DPC_GIDSTR,		N_("Group Name"),		"gname",	N_("Gname") },
	{ DPC_DEVICE,		N_("Device Number"),		"device",	N_("Device") },
	{ DPC_DEVMAJ,		N_("Device Number, major"),	"devmaj",	N_("DevMaj") },
	{ DPC_DEVMIN,		N_("Device Number, minor"),	"devmin",	N_("DevMin") },
	{ DPC_ATIME,		N_("Time of Last Access"),	"atime",	N_("Accessed") },
	{ DPC_MTIME,		N_("Time of Last Modification"),"mtime",	N_("Modified") },
	{ DPC_CRTIME,		N_("Time of Creation"),		"ctime",	N_("Created") },
	{ DPC_CHTIME,		N_("Time of Last Change"),	"chtime",	N_("Changed") },
	{ DPC_TYPE,		N_("Type Name"),		"type",		N_("Type") },
	{ DPC_ICON,		N_("Icon"),			"icon",		N_("I") },
	{ DPC_URI_NOFILE,	N_("URI (without file prefix)"),"urinofile",	N_("URI") },
	};

/* ----------------------------------------------------------------------------------------- */

/* 2000-07-02 -	Initialize the dpformat module. Mainly concerned with some I18N stuff. */
void dpf_initialize(void)
{
	guint	i;

	for(i = 0; i < sizeof content_info / sizeof content_info[0]; i++)
		content_info[i].column_title = _(content_info[i].column_title);
}

/* 1999-05-15 -	Get the "official" content name for column content number <content>. */
const gchar * dpf_get_content_name(DPContent content)
{
	guint	i;

	for(i = 0; i < sizeof content_info / sizeof content_info[0]; i++)
	{
		if(content_info[i].id == content)
			return _(content_info[i].name_full);
	}
	return NULL;
}

/* 1999-05-15 -	Map a string to a DPContent integer. Returns an illegal index if <name> is bad. */
DPContent dpf_get_content_from_name(const gchar *name)
{
	guint	i;

	for(i = 0; i < sizeof content_info / sizeof content_info[0]; i++)
	{
		if(strcmp(content_info[i].name_full, name) == 0)	/* Compatibility: try it in English first. */
			return content_info[i].id;
		else if(strcmp(_(content_info[i].name_full), name) == 0)
			return content_info[i].id;
	}
	return DPC_NUM_TYPES;
}

/* 2002-06-16 -	Get mnemonic (short, single-word, non-translated) name for content. */
const gchar * dpf_get_content_mnemonic(DPContent content)
{
	guint i;

	for(i = 0; i < sizeof content_info / sizeof *content_info; i++)
	{
		if(content_info[i].id == content)
			return content_info[i].name_short;
	}
	return NULL;
}

/* 1999-11-14 -	Map a short, single word mnemonic content name to an integer content identifier.
**		Returns an illegal identifier if the name is unknown.
*/
DPContent dpf_get_content_from_mnemonic(const gchar *name)
{
	guint	i;

	for(i = 0; i < sizeof content_info / sizeof content_info[0]; i++)
	{
		if(strcmp(content_info[i].name_short, name) == 0)
			return (DPContent) content_info[i].id;
	}
	return DPC_NUM_TYPES;
}

/* 1999-05-15 -	Get a recommended column title for a column showing <content>. */
const gchar * dpf_get_content_title(DPContent content)
{
	guint	i;

	for(i = 0; i < sizeof content_info / sizeof content_info[0]; i++)
	{
		if(content_info[i].id == content)
			return content_info[i].column_title;
	}
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 2010-05-12 -	Returns a tree model with two columns: one is the user-friendly name of a
**		content type, and the other is the enum. Handy for building combos.
*/
static GtkTreeModel * get_content_model(void)
{
	GtkListStore	*store;
	gsize		i;

	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
	for(i = 0; i < sizeof content_info / sizeof *content_info; i++)
	{
		GtkTreeIter	iter;

		gtk_list_store_insert_with_values(store, &iter,-1, 0, _(content_info[i].name_full), 1, content_info[i].id, -1);
	}
	return GTK_TREE_MODEL(store);
}

GtkWidget * dpf_get_content_combo_box(GCallback func, gpointer user)
{
	GtkTreeModel	*model = get_content_model();
	GtkWidget	*wid;
	GtkCellRenderer	*cr;

	wid = gtk_combo_box_new_with_model(model);
	cr = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(wid), cr, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(wid), cr, "text", 0, NULL);
	if(func != NULL)
		g_signal_connect(G_OBJECT(wid), "changed", func, user);

	return wid;
}

/* ----------------------------------------------------------------------------------------- */

void dpf_set_default_format(DpCFmt *fmt, DPContent content)
{
	g_strlcpy(fmt->title, dpf_get_content_title(content), sizeof fmt->title);
	fmt->content = content;
	fmt->just  = GTK_JUSTIFY_LEFT;
	fmt->width = 50;

	switch(fmt->content)
	{
		case DPC_NAME:
			fmt->extra.name.show_type = FALSE;
			fmt->width = 250;
			break;
		case DPC_SIZE:
			fmt->extra.size.unit = SZE_BYTES;
			fmt->extra.size.digits = 0;
			fmt->extra.size.ticks  = 0;
			fmt->extra.size.tick   = ',';
			g_snprintf(fmt->extra.size.dformat, sizeof fmt->extra.size.dformat, "%%#.%uf", fmt->extra.size.digits);
			fmt->extra.size.dir_show_fs_size = TRUE;
			fmt->just = GTK_JUSTIFY_RIGHT;
			break;
		case DPC_BLOCKS:
			g_strlcpy(fmt->extra.blocks.format, "%lu", sizeof fmt->extra.blocks.format);
			break;
		case DPC_BLOCKSIZE:
			g_strlcpy(fmt->extra.blocksize.format, "%lu", sizeof fmt->extra.blocksize.format);
			fmt->just = GTK_JUSTIFY_RIGHT;
			break;
		case DPC_MODENUM:
			g_strlcpy(fmt->extra.mode.format, "%o", sizeof fmt->extra.mode.format);
			break;
		case DPC_NLINK:
			g_strlcpy(fmt->extra.nlink.format, "%u", sizeof fmt->extra.nlink.format);
			fmt->width = 75;
			break;
		case DPC_UIDNUM:
			g_strlcpy(fmt->extra.uidnum.format, "%u", sizeof fmt->extra.uidnum.format);
			fmt->width = 75;
			break;
		case DPC_GIDNUM:
			g_strlcpy(fmt->extra.gidnum.format, "%u", sizeof fmt->extra.gidnum.format);
			fmt->width = 75;
			break;
		case DPC_DEVICE:
			g_strlcpy(fmt->extra.device.format, "%u", sizeof fmt->extra.device.format);
			fmt->width = 75;
			break;
		case DPC_DEVMAJ:
			g_strlcpy(fmt->extra.devmaj.format, "%u", sizeof fmt->extra.devmaj.format);
			fmt->width = 75;
			break;
		case DPC_DEVMIN:
			g_strlcpy(fmt->extra.devmin.format, "%u", sizeof fmt->extra.devmin.format);
			fmt->width = 75;
			break;
		case DPC_ATIME:
			g_strlcpy(fmt->extra.a_time.format, "%Y-%m-%d %H:%M:%S", sizeof fmt->extra.a_time.format);
			fmt->width = 184;
			break;
		case DPC_CRTIME:
			g_strlcpy(fmt->extra.cr_time.format, "%Y-%m-%d %H:%M:%S", sizeof fmt->extra.cr_time.format);
			fmt->width = 184;
			break;
		case DPC_MTIME:
			g_strlcpy(fmt->extra.m_time.format, "%Y-%m-%d %H:%M:%S", sizeof fmt->extra.m_time.format);
			fmt->width = 184;
			break;
		case DPC_CHTIME:
			g_strlcpy(fmt->extra.ch_time.format, "%Y-%m-%d %H:%M:%S", sizeof fmt->extra.ch_time.format);
			fmt->width = 184;
			break;
		case DPC_TYPE:
			break;
		case DPC_ICON:
			fmt->width = 16;
			fmt->just = GTK_JUSTIFY_CENTER;
			break;
		default:
			;
	}
}

/* 1998-10-15 -	Moved this old code out of the main module, since it really didn't belong
**		in there.
** 1999-05-15 -	Thanks to the slightly more general routine above, this could be simplified. Great.
** 1999-05-16 -	Now also initializes the default colors.
*/
void dpf_init_defaults(CfgInfo *cfg)
{
	DPFormat	*fmt = &cfg->dp_format[0];
	guint		i;

	for(i = 0; i < DPC_NUM_TYPES; i++)
	{
		dpf_set_default_format(&fmt->format[i], (DPContent) i);
	}
	fmt->num_columns = i;
	fmt->sort.content = DPC_NAME;
	fmt->sort.invert  = FALSE;
	fmt->sort.mode    = DPS_DIRS_FIRST;
	strcpy(fmt->def_path, "file:///");
	fmt->path_above = FALSE;
	fmt->hide_allowed = TRUE;
	fmt->scrollbar_always = TRUE;
	fmt->huge_parent = FALSE;
	fmt->set_font = FALSE;
	fmt->rubber_banding = FALSE;
	cfg->dp_format[1] = cfg->dp_format[0];	/* Duplicate default formats for second pane. */

	cfg->dp_paning.orientation = DPORIENT_HORIZ;
	cfg->dp_paning.mode	   = DPSPLIT_RATIO;
	cfg->dp_paning.value	   = 0.5;

	cfg->dp_history.select = TRUE;
	cfg->dp_history.save = TRUE;	/* Slightly out of place, but hey. */
}

/* ----------------------------------------------------------------------------------------- */

/* 2009-10-02 -	Gets the indicated content of a row, formatted as a string if possible. Note that this is independent
**		of the selected content types for the display; you can always get at the content this way. It does
**		cost you though. This is intended for human-speed operations, such as doing SelectRE on cell data.
**		NOTE: This uses a "natural" formatting, rather than the high-featured formatting used for pane display.
*/
gboolean dpf_get_content(MainInfo *min, const DirPane *dp, const DirRow2 *row, DPContent content, gchar *out, gsize max)
{
	GtkTreeModel	*m;
	const gchar	*name;
	gchar		*root;
	gsize		offset;
	guint64		filedate;
	GDate		date;

	if(min == NULL || dp == NULL || row == NULL || out == NULL)
		return FALSE;
	m = dp_get_tree_model(dp);
	switch(content)
	{
	case DPC_BLOCKS:
		g_snprintf(out, max, "%" G_GUINT64_FORMAT, dp_row_get_blocks(m, row));
		return TRUE;
	case DPC_BLOCKSIZE:
		g_snprintf(out, max, "%" G_GUINT64_FORMAT, dp_row_get_blocksize(m, row));
		return TRUE;
	case DPC_DEVICE:
		g_snprintf(out, max, "%" G_GUINT32_FORMAT, dp_row_get_device(m, row));
		return TRUE;
	case DPC_DEVMAJ:
		g_snprintf(out, max, "%" G_GUINT32_FORMAT, dp_row_get_device(m, row) >> 8);
		return TRUE;
	case DPC_DEVMIN:
		g_snprintf(out, max, "%" G_GUINT32_FORMAT, dp_row_get_device(m, row) & 0xff);
		return TRUE;
	case DPC_GIDNUM:
		g_snprintf(out, max, "%" G_GUINT32_FORMAT, dp_row_get_gid(m, row));
		return TRUE;
	case DPC_GIDSTR:
		if((name = usr_lookup_gname(dp_row_get_gid(m, row))) != NULL)
			g_snprintf(out, max, "%s", name);
		else
			g_snprintf(out, max, "%" G_GUINT32_FORMAT, dp_row_get_gid(m, row));
		return TRUE;
	case DPC_ICON:
		/* We just can't squeeze the icon into a string. */
		return FALSE;
	case DPC_MODENUM:
		g_snprintf(out, max, "%o", dp_row_get_mode(m, row));
		return TRUE;
	case DPC_MODESTR:
		stu_mode_to_text(out, max, dp_row_get_mode(m, row));
		return TRUE;
	case DPC_NAME:
		g_snprintf(out, max, "%s", dp_row_get_name_display(m, row));
		return TRUE;
	case DPC_NLINK:
		g_snprintf(out, max, "%" G_GUINT32_FORMAT, dp_row_get_nlink(m, row));
		return TRUE;
	case DPC_SIZE:
		g_snprintf(out, max, "%" G_GUINT64_FORMAT, dp_row_get_size(m, row));
		return TRUE;
	case DPC_ATIME:
	case DPC_CRTIME:
	case DPC_MTIME:
	case DPC_CHTIME:
	{
		filedate = (content == DPC_ATIME) ? dp_row_get_time_accessed(m, row) :
			   (content == DPC_CRTIME) ? dp_row_get_time_created(m, row) :
			   (content == DPC_MTIME) ? dp_row_get_time_modified(m, row) :
			   dp_row_get_time_changed(m, row);
		g_date_set_time_t(&date, (time_t) filedate);
		g_date_strftime(out, max, "%Y-%m-%d %H:%M:%S", &date);
		return TRUE;
	}
	case DPC_TYPE:
		g_snprintf(out, max, "%s", dp_row_get_ftype(m, row)->name);
		return TRUE;
	case DPC_UIDNUM:
		g_snprintf(out, max, "%" G_GUINT32_FORMAT, dp_row_get_uid(m, row));
		return TRUE;
	case DPC_UIDSTR:
		if((name = usr_lookup_uname(dp_row_get_uid(m, row))) != NULL)
			g_snprintf(out, max, "%s", name);
		else
			g_snprintf(out, max, "%" G_GUINT32_FORMAT, dp_row_get_uid(m, row));
		return TRUE;
	case DPC_URI_NOFILE:
		if((root = g_file_get_uri(dp->dir.root)) != NULL)
		{
			/* Skip the "file://" scheme prefix. */
			offset = (strncmp(root, "file://", 7) == 0) * 7;	/* Too clever? */
			g_snprintf(out, max, "%s/%s", root + offset, dp_row_get_name(m, row));
			g_free(root);
			return TRUE;
		}
		return FALSE;
	case DPC_NUM_TYPES:
		return FALSE;	/* Fail. */
	}
	return FALSE;
}

/* ----------------------------------------------------------------------------------------- */

/* 2010-02-22 -	Refactored this into a stand-alone function since it's not something I would like
**		seeing copied elsewhere (like, say, in the Styles config preview code).
*/
void dpf_cell_set_style_colors(GtkCellRenderer *r, const Style *style, gboolean do_fg, gboolean do_bg)
{
	const GdkColor	*fg = NULL, *bg = NULL;

	if(style != NULL)
	{
		if(do_fg)
			fg = stl_style_property_get_color(style, SPN_COL_UNSEL_FG);
		if(do_bg)
			bg = stl_style_property_get_color(style, SPN_COL_UNSEL_BG);
	}
	if(do_fg)
	{
		if(fg != NULL)
			g_object_set(G_OBJECT(r), "foreground-gdk", fg, "foreground-set", TRUE, NULL);
		else
			g_object_set(G_OBJECT(r), "foreground-set", FALSE, NULL);
	}
	if(do_bg)
	{
		if(bg != NULL)
			g_object_set(G_OBJECT(r), "cell-background-gdk", bg, "cell-background-set", TRUE, NULL);
		else
			g_object_set(G_OBJECT(r), "cell-background-set", FALSE, NULL);
	}
}

/* 2009-07-15 -	Formats the colors, if any, for the given renderer. The foreground is optional,
**		since not all renderers (such as pixbuf) support a foreground color. This needs to be done
**		per-column, there's no way of doing it per-row in GTK+ 2.0. :/
*/
static void format_colors_row(GtkTreeModel *model, const DirRow2 *row, GtkCellRenderer *r, gboolean do_fg, gboolean do_bg)
{
	if(do_fg || do_bg)
	{
		FType	*ft;
		Style	*st;

		ft = dp_row_get_ftype(model, row);
		if(ft != NULL)
			st = ft->style;
		else
			st = NULL;
		dpf_cell_set_style_colors(r, st, do_fg, do_bg);
	}
}

static void cdf_name(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	const DC_Name	*cfg = user;
	const gchar	*dname = dp_row_get_name_display(model, iter);

	if(cfg->show_type || cfg->show_linkname)
	{
		gchar		buf[1024];
		const gchar	*type = "", *tdname;

		if(cfg->show_type)
		{
			const mode_t	mode = dp_row_get_mode(model, iter);

			if(S_ISLNK(mode))
				type = "@";
			else if(S_ISDIR(mode))
				type = "/";
			else if(S_ISREG(mode) && (mode & S_IXUSR))
				type = "*";
			else if(S_ISFIFO(mode))
				type = "|";
			else if(S_ISSOCK(mode))
				type = "=";
		}
		/* \xe2\x86\x92 is UTF-8 for 'right arrow', Unicode character U+2190. */
		if(cfg->show_linkname && (tdname = dp_row_get_link_target(model, iter)) != NULL)
			g_snprintf(buf, sizeof buf, "%s%s \xe2\x86\x92 %s", dname, type, tdname);
		else
			g_snprintf(buf, sizeof buf, "%s%s", dname, type);

		g_object_set(renderer, "text", buf, NULL);
	}
	else
		g_object_set(renderer, "text", dname, NULL);

	format_colors_row(model, iter, renderer, TRUE, TRUE);
}

static void cdf_size(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	const DC_Size	*cfg = user;
	goffset		size;
	gchar		buf[64];

	if(!cfg->dir_show_fs_size && dp_row_get_file_type(model, iter, TRUE) == G_FILE_TYPE_DIRECTORY)
	{
		g_object_set(renderer, "text", "", NULL);
		return;
	}
	size = dp_row_get_size(model, iter);
	sze_put_offset(buf, sizeof buf, size, cfg->unit, cfg->digits, cfg->ticks ? cfg->tick : '\0');
	g_object_set(renderer, "text", buf, NULL);
	format_colors_row(model, iter, renderer, TRUE, TRUE);
}

static void cdf_blocks(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	guint64	blocks;
	gchar	buf[64];

	blocks = dp_row_get_blocks(model, iter);
	g_snprintf(buf, sizeof buf, "%" G_GUINT64_FORMAT, blocks);
	g_object_set(renderer, "text", buf, NULL);
	format_colors_row(model, iter, renderer, TRUE, TRUE);
}

static void cdf_icon(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	FType	*ftype;

	if((ftype = dp_row_get_ftype(model, iter)) != NULL)
	{
		const Style	*stl = ftype->style;
		const gchar	*iname;

		if((iname = stl_style_property_get_icon(stl, SPN_ICON_UNSEL)) != NULL)
		{
			GdkPixbuf	*pbuf;
			MainInfo	*min = g_object_get_data(G_OBJECT(renderer), "main");

			if((pbuf = ico_icon_get_pixbuf(min, iname)) != NULL)
				g_object_set(renderer, "pixbuf", pbuf, NULL);
		}
	}
	format_colors_row(model, iter, renderer, FALSE, FALSE);
}

static void do_cdf_time(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, const gchar *format, guint64 timestamp, gboolean do_bg)
{
	const time_t	now = (time_t) (timestamp & 0xffffffffu);
	struct tm	local;

	/* Use old-school Unix-y functions, since glib doesn't provide much until 2.26. Then we get GDateTime, w00t. :| */
	if(localtime_r(&now, &local) != NULL)
	{
		gchar	buf[32] = "\x1";	/* Marker char, to detect strftime() failure/success. */
		size_t	got;

		got = strftime(buf, sizeof buf, format, &local);
		if((got == 0 && buf[0] == '\x1') || got == sizeof buf)	/* Try to detect (and handle!) failure. */
			buf[0] = '\0';
		g_object_set(renderer, "text", buf, NULL);
	}
	else
		g_object_set(renderer, "text", "", NULL);
	format_colors_row(model, iter, renderer, TRUE, do_bg);
}

static void cdf_atime(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	do_cdf_time(col, renderer, model, iter, user, dp_row_get_time_accessed(model, iter), TRUE);
}

static void cdf_crtime(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	do_cdf_time(col, renderer, model, iter, user, dp_row_get_time_created(model, iter), TRUE);
}

static void cdf_mtime(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	do_cdf_time(col, renderer, model, iter, user, dp_row_get_time_modified(model, iter), TRUE);
}

static void cdf_chtime(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	do_cdf_time(col, renderer, model, iter, user, dp_row_get_time_changed(model, iter), TRUE);
}

static void cdf_devmax(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	gchar	buf[16];

	g_snprintf(buf, sizeof buf, user, dp_row_get_device(model, iter) >> 8);
	g_object_set(renderer, "text", buf, NULL);
	format_colors_row(model, iter, renderer, TRUE, TRUE);
}

static void cdf_device(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	gchar	buf[16];

	g_snprintf(buf, sizeof buf, user, dp_row_get_device(model, iter));
	g_object_set(renderer, "text", buf, NULL);
	format_colors_row(model, iter, renderer, TRUE, TRUE);
}

static void cdf_devmin(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	gchar	buf[16];

	g_snprintf(buf, sizeof buf, user, dp_row_get_device(model, iter) & 255);
	g_object_set(renderer, "text", buf, NULL);
	format_colors_row(model, iter, renderer, TRUE, TRUE);
}

static void cdf_gidnum(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	gchar	buf[16];

	g_snprintf(buf, sizeof buf, user, dp_row_get_gid(model, iter));
	g_object_set(renderer, "text", buf, NULL);
	format_colors_row(model, iter, renderer, TRUE, TRUE);
}

static void cdf_gidstr(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	guint32		gid;
	const gchar	*group;

	gid = dp_row_get_gid(model, iter);
	if((group = usr_lookup_gname(gid)) != NULL)
		g_object_set(renderer, "text", group, NULL);
	else
	{
		gchar	buf[16];

		g_snprintf(buf, sizeof buf, user, gid);
		g_object_set(renderer, "text", buf, NULL);
	}
	format_colors_row(model, iter, renderer, TRUE, TRUE);
}

static void cdf_modenum(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	gchar	buf[32];

	g_snprintf(buf, sizeof buf, user, dp_row_get_mode(model, iter));
	g_object_set(renderer, "text", buf, NULL);
	format_colors_row(model, iter, renderer, TRUE, TRUE);
}

static void cdf_modestr(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	gchar	buf[16];

	stu_mode_to_text(buf, sizeof buf, dp_row_get_mode(model, iter));
	g_object_set(renderer, "text", buf, NULL);
	format_colors_row(model, iter, renderer, TRUE, TRUE);
}

static void cdf_type(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	const FType	*ft;

	if((ft = dp_row_get_ftype(model, iter)) != NULL)
	{
		g_object_set(renderer, "text", ft->name, NULL);
		format_colors_row(model, iter, renderer, TRUE, TRUE);
	}
}

static void cdf_uidnum(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	gchar	buf[16];

	g_snprintf(buf, sizeof buf, "%u", dp_row_get_uid(model, iter));
	g_object_set(renderer, "text", buf, NULL);
	format_colors_row(model, iter, renderer, TRUE, TRUE);
}

static void cdf_uidstr(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	guint32		uid;
	const gchar	*name;

	uid = dp_row_get_uid(model, iter);

	if((name = usr_lookup_uname(uid)) != NULL)
		g_object_set(renderer, "text", name, NULL);
	else
	{
		gchar	buf[32];

		g_snprintf(buf, sizeof buf, user, uid);
		g_object_set(renderer, "text", buf, NULL);
	}
	format_colors_row(model, iter, renderer, TRUE, TRUE);
}

static void cdf_nlink(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	gchar	buf[16];

	g_snprintf(buf, sizeof buf, "%u", dp_row_get_nlink(model, iter));
	g_object_set(renderer, "text", buf, NULL);
	format_colors_row(model, iter, renderer, TRUE, TRUE);
}

static void cdf_uri(GtkTreeViewColumn *col, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer user)
{
	DirPane	*dp = user;
	GFile	*file;

	if((file = dp_get_file_from_row(dp, iter)) != NULL)
	{
		gchar	*uri, buf[512];

		if((uri = g_file_get_uri(file)) != NULL)
		{
			if(strncmp(uri, "file://", 7) == 0)
				g_snprintf(buf, sizeof buf, "%s", uri + 7);
			else
				g_snprintf(buf, sizeof buf, "%s", uri);
			g_free(uri);
		}
		else
			g_snprintf(buf, sizeof buf, "(failed)");
		g_object_set(renderer, "text", buf, NULL);
		format_colors_row(model, iter, renderer, TRUE, TRUE);
	}
}

GtkTreeCellDataFunc dpf_get_cell_data_func(DirPane *dp, gint column, gpointer *user)
{
	DPFormat	*fmt;
	DpCFmt		*col_fmt;

	fmt = &dp->main->cfg.dp_format[dp->index];
	if(column >= fmt->num_columns)
		return NULL;
	col_fmt = &fmt->format[column];

	switch(col_fmt->content)
	{
	case DPC_ATIME:
		*user = col_fmt->extra.a_time.format;
		return cdf_atime;
	case DPC_BLOCKS:
		*user = col_fmt->extra.blocks.format;
		return cdf_blocks;
	case DPC_BLOCKSIZE:
		return NULL;
	case DPC_CRTIME:
		*user = col_fmt->extra.cr_time.format;
		return cdf_crtime;
	case DPC_CHTIME:
		*user = col_fmt->extra.ch_time.format;
		return cdf_chtime;
	case DPC_DEVICE:
		*user = col_fmt->extra.device.format;
		return cdf_device;
	case DPC_DEVMAJ:
		*user = col_fmt->extra.devmaj.format;
		return cdf_devmax;
	case DPC_DEVMIN:
		*user = col_fmt->extra.devmin.format;
		return cdf_devmin;
	case DPC_GIDNUM:
		*user = col_fmt->extra.gidnum.format;
		return cdf_gidnum;
	case DPC_GIDSTR:
		*user = col_fmt->extra.gidnum.format;
		return cdf_gidstr;
	case DPC_ICON:
		*user = NULL;
		return cdf_icon;
	case DPC_MODENUM:
		*user = col_fmt->extra.mode.format;
		return cdf_modenum;
	case DPC_MODESTR:
		*user = col_fmt->extra.mode.format;
		return cdf_modestr;
	case DPC_MTIME:
		*user = col_fmt->extra.m_time.format;
		return cdf_mtime;
	case DPC_NAME:
		*user = &col_fmt->extra.name;
		return cdf_name;
	case DPC_NLINK:
		*user = col_fmt->extra.nlink.format;
		return cdf_nlink;
	case DPC_SIZE:
		*user = &col_fmt->extra.size;
		return cdf_size;
	case DPC_TYPE:
		return cdf_type;
	case DPC_UIDNUM:
		*user = col_fmt->extra.uidnum.format;
		return cdf_uidnum;
	case DPC_UIDSTR:
		*user = col_fmt->extra.uidnum.format;	/* Yes, numerical again. */
		return cdf_uidstr;
	case DPC_URI_NOFILE:
		*user = dp;
		return cdf_uri;
	default:
		*user = NULL;
	}
	return NULL;
}
