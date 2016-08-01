/*
** 1998-09-25 -	Header for the new command definition parser. Very useful.
*/

extern gchar **	cpr_parse(MainInfo *min, const gchar *def);
extern void	cpr_free(gchar **argv);
