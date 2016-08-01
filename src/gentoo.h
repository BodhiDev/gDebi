/*
** This is the main header file for gentoo. Most source files will include this header,
** since it declares many of the data types that are used. When including this header,
** it should always be the first included file. The preferred order of inclusion is:
** #include "gentoo.h"
** #include <system stuff> (e.g. <stdio.h>, <stdlib.h>, ...)
** #include "more gentoo stuff" (e.g. "fileutil.h", "xmlutil.h", ...)
** #include "me.h" (for a module named "me.c", that is).
**
** The reason why this file should be the first included one is that it fixes some
** system-dependent things which need to go before any system headers are included.
*/

#include "config.h"

#if !(defined __osf__ && defined __alpha__) && !defined __NetBSD__ && !defined __FreeBSD__ && !defined __sgi
 #if !defined __EXTENSIONS__
  #define __EXTENSIONS__
 #endif
 #if !defined _POSIX_C_SOURCE
  #define _POSIX_C_SOURCE	3	/* This is for Solaris. */
 #endif
 #define POSIX_C_SOURCE	3
#endif				/* !__osf__ && !__alpha */

#if defined __osf__ && defined __alpha__		/* On Tru64, this should bring in mknod(). */
 #define _XOPEN_SOURCE_EXTENDED
 #define _OSF_SOURCE					/* For MAXNAMLEN on Tru64. */
#endif

#if !defined _BSD_SOURCE
 #define _BSD_SOURCE					/* For MAXNAMLEN on Linux. */
#endif

#include <dirent.h>
#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <regex.h>
#include <time.h>
#include <unistd.h>

/* The various BSDs seem to have a lot in common. Let's try and use that,
** and save some typing in various tests below. This should probably all
** be replaced by proper Autoconfed stuff at some point.
*/
#if defined __OpenBSD__ || defined __FreeBSD__ || defined __NetBSD__
#define GENTOO_ON_BSD
#endif

/* BSD-specific mounting stuff. */
#if defined GENTOO_ON_BSD
#include <sys/param.h>
#include <sys/mount.h>
#endif

#undef GTK_ENABLE_BROKEN		/* We can't use autoconf to undefine symbols. */
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

/* Fix for < Glib 2.20. */
#ifndef G_GOFFSET_FORMAT
#define G_GOFFSET_FORMAT G_GINT64_FORMAT
#endif

/* Not all systems' <mman.h> seem to define this. */
#if !defined MAP_FAILED
#define	MAP_FAILED	((void *) -1)
#endif

#if !defined MAXNAMLEN			/* Still no MAXNAMLEN? Then make one up. */
#define	MAXNAMLEN	255
#endif

/* ----------------------------------------------------------------------------------------- */

#define	RCNAME	"gentoorc"

/* ----------------------------------------------------------------------------------------- */

/* Sometimes, this does good stuff. I hope. That's about all you can do with GNU tools,
** since actually understanding them requires way more time than I'm willing to spend.
*/
#include "gnu-gettext.h"

#if defined ENABLE_NLS
/* This provides the _() and N_() macros used to mark strings for translation. */
#include <glib/gi18n.h>
#else
/* Provide transparent definitions if NLS is disabled. */
#define	_(str)	str
#define	N_(str)	str
#endif		/* ENABLE_NLS */

/* ----------------------------------------------------------------------------------------- */

typedef struct MainInfo		MainInfo;
typedef struct DirPane		DirPane;
typedef struct CmdSeq		CmdSeq;

/* These are various subsystems whose includes we need here, since they define datatypes
** that are needed below. This is a good point to include them, since MainInfo has been
** introduced (above).
*/
#include "buttonlayout.h"
#include "keyboard.h"
#include "controls.h"
#include "iconutil.h"
#include "queue.h"
#include "cmdarg.h"
#include "styles.h"
#include "dirhistory.h"
#include "window.h"
#include "menus.h"
#include "sizeutil.h"

#define	KEY_NAME_SIZE	(32)

#define	CSQ_NAME_SIZE	(32)

/* FileType-related constants. */
enum {
	FT_NAME_SIZE	= (32),
	FT_SUFFIX_SIZE	= (32),
	FT_NAMERE_SIZE	= (64),
	FT_FILERE_SIZE	= (64),

