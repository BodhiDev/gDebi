/*
** 1998-12-14 -	A new module to pretty up the configuration-invocation, and also
**		to implement a ConfigSave command.
*/

#include "gentoo.h"
#include "cmdseq_config.h"
#include "configure.h"
#include "cmd_configure.h"

/* ----------------------------------------------------------------------------------------- */

typedef struct {			/* Options used by the "Configure" command. */
	gboolean	modified;
	gboolean	auto_save;						/* Automatic save, suppresses the dialog on quit. */
} OptConfigure;

static OptConfigure configure_options;
static CmdCfg	*configure_cmc = NULL;

/* 1998-12-14 -	This is the classic "Configure" command, just given a standard prototype. */
gint cmd_configure(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	cfg_configure(min);

	return TRUE;
}

/* 1998-12-14 -	This command causes the configuration to be saved. */
gint cmd_configuresave(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	cfg_save_all(min);

	return TRUE;
}

gboolean cmd_configure_autosave(void)
{
	return configure_options.auto_save;
}

void cfg_configurecmd(MainInfo *min)
{
	if(configure_cmc == NULL)
	{
		configure_options.auto_save = FALSE;

		configure_cmc = cmc_config_new("Configure", &configure_options);
		cmc_field_add_boolean(configure_cmc, "modified", NULL, offsetof(OptConfigure, modified));
		cmc_field_add_boolean(configure_cmc, "auto_save", _("Automatically Save Changed Configuration on Exit?"), offsetof(OptConfigure, auto_save));
		cmc_config_register(configure_cmc);
	}
}
