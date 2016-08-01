/*
** 1998-05-25 -	Header for the internal SWAP command.
*/

extern gint	cmd_activateother(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_activateleft(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_activateright(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);

extern gint	cmd_activatepush(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_activatepop(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
