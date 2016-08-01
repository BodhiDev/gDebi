/*
** 1998-08-31 -	This little module provides helper functions when dealing with representations
**		of numbers, specifically numbers describing the size of things. For example,
**		this module knows that 1457664 bytes can be expressed conveniently as 1.39 MB.
**		I haven't tried teaching it to make that into the "official" 1.44 MB yet, though...
** 1999-05-09 -	Added explicit support for 64-bit sizes. Might not be very portable. :(
** 2000-02-18 -	Redid handling of 64-bit constants in sze_put_size(). More portability now?
** 2000-07-02 -	Added support for I18N. Fairly hairy.
*/

#include "gentoo.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strutil.h"
#include "sizeutil.h"

/* ----------------------------------------------------------------------------------------- */

/* Information about the various units we support. Note that the "config" names are slighly legacy. */
static const struct {
	const gchar	*unit;
	const gchar	*config;
	const gchar	*label;
	gdouble		multiplier;
} sze_unit_info[] = {
		{ "", "native", N_("Unformatted Size"), 1 },
		{ "", "bytesnounit", N_("Formatted Size, Without Unit") },
		{ N_("bytes"), "bytes", N_("Formatted Size, in Bytes"), 1 },
		{ N_("KB"), "kilobytes", N_("Formatted Size, in Kilobytes"), 1024.0 },
		{ N_("MB"), "megabytes", N_("Formatted Size, in Megabytes"), 1024.0 * 1024.0 },
		{ N_("GB"), "gigabytes", N_("Formatted Size, in Gigabytes"), 1024.0 * 1024.0 * 1024.0 },
		{ N_("TB"), "tb", N_("Formatted Size, in Terabytes"), 1024.0 * 1024.0 * 1024.0 * 1024.0 },
		{ "", "smart", N_("Formatted Size, Automatic Unit"), -1.0 }
	};

/* ----------------------------------------------------------------------------------------- */

const gchar * sze_get_unit_label(SzUnit unit)
{
	return sze_unit_info[unit].label;
}

/* 2010-09-22 -	Given a SzUnit, this returns a unique symbolic name. This name is intended for
**		use when externally representing the SzUnit, i.e. in a configuration file.
*/
const gchar * sze_get_unit_config_name(SzUnit unit)
{
	return sze_unit_info[unit].config;
}

/* 2010-09-22 -	Parse a config name as returned by sze_get_unit_config_name(), and return the
**		corresponding SzeUnit. If the name is unknown, SZE_NONE is returned.
*/
SzUnit sze_parse_unit_config_name(const gchar *name)
{
	size_t	i;

	for(i = 0; i < sizeof sze_unit_info / sizeof *sze_unit_info; i++)
	{
		if(strcmp(sze_unit_info[i].config, name) == 0)
			return (SzUnit) i;
	}
	return SZE_NONE;
}

/* ----------------------------------------------------------------------------------------- */

/* 2010-09-22 -	Skip characters at <s> while <classifier> returns TRUE. */
static const gchar * utf8_skip_class(const gchar *s, gboolean (*classifier)(gunichar ch))
{
	while(*s)
	{
		const gunichar	here = g_utf8_get_char(s);
		if(!classifier(here))
			return s;
		s = g_utf8_next_char(s);
	}
	return s;
}

/* 2010-09-22 -	Attempt to parse an offset from string at <buf>. The string can be UTF-8.
**		* Leading spaces are stripped.
**		* A trailing unit from the set of units supported by this module can appear.
*/
goffset sze_get_offset(const gchar *buf)
{
	gunichar	here;

	buf = utf8_skip_class(buf, g_unichar_isspace);

	here = g_utf8_get_char(buf);
	if(g_ascii_isdigit(here))
	{
		gchar		*end = NULL;
		const gdouble	value = strtod(buf, &end);
		gdouble		multiplier = 1;

		if(end > buf)
		{
			gchar		ubuf[32];
			const gchar	*unit = utf8_skip_class(end, g_unichar_isspace);
			const gchar	*uend = utf8_skip_class(unit, g_unichar_isalpha);
			const gsize	len = uend - unit;

			/* Any unit found? */
			if(uend > unit && len < sizeof ubuf - 1)
			{
				gsize	i;

				memcpy(ubuf, unit, len);
				ubuf[len] = '\0';
				for(i = 0; i < SZE_NUM_UNITS; i++)
				{
					if(strcmp(_(sze_unit_info[i].unit), unit) == 0)
					{
						multiplier = sze_unit_info[i].multiplier;
						break;
					}
				}
			}
			else if(len >= sizeof ubuf)
				g_warning("Unable to parse unit at '%s', too long", unit);
		}
		return value * multiplier;
	}
	return -1;
}