	FTFL_REQPERM	= (1<<0),
	FTFL_REQSUFFIX	= (1<<1),
	FTFL_NAMEMATCH	= (1<<2),
	FTFL_NAMEGLOB	= (1<<3),
	FTFL_FILEMATCH	= (1<<4),
	FTFL_FILEGLOB	= (1<<5),
	FTFL_NAMENOCASE	= (1<<6),
	FTFL_FILENOCASE	= (1<<7),

	FTPM_SETUID	= (1<<0),
	FTPM_SETGID	= (1<<1),
	FTPM_STICKY	= (1<<2),
	FTPM_READ	= (1<<3),
	FTPM_WRITE	= (1<<4),
	FTPM_EXECUTE	= (1<<5),
};

typedef struct {			/* Used to map stat() info onto names. */
	gchar	name[FT_NAME_SIZE];		/* Human-readable name of filetype (e.g. "GIF image", "MP3 song"). */
	mode_t	mode;				/* Type flags, matched against stat()'s mode info. */
	guint32	flags;				/* Various flags. */
	guint32	perm;				/* Permissions to require. Not orthogonal with mode. */
	gchar	suffix[FT_SUFFIX_SIZE];		/* Suffix to require (if FTFL_REQSUFFIX is set). */
	gchar	name_re_src[FT_NAMERE_SIZE];	/* Regular expression to match against the name (if FTFL_MATCHNAME). */
	regex_t	*name_re;			/* Compiled version of the regular expression. */
	gchar	file_re_src[FT_FILERE_SIZE];	/* RE to match against output of 'file' command (if FTFL_MATCHFILE). */
	regex_t	*file_re;			/* Again, a compiled version of the regular expression. */
	Style	*style;				/* Style to use for items matching this type. */
} FType;

#define	DP_TITLE_SIZE	(32)
#define	DP_FORMAT_SIZE	(16)
#define	DP_DATEFMT_SIZE	(32)
#define	DP_MAX_COLUMNS	(32)

typedef enum {	DPC_NAME, DPC_SIZE, DPC_BLOCKS, DPC_BLOCKSIZE, DPC_MODENUM, DPC_MODESTR,
		DPC_NLINK, DPC_UIDNUM, DPC_UIDSTR, DPC_GIDNUM, DPC_GIDSTR, DPC_DEVICE, DPC_DEVMAJ, DPC_DEVMIN,
		DPC_ATIME, DPC_MTIME, DPC_CRTIME, DPC_CHTIME,
		DPC_TYPE, DPC_ICON,
		DPC_URI_NOFILE,
		DPC_NUM_TYPES } DPContent;

typedef struct {
	guint	show_type : 1;			/* Append type-character (from "@ / * | =") to names? */
	guint	show_linkname : 1;		/* Append " -> destination" on symbolic links? */
} DC_Name;

typedef struct {
	SzUnit	unit;
	guint	ticks : 1;
	gchar	tick;
	gint	digits;
	gchar	dformat[DP_FORMAT_SIZE];	/* Hidden from user, not saved, built by config code. */
	guint	dir_show_fs_size : 1;		/* Show filesystem size for directories? */
} DC_Size;

typedef struct {
	gchar	format[DP_FORMAT_SIZE];		/* General numerical formatter (for size, mode, uid, etc). */
} DC_Fmt;

typedef struct {
	gchar	format[DP_DATEFMT_SIZE];	/* A strftime() format specifier. */
} DC_Time;

typedef union {
	DC_Name	name;
	DC_Size	size;
	DC_Fmt	blocks, blocksize;
	DC_Fmt	mode, nlink, uidnum, gidnum, device, devmaj, devmin;
	DC_Time	a_time, m_time, cr_time, ch_time;
} DpCExtra;

typedef struct {
	gchar			title[DP_TITLE_SIZE];
	DPContent		content;
	DpCExtra		extra;		/* Content-specific flags. */
	GtkJustification	just;
	gint			width;
} DpCFmt;

typedef enum { DPS_DIRS_FIRST = 0, DPS_DIRS_LAST, DPS_DIRS_MIXED } SortMode;

typedef struct {
	DPContent	content;		/* The content type we wish to sort on. */
	SortMode	mode;			/* Controls placement of directories. */
	gboolean	invert;			/* If set, we sort backwards (Z-A). */
	gboolean	nocase;			/* Set for case-insensitive string comparisons. Lame! */
} DPSort;

typedef enum { SBP_IGNORE = 0, SBP_LEFT, SBP_RIGHT } SBarPos;

#define	FONT_MAX	128

