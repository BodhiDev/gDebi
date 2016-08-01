/*
** 1999-04-05 -	Configuration module to deal with individual built-in commands'
**		configuration. Deep, huh? Relies heavily on the "cmdseq_config"
**		module.
*/

#include "gentoo.h"

#include "cmdseq_config.h"

#include "configure.h"
#include "cfg_module.h"

#include "cfg_cmdcfg.h"

#define	NODE	"CommandConfig"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	CmdCfg		*cmc;
	gpointer	instance;
	GtkWidget	*gui;
} CmdPair;

typedef struct {
	GtkWidget	*vbox;			/* This is required. */

	MainInfo	*min;
	CmdPair		*cmd;			/* Vector of cmc-instance pairs. */
	guint		cmd_num;		/* Length of above vector. */
	guint		cmd_index;		/* Current, used during setup. */
} P_CmdCfg;

static P_CmdCfg	the_page;

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-05 -	Create editing instances for all registered cmc's out there. */
static void create_instance(CmdCfg *cmc, gpointer user)
{
	P_CmdCfg	*page = user;

	page->cmd[page->cmd_index].cmc = cmc;
	page->cmd[page->cmd_index].instance = cmc_instance_new_from_base(cmc);
	page->cmd[page->cmd_index].gui = NULL;
	page->cmd_index++;
}

/* 1999-04-05 -	Initialize the command config interface. Actually quite simple. */
static GtkWidget * ccc_init(MainInfo *min, gchar **name)
{
	P_CmdCfg	*page = &the_page;
	const gchar	*label;
	guint		i;

	page->min = min;
	page->cmd_num = cmc_config_registered_num();
	page->cmd = g_malloc(page->cmd_num * sizeof *page->cmd);
	page->cmd_index = 0;
	cmc_config_registered_foreach(create_instance, page);

	page->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	gtk_widget_show_all(page->vbox);
	cfg_tree_level_begin(_("Options"));
	for(i = 0; i < page->cmd_num; i++)
	{
		label = cmc_config_get_name(page->cmd[i].cmc);
		page->cmd[i].gui = gtk_label_new("(dummy)");
		cfg_tree_level_append(label, page->cmd[i].gui);
	}
	cfg_tree_level_end();
	cfg_tree_level_end();
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-05 -	The configuration window is opening up; make sure our page is up-to-date.
**		This involves refreshing the editing data from the base instances, and
**		building new widgets.
*/
static void ccc_update(MainInfo *min)
{
	P_CmdCfg	*page = &the_page;
	GtkWidget	*subpage;
	guint		i;

	for(i = 0; i < page->cmd_num; i++)
	{
		cmc_instance_copy_from_base(page->cmd[i].cmc, page->cmd[i].instance);
		if((subpage = cmc_instance_build(page->cmd[i].cmc, page->cmd[i].instance)) != NULL)
		{
			cfg_tree_level_replace(page->cmd[i].gui, subpage);
			page->cmd[i].gui = subpage;
		}
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-05 -	Accept the current settings, thus copying our editing instances over the
**		commands' base ones
*/
static void ccc_accept(MainInfo *min)
{
	P_CmdCfg	*page = &the_page;
	guint		i;

	for(i = 0; i < page->cmd_num; i++)
	{
		if(cmc_instance_get_modified(page->cmd[i].cmc, page->cmd[i].instance))
			cmc_instance_copy_to_base(page->cmd[i].cmc, page->cmd[i].instance);
	}
}

/* ----------------------------------------------------------------------------------------- */

static void cmdcfg_save(CmdCfg *cmc, gpointer user)
{
	cmc_config_base_save(cmc, user);
}

/* 1999-04-05 -	Write command configuration data into open file at <out>. Note that this does
**		not use the editing instances, since they are not necessarily up-to-date.
*/
static gint ccc_save(MainInfo *min, FILE *out)
{
	xml_put_node_open(out, NODE);
	cmc_config_registered_foreach(cmdcfg_save, out);
	xml_put_node_close(out, NODE);

	return 1;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-05 -	Look for node with <cmc>'s name in XML tree at <user>, and load (parse) the
**		data into the base instance.
*/
static void cmdcfg_load(CmdCfg *cmc, gpointer user)
{
	const XmlNode	*tree = user, *data;

	if((data = xml_tree_search(tree, cmc_config_get_name(cmc))) != NULL)
		cmc_config_base_load(cmc, data);
}

/* 1999-04-05 -	Load command config data from tree rooted at <node>. As all other config loaders,
**		this actually does *not* load into the editing copies of the data being configured,
**		but rather into the actual config data in use (the base instances here).
*/
static void ccc_load(MainInfo *min, const XmlNode *node)
{
	cmc_config_registered_foreach(cmdcfg_load, (gpointer) node);
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-05 -	That ever-present page describing function. */
const CfgModule * ccc_describe(MainInfo *min)
{
	static const CfgModule	desc = { NODE, ccc_init, ccc_update, ccc_accept, ccc_save, ccc_load, NULL };

	return &desc;
}
