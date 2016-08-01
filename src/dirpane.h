/*
** 1998-05-25 -	Header file for the dirpane module. Deals with all aspects of
**		directory panes. Since those are very central to this program,
**		there's quite a lot to deal with.
*/

#if !defined DIRPANE_H
#define	DIRPANE_H

#define	DP_ENTRY(dp)	GTK_ENTRY(gtk_bin_get_child(GTK_BIN(dp->path)))

/* ----------------------------------------------------------------------------------------- */

extern void		dp_initialize(DirPane *dp, size_t num);

extern gboolean		dp_realized(const MainInfo *min);

extern DirPane *	dp_mirror(const MainInfo *min, const DirPane *dp);
extern const gchar *	dp_full_name(const DirPane *dp, const DirRow2 *row);
extern const gchar *	dp_name_quoted(const DirPane *dp, const DirRow2 *row, gboolean path);

extern void		dp_show_stats(DirPane *dp);
extern void		dp_update_stats(DirPane *dp);
extern gboolean		dp_activate(DirPane *dp);

extern void		dp_resort(DirPane *dp);
extern void		dp_redisplay(DirPane *dp);
extern void		dp_redisplay_preserve(DirPane *dp);

extern void		dp_unfocus(DirPane *dp);
extern void		dp_focus_enter_dir(DirPane *dp);

extern gboolean		dp_has_selection(DirPane *dp);
extern gboolean		dp_has_single_selection(const DirPane *dp);
extern gboolean		dp_is_selected(DirPane *dp, const DirRow2 *row);

extern GSList *		dp_get_selection(DirPane *dp);
extern GSList *		dp_get_selection_full(const DirPane *dp);
#if 0
extern void		dp_print_selection(DirPane *dp, const gchar *tag);
#endif
extern void		dp_free_selection(GSList *sel);

extern gboolean		dp_enter_dir(DirPane *dp, const gchar *path);

extern void		dp_rescan(DirPane *dp);
extern void		dp_rescan_post_cmd(DirPane *dp);
extern gboolean		dp_rescan_row(DirPane *dp, const DirRow2 *row, GError **error);

extern void		dp_path_clear(DirPane *dp);
extern void		dp_path_focus(DirPane *dp);
extern void		dp_path_unfocus(DirPane *dp);

extern void		dp_history_set(DirPane *dp, const GList *paths);

extern void		dp_dbclk_row(DirPane *dp, gint row);

extern void		dp_select(DirPane *dp, const DirRow2 *row);
extern void		dp_select_all(DirPane *dp);
extern void		dp_unselect(DirPane *dp, const DirRow2 *row);
extern void		dp_unselect_all(DirPane *dp);
extern void		dp_toggle(DirPane *dp,const DirRow2 *row);

extern void		dp_split_refresh(MainInfo *min);

/* Functionality for letting various modules create alternative widgetry that
** can replace the ordinary parent, path, and hide widget row. ISearch first.
*/
typedef GtkWidget ** (*PageBuilder)(MainInfo *min);

extern guint		dp_pathwidgetry_add(PageBuilder func);
extern GtkWidget **	dp_pathwidgetry_show(DirPane *dp, guint key);


/* Data access functions. This very much exposes that we use a GtkTreeModel to store
** the pane' data, but that is hardly a secret. This is done for performance; not
** needing to pass a DirPane pointer frees up the 'user' pointer in CellRenderer
** data callbacks. See also the dpformat module.
*/
extern GtkTreeModel *	dp_get_tree_model(const DirPane *dp);

extern GFile *		dp_get_file_from_row(const DirPane *dp, const DirRow2 *row);
extern GFile *		dp_get_file_from_name(const DirPane *dp, const gchar *name);
extern GFile *		dp_get_file_from_name_display(const DirPane *dp, const gchar *name);

extern void		dp_row_set_ftype(GtkTreeModel *model, const DirRow2 *row, FType *type);
extern void		dp_row_set_size(GtkTreeModel *model, const DirRow2 *row, guint64 size);
extern void		dp_row_set_flag(GtkTreeModel *model, const DirRow2 *row, guint32 mask);

extern const gchar *	dp_row_get_name(const GtkTreeModel *model, const DirRow2 *row);
extern const gchar *	dp_row_get_name_display(const GtkTreeModel *model, const DirRow2 *row);
extern const gchar *	dp_row_get_name_edit(const GtkTreeModel *model, const DirRow2 *row);
extern goffset		dp_row_get_size(const GtkTreeModel *model, const DirRow2 *row);
extern guint64		dp_row_get_blocks(const GtkTreeModel *model, const DirRow2 *row);
extern guint64		dp_row_get_blocksize(const GtkTreeModel *model, const DirRow2 *row);
extern guint32		dp_row_get_mode(const GtkTreeModel *model, const DirRow2 *row);
extern FType *		dp_row_get_ftype(const GtkTreeModel *model, const DirRow2 *row);
extern GFileType	dp_row_get_file_type(const GtkTreeModel *model, const DirRow2 *row, gboolean target);
extern guint64		dp_row_get_time_accessed(const GtkTreeModel *model, const DirRow2 *row);
extern guint64		dp_row_get_time_changed(const GtkTreeModel *model, const DirRow2 *row);
extern guint64		dp_row_get_time_created(const GtkTreeModel *model, const DirRow2 *row);
extern guint64		dp_row_get_time_modified(const GtkTreeModel *model, const DirRow2 *row);
extern guint32		dp_row_get_gid(const GtkTreeModel *model, const DirRow2 *row);
extern guint32		dp_row_get_uid(const GtkTreeModel *model, const DirRow2 *row);
extern guint32		dp_row_get_nlink(const GtkTreeModel *model, const DirRow2 *row);
extern guint32		dp_row_get_device(const GtkTreeModel *model, const DirRow2 *row);
extern const gchar *	dp_row_get_link_target(const GtkTreeModel *model, const DirRow2 *row);
extern gboolean		dp_row_get_flags(const GtkTreeModel *model, const DirRow2 *row, guint32 mask);
extern gboolean		dp_row_get_can_read(const GtkTreeModel *model, const DirRow2 *row);
extern gboolean		dp_row_get_can_write(const GtkTreeModel *model, const DirRow2 *row);
extern gboolean		dp_row_get_can_execute(const GtkTreeModel *model, const DirRow2 *row);

extern GtkWidget *	dp_build(MainInfo *min, DPFormat *fmt, DirPane *dp);

#endif		/* DIRPANE_H */