typedef struct {
	guint		num_columns;
	DpCFmt		format[DP_MAX_COLUMNS];
	DPSort		sort;
	gchar		def_path[PATH_MAX];
	gboolean	path_above;		/* Set to get the path entry above the actual pane. */
	gboolean	hide_allowed;		/* Set to enable hiding (default). */
	gboolean	scrollbar_always;	/* Set to always show scrollbar, regardless of # of entries. */
	gboolean	huge_parent;		/* Set to enable huge, tall, Opus-like parent button. */
	SBarPos		sbar_pos;		/* Position of scrollbar. */
	gboolean	set_font;		/* Set to override GTK+'s default font. */
	gchar		font_name[FONT_MAX];	/* Last-set font name, remains valid even when set_font == FALSE. */
	gboolean	rubber_banding;		/* Set to enable GTK+'s "rubberbanding" selection. */
} DPFormat;

typedef enum { DPORIENT_HORIZ = 0, DPORIENT_VERT } DpOrient;
typedef enum { DPSPLIT_FREE = 0,   DPSPLIT_RATIO, DPSPLIT_ABS_LEFT, DPSPLIT_ABS_RIGHT } DpSplit;

/* Allow user to control how gentoo should allocate window space between the panes. */
typedef struct {
	DpOrient	orientation;
	DpSplit		mode;
	gdouble		value;		/* Parameter interpreted depending on mode. */
} DPPaning;

typedef struct {
	gboolean	select;			/* Remember selections? */
	gboolean	save;			/* Save history lists on exit? */
} DPHistory;

/* ----------------------------------------------------------------------------------------- */

/* General command row flags. */
enum {
	CGF_RUNINBG = (1<<0),		/* Run in command in background? */
	CGF_KILLPREV = (1<<1),		/* Kill previous instance of program (only background)? */
	CGF_GRABOUTPUT = (1<<2),	/* Capture the output into a special window? */
	CGF_SURVIVE = (1<<3)		/* Survive when gentoo quits (only background)? */
};

/* These flags are for the before- and after-flag fields. */
enum {
	CBAF_RESCAN_SOURCE = (1<<0),		/* Rescan the source dir pane? */
	CBAF_RESCAN_DEST = (1<<1),
	CBAF_CD_SOURCE = (1<<2),		/* These save some config work. */
	CBAF_CD_DEST = (1<<3),
	CBAF_REQSEL_SOURCE = (1<<4),		/* Command won't run if there's no source selection when invoked. */
	CBAF_REQSEL_DEST = (1<<5)		/* Command won't run if there's no destination selection when invoked. */
};

typedef struct {		/* Extra info for external commands. */
	guint32	gflags;			/* General flags. */
	guint32	baflags[2];		/* Before and after flags. */
} CX_Ext;

typedef enum {	CRTP_BUILTIN, CRTP_EXTERNAL,
		CRTP_NUM_TYPES } CRType;


typedef struct {		/* A command "row". */
	CRType	type;			/* The type of the row. */
	GString	*def;			/* The row definition string. */
	guint32	flags;			/* Flags common to all types. */
	union {				/* Type-specific row info. */
	CX_Ext	external;		/* Extra info for external commands. */
	} extra;
} CmdRow;

#define	CSFLG_REPEAT	(1<<0)		/* Repeat sequence until no selection? */

struct CmdSeq {
	gchar	name[CSQ_NAME_SIZE];	/* Name of this command sequence, really. */
	guint32	flags;			/* Flags for this sequence. */
	GList	*rows;			/* List of CmdRow definition rows. */
};

typedef struct {			/* Information about commands lives here. */
	GHashTable	*builtin;		/* Built-in commands (actually CmdDesc structs -- see cmdseq.c). */
	GHashTable	*cmdseq;		/* Command sequences. */
} CmdInfo;

/* ----------------------------------------------------------------------------------------- */

#define	BTN_LABEL_SIZE		(32)
#define	BTN_TOOLTIP_SIZE	(64)	/* Arbitrary, as always. */

typedef struct {
	GList	*sheets;			/* List of button sheets. */
} ButtonInfo;

typedef struct {			/* Options for overwrite-confirmation dialog module. */
	guint	show_info : 1;			/* Show info (sizes & dates) for conflicting files? */
	gchar	datefmt[DP_DATEFMT_SIZE];	/* How to format dates? */
} OptOverwrite;

typedef enum { HIDE_NONE, HIDE_DOT, HIDE_REGEXP } HMode;

