/*
** 1999-05-06 -	Header for the built-in command argument parsing/handling module.
*/

#if !defined CMDARG_H
#define	CMDARG_H

/* An opaque type that acts as a "handle" on the arguments given to a command. */
typedef struct CmdArg	CmdArg;

/* These are never called by individual command implementations, only by the
** main cmdseq command execution core. Gee, that sounded cool. :)
*/
extern CmdArg *		car_create(gchar **argv);
extern void		car_destroy(CmdArg *ca);

/* These are called by command implementations, to find out about the command
** argument keywords provided by the user. They all react very sanely to being
** called with a NULL <ca> argument.
*/
extern const gchar *	car_keyword_get_value(const CmdArg *ca, const gchar *keyword, const gchar *default_value);
extern gint		car_keyword_get_integer(const CmdArg *ca, const gchar *keyword, gint default_value);
extern gint		car_keyword_get_enum(const CmdArg *ca, const gchar *keyword, gint default_value, ...);
extern guint		car_keyword_get_boolean(const CmdArg *ca, const gchar *keyword, guint default_value);

/* These deal with the "bare" arguments, i.e. those that are NOT of the form
** keyword=value. Useful when a command needs a single "core" argument, that
** is not conceptually an option or modifier.
*/
extern guint		car_bareword_get_amount(const CmdArg *ca);
extern gboolean		car_bareword_present(const CmdArg *ca, const gchar *word);
extern const gchar *	car_bareword_get(const CmdArg *ca, guint index);
extern guint		car_bareword_get_enum(const CmdArg *ca, guint index, guint default_value, ...);

#endif		/* CMDARG_H */