/* 2010-08-08 -	Format an offset, which the is native type used in GIO for file sizes. Since it's
**		pretty much guaranteed to be 64 bits, it's all we need. We pretend it's unsigned.
*/
gint sze_put_offset(gchar *buf, gsize buf_max, goffset value, SzUnit unit, guint precision, gchar tick)
{
	gchar		tmp[32], *fsize, *funit = "";
	gdouble		temp = 0.0;
	static gchar	fmt[16];
	static guint	fmt_precision = 0, fmt_tick = 0;

	/* Check if we need to re-format the formatting string. Quite meta. */
	if(precision != fmt_precision || tick != (gchar) fmt_tick)
	{
		g_snprintf(fmt, sizeof fmt, "%%.%uf %%s", precision);
		fmt_precision = precision;
		fmt_tick = tick;
	}

	switch(unit)
	{
		case SZE_NONE:
			return g_snprintf(buf, buf_max, "%" G_GUINT64_FORMAT, value);
		case SZE_BYTES_NO_UNIT:
			break;
		case SZE_BYTES:
			funit = _("bytes");
			break;
		case SZE_KB:
			temp = value / 1024.0;
			funit = _("KB");
			break;
		case SZE_MB:
			temp = value / (1024.0 * 1024.0);
			funit = _("MB");
			break;
		case SZE_GB:
			temp = value / (1024.0 * 1024.0 * 1024.0);
			funit = _("GB");
			break;
		case SZE_TB:
			temp = value / (1024.0 * 1024.0 * 1024.0 * 1024.0);
			funit = _("TB");
			break;
		case SZE_AUTO:
			if(value < (1 << 10))
				return sze_put_offset(buf, buf_max, value, SZE_BYTES, precision, tick);
			else if(value < (G_GINT64_CONSTANT(1) << 20))
				return sze_put_offset(buf, buf_max, value, SZE_KB, precision, tick);
			else if(value < (G_GINT64_CONSTANT(1) << 30))
				return sze_put_offset(buf, buf_max, value, SZE_MB, precision, tick);
			else if(value < (G_GINT64_CONSTANT(1) << 40))
				return sze_put_offset(buf, buf_max, value, SZE_GB, precision, tick);
			return sze_put_offset(buf, buf_max, value, SZE_TB, precision, tick);
		default:
			return 0;
	}

	/* Nothing in the temporary double? Then just use value as-is. */
	if(temp == 0.0)
	{
		tmp[sizeof tmp - 1] = '\0';
		fsize = stu_tickify(tmp + sizeof tmp - 1, value, tick);
		return g_snprintf(buf, buf_max, "%s %s", fsize, funit);
	}
	/* Now format the floating point version using both ticks and user's precision. Not, eh, super fast. */
	if(tick != 0)
	{
		gchar		tmp2[32];
		gdouble		fraction;
		const gdouble	scale = pow(10, precision);

		/* Round the floating point version to the desired precision. */
		temp *= scale;
		temp += 0.5;
		temp = (gint) temp;
		temp /= scale;

		/* Now split into integer and fraction parts, so we can format properly. */
		fraction = modf(temp, &temp);
		tmp[sizeof tmp - 1] = '\0';
		fsize = stu_tickify(tmp + sizeof tmp - 1, (long) temp, tick);
		/* Format fraction+unit. */
		g_snprintf(tmp2, sizeof tmp2, fmt, fraction, funit);
		return g_snprintf(buf, buf_max, "%s%s", fsize, tmp2 + 1);	/* Skip the parts before the decimal point. */
	}
	return g_snprintf(buf, buf_max, fmt, temp, funit);
}