typedef struct {			/* Info about files hidden from user. Not ignored, just hidden. */
	HMode	mode;
	gchar	hide_re_src[MAXNAMLEN];		/* Regular expression; matches are hidden if mode == HIDE_REGEXP. */
	regex_t	*hide_re;			/* Compiled version of the RE. */
	guint	no_case : 1;			/* Ignore case in the RE? */
} HideInfo;

typedef enum {	PTID_ICON = 0, PTID_GTKRC, PTID_FSTAB, PTID_MTAB,
		PTID_NUM_PATHS } PathID;

typedef struct {				/* Miscellanous paths. */
	GString		*path[PTID_NUM_PATHS];		/* Just one GString per path, that's all. */
	HideInfo	hideinfo;			/* Info about which files should be hidden. */
} PathInfo;

typedef struct {			/* Configuration information for dialogs. */
	GtkWindowPosition	pos;		/* How should dialogs be positioned? */
} DialogInfo;

typedef enum {
	ERR_DISPLAY_STATUSBAR,
	ERR_DISPLAY_TITLEBAR,
	ERR_DISPLAY_DIALOG
} ErrDisplay;

typedef struct {
	ErrDisplay	display;		/* How should error (and status messages) be shown? */
	gboolean	beep;			/* Do a gdk_beep() on error? */
} ErrInfo;

typedef struct {
	GHashTable	*ignored;		/* Keyed on 'tag', if present ignore the dialog. */
} NagInfo;

#define	CFLG_CHANGED	(1<<0)		/* Config has changed. Set on "OK", cleared by cfg_save_all(). */

typedef struct {			/* Holds all configuration info. */
	guint32		flags;
	GList		*type;			/* List of types (FType structures). */
	StyleInfo	*style;			/* Opaque style container. */
	DPFormat	dp_format[2];
	DPPaning	dp_paning;		/* Controls how the paned view acts. */
	DPHistory	dp_history;		/* History list option. */
	OptOverwrite	opt_overwrite;
	MenuInfo	*menus;
	ButtonInfo	buttons;
	ButtonLayout	*buttonlayout;
	CmdInfo		commands;
	PathInfo	path;
	WinInfo		*wininfo;
	DialogInfo	dialogs;
	CtrlInfo	*ctrlinfo;
	ErrInfo		errors;
	gboolean	(*dir_filter)(const gchar *name);
	NagInfo		nag;
} CfgInfo;

typedef struct GuiInfo	GuiInfo;

typedef struct {
	GVfs		*vfs;
} VfsInfo;

struct MainInfo {				/* gentoo's single most central data structure. */
	gchar		**run_commands;		/* From the command line --run option, kept first for initializer in main(). */
	VfsInfo		vfs;
	CfgInfo		cfg;
	GuiInfo		*gui;
	IconInfo	*ico;
	QueueInfo	*que;
};

/* -- Directory content data structures -- */

/* This is used for complete-as-you-type of paths. */
typedef struct {
	gchar			prefix[PATH_MAX];	/* The path prefix that the GtkCompletion holds strings for. */
	guint			change_sig;		/* Signal for change handler on path entry widget. */
	GtkEntryCompletion	*compl;			/* A GTK+ 2.0 completion object, attached to the pane's path entry. */
} PathComplete;

enum {	DPRF_HAS_SIZE	 = 1 << 0,		/* Set when a row has a "real" size (not set for directories unless GetSize:d). */
	DPRF_LINK_EXISTS = 1 << 1,		/* Set when a symlink's target exists. */
	DPRF_LINK_TO_DIR = 1 << 2		/* Set when symlink points at (valid) directory. Implies DPRF_LINK_EXISTS. */
};

typedef struct {			/* A filename, in both on-disk and display formats. */
	const gchar	*disk;			/* On-disk name, as read during directory scanning. */
	const gchar	*display;		/* Display, as returned by glib (generally, this will be UTF-8). */
} DRName;

typedef struct {			/* Representation of a single line in a directory (a file, typically). */
	DRName		dr_names;		/* Actual filename, in two representations. */
	DRName		dr_linknames;		/* Link name, again in two representations. */
	struct stat	dr_lstat;		/* From a call to lstat(). */
	struct stat	*dr_stat;		/* If link, this holds link target stats. Else, it points at dr_lstat. */
	guint32		dr_flags;		/* Misc. flags. */
	const FType	*dr_type;		/* Type information. */
} DirRow;

