/*
** 1999-02-03 -	Header for the module implementing the ViewText and ViewHex commands.
*/

extern gint	cmd_viewtext(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);

/* Since the three related commands are in the same module, they can share one config
** structure, making the user interface cleaner. Nice.
*/
extern void	cfg_viewtext(MainInfo *min);
