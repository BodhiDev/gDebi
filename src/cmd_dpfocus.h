/*
** 1999-05-10 -	Header for the dirpane focus manipulation command DpFocus.
*/

extern gboolean	dpf_get_fake_select(void);
extern gboolean	dpf_get_focus_select(void);

extern gint	cmd_dpfocus(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern void	cfg_dpfocus(MainInfo *min);