typedef GtkTreeIter	DirRow2;

typedef struct {
	guint		num_dirs, num_files;
	guint64		num_bytes;
} SelInfo;

typedef struct {			/* Some trivial file system information. Updated on rescan. */
	gboolean	valid;			/* Set if the structure's contents are valid. */
	guint64		fs_size;		/* Size of filesystem, in bytes. */
	guint64		fs_free;		/* Free bytes in this filesystem. */
} FsInfo;

#define	URI_MAX		(2000)			/* This should only rarely be used, and hopefully ease out once GIO gets more mature. */

typedef struct {
	gchar		path[URI_MAX];		/* Contents apply to this path. */
	gchar		*pathd;			/* Dynamically allocated display version of the path. Beware! */
	GtkListStore	*store;			/* Actual ListStore holding the pane's contents. Insert and clear here. */
	GFile		*root;			/* A GFile representing the current location shown. */
	gboolean	is_local;		/* Is the root considered local? Used to control 'file' etc. */
	guint		num_rows;		/* Number of valid rows. */
	guint		tot_dirs, tot_files;	/* Total number of entries in directory. */
	guint64		tot_bytes;		/* Sum of all sizes, in bytes. */
	struct stat	*stat;			/* Info about link targets. */
	gsize		stat_alloc, stat_use;	/* Allocated and used link stat structs. */
	SelInfo		sel;
	FsInfo		fs;
} DirContents;

/* -- Main DirPane structure --------------------------------------------------------------- */

struct DirPane {
	MainInfo	*main;			/* Handy to have around. */
	guint		index;			/* Index of *this* pane, in the grand scheme of things. */
	GtkWidget	*vbox;
	GtkWidget	*notebook;		/* Notebook holding (normally) path widgetry. */
	GtkWidget	*parent;		/* The common parent button. */
	GtkWidget	*hparent;		/* The "huge" parent button, if enabled in config. */
	GtkWidget	*path;			/* GTK+ 2.0 GtkComboBox showing path and path history. */
	PathComplete	complete;		/* Information for completing the path. */
	GtkWidget	*hide;			/* A toggle button showing (and controlling) hide status. Focus target! */
	GtkWidget	*scwin;			/* Scrolling window for clist to live in. */
	GtkWidget	*view;			/* GtkTreeView for viewing the pane. */
	GtkWidget	*menu_top;		/* The top-level menu, typically shown by right-clicking. */
	GtkWidget	*mitem_action;		/* The item in the top-level menu that contains the actions submenu. */
	GtkWidget	*menu_action;		/* The menu containing the intersection of all available actions on the selection. */
	DirContents	dir;			/* Contents of directory (list of names etc). */
	DirHistory	*hist;			/* Historic data about previously visited directories. */
	gint		last_row;		/* The last clicked row. */
	gint		last_row2;		/* The second last clicked row. */
	gint		dbclk_row;		/* The row that was double clicked, or -1. */
	guint		sig_sel_changed;	/* Signal for the "changed" event on the view's GtkTreeSelection. */
	guint		sig_path_activate;	/* Signal for the "activate" event on path entry combo box. */
	guint		sig_path_changed;	/* Signal for the "changed" event on the path entry combo box. */
};

/* -- Graphic user interface stuff --------------------------------------------------------- */

struct GuiInfo {
	GtkWidget	*window;
	guint		sig_main_configure;
	guint		sig_main_delete;
	KbdContext	*kbd_ctx;		/* Keyboard context, for shortcuts. */
	GtkWidget	*vbox;			/* Vbox that contains entire GUI. */
	GtkWidget	*top;			/* A label showing status (selections, free space, etc). */
	GtkWidget	*panes;			/* GtkPaned widget holding DirPanes. */
	guint		sig_pane_notify;	/* Signal handler ID for notify::position. */
	GtkWidget	*middle;		/* A box holding entire middle part. */
	GtkWidget	*bottom;		/* Bottom part of GUI. */
	DirPane		pane[2];
	DirPane		*cur_pane;

	gboolean	evt_button_valid;
	GdkEventButton	evt_button;
};

/* ----------------------------------------------------------------------------------------- */

/* These are defined in the main "gentoo.c" module, but really shouldn't be. Until they
** get a module of their own, they need to be prototyped like this. :(
*/
extern void	rebuild_top(MainInfo *min);
extern void	rebuild_middle(MainInfo *min);
extern void	rebuild_bottom(MainInfo *min);
