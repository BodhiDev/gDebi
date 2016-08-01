/*
** 2002-05-31 -	Interface for the (temporary) button layout module.
*/

#if !defined BUTTON_LAYOUT_H
#define	BUTTON_LAYOUT_H

#include "xmlutil.h"

typedef struct ButtonLayout	ButtonLayout;

typedef enum { BTL_SEP_NONE = 0, BTL_SEP_SIMPLE, BTL_SEP_PANED } BtlSep;

extern ButtonLayout *	btl_buttonlayout_new(void);
extern ButtonLayout *	btl_buttonlayout_new_copy(const ButtonLayout *original);
extern void		btl_buttonlayout_copy(ButtonLayout *dest, const ButtonLayout *src);
extern void		btl_buttonlayout_destroy(ButtonLayout *btl);

extern void		btl_buttonlayout_set_right(ButtonLayout *btl, gboolean right);
extern gboolean		btl_buttonlayout_get_right(const ButtonLayout *btl);

extern void		btl_buttonlayout_set_separation(ButtonLayout *btl, BtlSep sep);
extern BtlSep		btl_buttonlayout_get_separation(const ButtonLayout *btl);

extern GtkWidget *	btl_buttonlayout_pack(const ButtonLayout *btl, GtkWidget *sheet_default, GtkWidget *sheet_shortcuts);

extern void		btl_buttonlayout_save(const ButtonLayout *btl, FILE *out);
extern ButtonLayout *	btl_buttonlayout_load(MainInfo *min, const XmlNode *node);

#endif		/* BUTTON_LAYOUT_H */
