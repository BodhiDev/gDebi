/*
** 1998-09-17 -	Header for the overwrite protection system. Sounds cool, eh?
*/

enum { OVWF_NO_RECURSE_TEST = 1 << 0 };

typedef enum { OVW_SKIP, OVW_PROCEED, OVW_PROCEED_FILE, OVW_PROCEED_DIR, OVW_CANCEL } OvwRes;

extern void	ovw_overwrite_begin(MainInfo *min, const gchar *desc, guint32 flags);
extern OvwRes	ovw_overwrite_file(MainInfo *min, const gchar *old_file, const gchar *new_file);
extern OvwRes	ovw_overwrite_unary_name(DirPane *dst, const gchar *dst_name);
extern OvwRes	ovw_overwrite_unary_file(DirPane *dst, const GFile *dst_file);
extern void	ovw_overwrite_end(MainInfo *min);
