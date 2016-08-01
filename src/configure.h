/*
** 1998-05-31 -	Header for the GUI configuration main module.
*/

#if !defined CONFIGURE_H
#define	CONFIGURE_H

/* These are flags useful in calls to cfg_set_flags(). */
enum {
	CFLG_RESCAN_LEFT	= (1 << 0),
	CFLG_RESCAN_RIGHT	= (1 << 1),
	CFLG_RESCAN_BOTH	= (CFLG_RESCAN_LEFT | CFLG_RESCAN_RIGHT),
	CFLG_REDISP_LEFT	= (1 << 2),
	CFLG_REDISP_RIGHT	= (1 << 3),
	CFLG_FLUSH_ICONS	= (1 << 4),
	CFLG_REBUILD_TOP	= (1 << 5),
	CFLG_REBUILD_MIDDLE	= (1 << 6),
	CFLG_REBUILD_BOTTOM	= (1 << 7),
	CFLG_RESET_MOUNT	= (1 << 8),
	CFLG_RESET_KEYBOARD	= (1 << 9),
};

/* These are flags returned by cfg_load_config(). */
enum {
	CLDF_NONE_FOUND		= (1 << 0)		/* There was no config available. */
};

gint	cfg_configure(MainInfo *min);

void	cfg_goto_page(const char *label);

/* Config modules which have internal hierarchy use these from their init() function.
** Simpler modules don't, they just return their single page and set the label, as
** with the old tabs-based layout.
*/
void	cfg_tree_level_begin(const gchar *label);
void	cfg_tree_level_append(const gchar *label, GtkWidget *page);
void	cfg_tree_level_replace(GtkWidget *old, GtkWidget *new);
void	cfg_tree_level_end(void);

void	cfg_save_all(MainInfo *min);

guint32	cfg_load_config(MainInfo *min);

void	cfg_set_flags(guint32 flags);

void	cfg_modified_set(MainInfo *min);
void	cfg_modified_clear(MainInfo *min);

#endif		/* CONFIGURE_H */
