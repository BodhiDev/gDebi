/*
** 1998-05-25 -	Header for the DirPane formatting module.
*/

void		dpf_initialize(void);

const gchar *	dpf_get_content_name(DPContent content);
DPContent	dpf_get_content_from_name(const gchar *name);
const gchar *	dpf_get_content_mnemonic(DPContent content);
DPContent	dpf_get_content_from_mnemonic(const gchar *name);
const gchar *	dpf_get_content_title(DPContent content);

GtkWidget *	dpf_get_content_combo_box(GCallback func, gpointer user);

void		dpf_set_default_format(DpCFmt *fmt, DPContent content);
void		dpf_init_defaults(CfgInfo *cfg);

gboolean	dpf_get_content(MainInfo *min, const DirPane *dp, const DirRow2 *row, DPContent content, gchar *out, gsize max);

void		dpf_cell_set_style_colors(GtkCellRenderer *r, const Style *st, gboolean do_fg, gboolean do_bg);

GtkTreeCellDataFunc	dpf_get_cell_data_func(DirPane *dp, gint column, gpointer *user);
