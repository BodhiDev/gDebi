/*
** 1998-12-14 -	Header for the configuration commands.
*/

extern gint	cmd_configure(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_configuresave(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);

extern gboolean cmd_configure_autosave(void);

extern void cfg_configurecmd(MainInfo *min);
