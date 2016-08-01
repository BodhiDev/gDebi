/*
** 2002-08-25 -	Configuration for dialogs. This is very stealthy; the actual GUI is not here,
**		it's so small I put it on the Windows page instead; it makes sense too. But
**		I can't save from there; the loader won't find it due to the XML structure in
**		use. So, we create a little "fake" config page, only for the file I/O.
*/

#include "gentoo.h"

#include "cfg_module.h"
#include "cfg_dialogs.h"

#define	NODE	"Dialogs"

/* ----------------------------------------------------------------------------------------- */

static gint save(MainInfo *min, FILE *out)
{
	xml_put_node_open(out, "Dialogs");
	switch(min->cfg.dialogs.pos)
	{
		case GTK_WIN_POS_NONE:
			xml_put_text(out, "pos", "none");
			break;
		case GTK_WIN_POS_MOUSE:
			xml_put_text(out, "pos", "mouse");
			break;
		case GTK_WIN_POS_CENTER:
			xml_put_text(out, "pos", "center");
			break;
		default:
			;
	}
	xml_put_node_close(out, "Dialogs");

	return TRUE;
}

static void load(MainInfo *min, const XmlNode *node)
{
	const gchar	*pos = NULL;

	min->cfg.dialogs.pos = GTK_WIN_POS_NONE;
	if(xml_get_text(node, "pos", &pos))
	{
		if(strcmp(pos, "none") == 0)
			min->cfg.dialogs.pos = GTK_WIN_POS_NONE;
		else if(strcmp(pos, "mouse") == 0)
			min->cfg.dialogs.pos = GTK_WIN_POS_MOUSE;
		else if(strcmp(pos, "center") == 0)
			min->cfg.dialogs.pos = GTK_WIN_POS_CENTER;
		else
			fprintf(stderr, "**DIALOGS: Unknown positioning mode \"%s\"\n", pos);
	}
}

/* ----------------------------------------------------------------------------------------- */

const CfgModule * cdl_describe(MainInfo *min)
{
	static const CfgModule	desc = { NODE, NULL, NULL, NULL, save, load, NULL };

	return &desc;
}
