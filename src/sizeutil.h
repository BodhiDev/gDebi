/*
** 1998-08-31 -	Header for the pretty cool module called "sizeutil".
*/

#if !defined SIZEUTIL_H
#define	SIZEUTIL_H

/* This enum must be 1:1 with the integers from 0..(SZE_NUM_UNITS-1), to simplify life elsewhere. */
typedef enum {
	SZE_NONE, SZE_BYTES_NO_UNIT, SZE_BYTES,
	SZE_KB, SZE_MB, SZE_GB, SZE_TB, SZE_AUTO,
	SZE_NUM_UNITS
} SzUnit;

extern void	sze_initialize(void);

extern const gchar *	sze_get_unit_label(SzUnit unit);

extern const gchar *	sze_get_unit_config_name(SzUnit unit);
extern SzUnit		sze_parse_unit_config_name(const gchar *name);

extern goffset		sze_get_offset(const gchar *buf);
extern gint		sze_put_offset(gchar *buf, gsize buf_max, goffset offset, SzUnit unit, guint precision, gchar tick);

#endif		/* SIZEUTIL_H */
