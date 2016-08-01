/*
** 1998-08-12 -	These config modules really have ridiculous header files, but...
*/

extern const CfgModule * ctp_describe(MainInfo *min);
extern GList *		 ctp_get_types(void);
extern void		 ctp_replace_style(const gchar *from, Style *to);
extern void		 ctp_relink_styles(const StyleInfo *from, const StyleInfo *to);
