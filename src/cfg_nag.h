/*
** 2009-12-28 -	The nagging is configurable. What a dream! This module, for simplicity's sake, also implements the
**		"API" used by the nag dialog to query and set ignore-flags.
*/

#include "cfg_module.h"

extern void			cng_initialize(NagInfo *ni);

extern gsize			cng_num_ignored(const NagInfo *ni);
extern gboolean			cng_is_ignored(const NagInfo *ni, const gchar *tag);
extern void			cng_ignore(NagInfo *ni, const gchar *tag);
extern void			cng_reset(NagInfo *ni);

extern const CfgModule *	cng_describe(MainInfo *min);
