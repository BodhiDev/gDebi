/*
** 1998-09-12 -	Header file for the CopyAs and Clone commands.
** 1999-05-30 -	Added SymLinkAs command, too.
*/

extern gint	cmd_copyas(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_clone(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_symlinkas(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_symlinkclone(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
