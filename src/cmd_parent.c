/*
** 1998-05-25 -	The PARENT native command, which simply causes the currently active
**		dir pane to go up one level, to its parent directory.
** 1999-07-03 -	Rewrote entire function, should now work a little bit better. I hope.
*/

#include "gentoo.h"

#include <stdlib.h>

#include "cmdseq.h"
#include "dirpane.h"
#include "strutil.h"

#include "cmd_parent.h"

/* ----------------------------------------------------------------------------------------- */

/* 1999-07-03 -	Go up one level in the source pane. Rewritten to add a little more armor
**		plating. Still far from bullet-proof, though...
** 2010-06-05 -	Rewritten to be lot more GIO, no more string-level parent computation.
*/
gint cmd_parent(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	GFile	*parent;
	gchar	*base = NULL;
	gint	ret = 0;

	if(src->dir.root == NULL)
		return 0;

	if(car_keyword_get_boolean(ca, "select", 0) != 0)
		base = g_file_get_basename(src->dir.root);

	parent = g_file_get_parent(src->dir.root);
	if(parent != NULL)
	{
		gchar	*uri;

		if((uri = g_file_get_uri(parent)) != NULL)
		{
			ret = csq_execute_format(min, "DirEnter 'dir=%s'", stu_escape(uri));
			g_free(uri);
			if(base != NULL)
			{
				GtkTreeModel	*tm = dp_get_tree_model(src);
				DirRow2		iter;
				gboolean	valid;

				for(valid = gtk_tree_model_get_iter_first(tm, &iter);
				    valid;
				    valid = gtk_tree_model_iter_next(tm, &iter))
				{
					const gchar	*name = dp_row_get_name(tm, &iter);

					if(strcmp(name, base) == 0)
					{
						dp_select(src, &iter);
						break;
					}
				}
				g_free(base);
			}
		}
	}
	return ret;
}
