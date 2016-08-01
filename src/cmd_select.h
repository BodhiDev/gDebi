/*
** 1998-05-29 -	One must always have a header.
** 1998-06-04 -	Extended to include support for selecting none and toggling,
**		and also renamed to cmd_select.h.
*/

extern gint	cmd_selectrow(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);

extern gint	cmd_selectall(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_selectnone(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_selecttoggle(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_selectre(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_selectext(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_selecttype(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);

extern gint	cmd_selectsuffix(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
extern gint	cmd_selecttype(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);

#if 0
extern gint	cmd_selectcmp(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
#endif

extern gint	cmd_unselectfirst(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);

extern gint	cmd_selectshell(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
