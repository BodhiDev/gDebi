/*
** 1998-12-19 -	Header for the DpHide command implementation.
** 1999-03-15 -	Renamed when I added the recenter command.
*/

extern gint	cmd_dphide(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_dprecenter(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_dpreorient(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_dpmaximize(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_dpgotorow(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_dpfocuspath(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
