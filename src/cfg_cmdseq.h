/*
** 1998-09-26 -	Another one of those pretty silly header file... This time for the
**		command sequence config page.
*/

const CfgModule *	ccs_describe(MainInfo *);
GHashTable *		ccs_get_current(void);

gboolean		ccs_goto_cmdseq(const char *name);
