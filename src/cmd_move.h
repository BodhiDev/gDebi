/*
** 1998-05-30 -	Header file for the MOVE module. Really simple.
*/

/* Top-level moving functions, for external use. */
extern gboolean	move_gfile(MainInfo *min, DirPane *src, DirPane *dst, const GFile *sfile, const GFile *dfile, gboolean progress, GError **err);

extern gint	cmd_move(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca);
