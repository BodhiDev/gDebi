/*
** 1998-09-15 -	Header for the buttons module.
** 1999-05-01 -	Huge changes to incorporate the new multi-function button.
*/

#include "xmlutil.h"

typedef struct Button		Button;
typedef struct ButtonRow	ButtonRow;
typedef struct ButtonSheet	ButtonSheet;

typedef enum {	BTN_PRIMARY   = 0, BTN_SECONDARY = 1, BTN_FACES } BtnFace;

/* ----------------------------------------------------------------------------------------- */

/* These are the flags available in each Button. Use the btn_button_XXX_flags() calls
** to access these (a good idea, since nothing else is possible :).
*/
#define	BTF_NARROW		(1<<0)		/* Is the button narrow? */
#define	BTF_SHOW_TOOLTIP	(1<<1)		/* Does the button show its tooltip string? */

/* ----------------------------------------------------------------------------------------- */

extern void		btn_buttoninfo_new_default(MainInfo *min, ButtonInfo *bti);
extern void		btn_buttoninfo_copy(ButtonInfo *dst, ButtonInfo *src);
extern void		btn_buttoninfo_add_sheet(ButtonInfo *bti, ButtonSheet *bsh);
extern void		btn_buttoninfo_clear(ButtonInfo *bti);

extern void		btn_buttoninfo_save(MainInfo *min, const ButtonInfo *bti, FILE *out, const gchar *tag);
extern void		btn_buttoninfo_load(MainInfo *min, ButtonInfo *bti, const XmlNode *node);

/* Main button allocation routine. Probably not useful outside of the buttons module... */
extern Button *		btn_button_new(Button *base, guint num);

/* Set/get functions for per-face properties. */

extern void		btn_button_set_label(Button *btn, BtnFace face, const gchar *label);
extern void		btn_button_set_label_widget(GtkWidget *widget, BtnFace face, const gchar *label);
extern void		btn_button_set_cmdseq(Button *btn, BtnFace face, const gchar *seq);
extern void		btn_button_set_key(Button *btn, BtnFace face, const gchar *key);
extern void		btn_button_set_colors(Button *btn, BtnFace face, const GdkColor *bg, const GdkColor *fg);
extern void		btn_button_set_color_bg(Button *btn, BtnFace face, const GdkColor *bg);
extern void		btn_button_set_color_bg_widget(GtkWidget *widget, BtnFace face, const GdkColor *bg);
extern void		btn_button_set_color_fg(Button *btn, BtnFace face, const GdkColor *fg);
extern void		btn_button_set_color_fg_widget(GtkWidget *widget, BtnFace face, const GdkColor *fg);

/* Get definition from widget instance. */
extern Button *	btn_button_get(GtkWidget *wid);

extern const gchar *	btn_button_get_label(Button *btn, BtnFace face);
extern const gchar *	btn_button_get_cmdseq(Button *btn, BtnFace face);
extern const gchar *	btn_button_get_key(Button *btn, BtnFace face);
extern gboolean		btn_button_get_color_bg(Button *btn, BtnFace face, GdkColor *bg);/* Returns FALSE if color not valid. */
extern gboolean		btn_button_get_color_fg(Button *btn, BtnFace face, GdkColor *fg);
extern const gchar *	btn_button_get_menu(const Button *btn);

extern void		btn_button_set_tooltip(Button *btn, const gchar *tip);
const gchar *		btn_button_get_tooltip(Button *btn);

extern void		btn_button_set_flags(Button *btn, guint32 flags);
extern guint32		btn_button_get_flags(Button *btn);
extern void		btn_button_set_flags_boolean(Button *btn, guint32 mask, gboolean value);
extern gboolean		btn_button_get_flags_boolean(Button *btn, guint32 mask);

extern gboolean		btn_button_is_blank(Button *btn);

extern void		btn_button_clear(Button *btn);
extern void		btn_button_swap(Button *a, Button *b);
extern void		btn_button_copy(Button *dst, Button *src);
extern void		btn_button_copy_colors(Button *dst, Button *src);
extern void		btn_button_destroy(Button *b);

extern ButtonRow *	btn_buttonrow_new(gint width);
extern ButtonRow *	btn_buttonrow_new_default(MainInfo *min);
extern ButtonRow *	btn_buttonrow_copy(ButtonRow *src);
extern void		btn_buttonrow_set_width(ButtonRow *brw, guint width);
extern guint		btn_buttonrow_get_width(ButtonRow *brw);
extern void		btn_buttonrow_destroy(ButtonRow *brw);

extern ButtonSheet *	btn_buttonsheet_new(const gchar *label);
extern ButtonSheet *	btn_buttonsheet_copy(ButtonSheet *src);
extern void		btn_buttonsheet_append_row(ButtonSheet *bsh, ButtonRow *brw);
extern void		btn_buttonsheet_insert_row(ButtonSheet *bsh, ButtonRow *brw, gint width);
extern void		btn_buttonsheet_delete_row(ButtonSheet *bsh, ButtonRow *brw);
extern void		btn_buttonsheet_move_row(ButtonSheet *bsh, ButtonRow *brw, gint delta);
extern guint		btn_buttonsheet_get_height(ButtonSheet *bsh);
extern ButtonSheet *	btn_buttonsheet_get(ButtonInfo *bti, const gchar *label);
extern GtkWidget *	btn_buttonsheet_build(MainInfo *min, ButtonInfo *bti, const gchar *label, gint partial, GCallback func, gpointer user);
extern void		btn_buttonsheet_built_add_keys(MainInfo *min, GtkContainer *sheet, gpointer user);
extern void		btn_buttonsheet_destroy(ButtonSheet *bsh);
