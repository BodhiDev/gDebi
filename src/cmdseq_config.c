/*
** 1999-04-04 -	Backend support for command sequence configuration. This deals with the specific
**		configuration data for command sequences. This data is used by individual commands,
**		and loaded/saved/visualized by the cfg_cmdseq.c module.
** BUG BUG BUG	Currently handles only "flat", simple data types. Goes a long way, though.
*/

#include "gentoo.h"

#include <stdio.h>
#include <string.h>

#include "color_dialog.h"
#include "dialog.h"
#include "guiutil.h"
#include "list_dialog.h"
#include "odmultibutton.h"
#include "sizeutil.h"
#include "strutil.h"
#include "xmlutil.h"

#include "cmdseq_config.h"

/* ----------------------------------------------------------------------------------------- */

#define	CMC_FIELD_SIZE	(24)

struct CmdCfg {
	gchar		name[CSQ_NAME_SIZE];
	gpointer	base;
	GList		*fields;
};

typedef enum { CFT_INTEGER = 0, CFT_BOOLEAN, CFT_ENUM, CFT_SIZE, CFT_STRING, CFT_PATH } CFType;

typedef struct {
	gint	min, max;
} CFInt;

typedef struct {
	gsize	num;
	gchar	*label[4];		/* Static limits... They rule! */
	gchar	*def;
} CFEnum;

typedef struct {
	gsize	size;
	gunichar	separator;	/* Used for CFT_PATH. */
} CFStr;

typedef struct {
	gsize	min, max;
	gsize	step;			/* Minimum change. */
} CFSize;

typedef struct {
	CFType	type;
	gchar	name[CMC_FIELD_SIZE];
	gchar	*desc;
	gsize	offset;

	union {
	CFInt	integer;
	CFEnum	fenum;
	CFSize	size;
	CFStr	string;
	}	field;
} CField;

/* ----------------------------------------------------------------------------------------- */

/* This holds "registered" command configs; these are the ones that are stored in the config
** file, and dealt with by the command options config page. For now, this will in fact be all
** cmc's created. That will change if I ever add nesting support, though.
*/
static GList	*cmdcfg_list = NULL;

/* ----------------------------------------------------------------------------------------- */

static void	field_destroy(CField *fld);

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-04 -	Create a new, empty, command config descriptor to which fields can then be
**		added. Also, in most cases you'd want to register it. The <base_instance>
**		is the (typically static) base configuration data store.
*/
CmdCfg * cmc_config_new(const gchar *cmdname, gpointer base_instance)
{
	CmdCfg	*cmc;

	cmc = g_malloc(sizeof *cmc);
	g_strlcpy(cmc->name, cmdname, sizeof cmc->name);
	cmc->base   = base_instance;
	cmc->fields = NULL;

	return cmc;
}

/* 1999-04-05 -	Just get the name of a given config descriptor. Typically, this will be the
**		name of the command whose config data is described by it.
*/
const gchar * cmc_config_get_name(CmdCfg *cmc)
{
	if(cmc != NULL)
		return cmc->name;
	return NULL;
}

/* 1999-04-05 -	Save given <cmc>'s base instance to file <out>, in XML format. */
void cmc_config_base_save(CmdCfg *cmc, FILE *out)
{
	if((cmc != NULL) && (out != NULL))
	{
		const GList	*iter;
		CField		*fld;

		xml_put_node_open(out, cmc->name);
		for(iter = cmc->fields; iter != NULL; iter = g_list_next(iter))
		{
			fld = iter->data;

			if(strcmp(fld->name, "modified") == 0)
				continue;
			switch(fld->type)
			{
				case CFT_INTEGER:
					xml_put_integer(out, fld->name, *(gint *) ((gchar *) cmc->base + fld->offset));
					break;
				case CFT_BOOLEAN:
					xml_put_boolean(out, fld->name, *(gboolean *) ((gchar *) cmc->base + fld->offset));
					break;
				case CFT_ENUM:
					xml_put_uinteger(out, fld->name, *(gint *) ((gchar *) cmc->base + fld->offset));
					break;
				case CFT_SIZE:
					xml_put_uinteger(out, fld->name, *(guint *) ((gchar *) cmc->base + fld->offset));
					break;
				case CFT_STRING:
				case CFT_PATH:
					xml_put_text(out, fld->name, (gchar *) cmc->base + fld->offset);
					break;
			}
		}
		xml_put_node_close(out, cmc->name);
	}
}

/* 1999-04-05 -	Load (parse) data from <node> into the base instance of <cmc>. */
void cmc_config_base_load(CmdCfg *cmc, const XmlNode *node)
{
	if((cmc != NULL) && (node != NULL))
	{
		const GList	*iter;
		CField		*fld;

		for(iter = cmc->fields; iter != NULL; iter = g_list_next(iter))
		{
			fld = iter->data;

			if(strcmp(fld->name, "modified") == 0)
				continue;
			switch(fld->type)
			{
				case CFT_INTEGER:
					xml_get_integer(node, fld->name, (gint *) ((gchar *) cmc->base + fld->offset));
					break;
				case CFT_BOOLEAN:
					xml_get_boolean(node, fld->name, (gboolean *) ((gchar *) cmc->base + fld->offset));
					break;
				case CFT_ENUM:
					xml_get_uinteger(node, fld->name, (guint *) ((gchar *) cmc->base + fld->offset));
					break;
				case CFT_SIZE:
					{
						guint	tmp;

						xml_get_uinteger(node, fld->name, &tmp);
						*(gsize *) ((gchar *) cmc->base + fld->offset) = tmp;
					}
					break;
				case CFT_STRING:
				case CFT_PATH:
					xml_get_text_copy(node, fld->name, (gchar *) cmc->base + fld->offset, fld->field.string.size);
					break;
			}
		}
	}
}

/* 1999-04-05 -	Compare two command config namess; useful to keep them sorted. */
static gint cmp_cmc_name(gconstpointer a, gconstpointer b)
{
	return strcmp(((CmdCfg *) a)->name, ((CmdCfg *) b)->name);
}

/* 1999-04-05 -	Register given <cmc>, so iterators and other things become aware of it. */
void cmc_config_register(CmdCfg *cmc)
{
	if(cmc != NULL)
		cmdcfg_list = g_list_insert_sorted(cmdcfg_list, cmc, cmp_cmc_name);
}

/* 1999-04-05 -	Call a user-defined function for each registered command config descriptor. */
void cmc_config_registered_foreach(void (*func)(CmdCfg *cmc, gpointer user), gpointer user)
{
	const GList	*iter;

	for(iter = cmdcfg_list; iter != NULL; iter = g_list_next(iter))
		func(iter->data, user);
}

/* 1999-04-05 -	Return the number of registered command config descriptors. */
guint cmc_config_registered_num(void)
{
	return g_list_length(cmdcfg_list);
}

/* 1999-04-05 -	Unregister a <cmc>. */
void cmc_config_unregister(CmdCfg *cmc)
{
	if(cmc != NULL)
	{
		GList	*link;

		if((link = g_list_find(cmdcfg_list, cmc)) != NULL)
		{
			cmdcfg_list = g_list_remove_link(cmdcfg_list, link);
			g_list_free_1(link);
		}
	}
}

/* 1999-04-05 -	Free a single field. */
static void free_field(gpointer data, gpointer user)
{
	field_destroy(data);
}

/* 1999-04-05 -	Destroy a command config descriptor. Also causes it to be unregistered. */
void cmc_config_destroy(CmdCfg *cmc)
{
	if(cmc != NULL)
	{
		cmc_config_unregister(cmc);
		if(cmc->fields != NULL)
		{
			g_list_foreach(cmc->fields, free_field, NULL);
			g_list_free(cmc->fields);
		}
		g_free(cmc);
	}
}

/* ----------------------------------------------------------------------------------------- */

static CField * field_new(CFType type, const gchar *name, const gchar *desc, gsize offset)
{
	CField	*fld;

	fld = g_malloc(sizeof *fld);

	fld->type = type;
	if(name != NULL)
		g_strlcpy(fld->name, name, sizeof fld->name);
	if(desc != NULL)
		fld->desc = strdup(desc);
	else
		fld->desc = NULL;
	fld->offset = offset;

	return fld;
}

static void field_destroy(CField *fld)
{
	if(fld != NULL)
	{
		if(fld->desc != NULL)
			g_free(fld->desc);
		g_free(fld);
	}
}

/* 1999-04-04 -	Compare fields, sorting them in increasing offset order. */
static gint cmp_field(gconstpointer a, gconstpointer b)
{
	return (((CField *) a)->offset - ((CField *) b)->offset);
}

static void field_add(CmdCfg *cmc, CField *fld)
{
	if((cmc != NULL) && (fld != NULL))
	{
		if((cmc->fields == NULL) && strcmp(fld->name, "modified") != 0)
			g_error("CMDCFG: First field should be boolean named 'modified' (%s)", cmc->name);

		cmc->fields = g_list_insert_sorted(cmc->fields, fld, cmp_field);
	}
}

void cmc_field_add_integer(CmdCfg *cmc, const gchar *name, const gchar *desc, gsize offset, gint min, gint max)
{
	if(cmc != NULL)
	{
		CField	*fld = field_new(CFT_INTEGER, name, desc, offset);

		fld->field.integer.min = min;
		fld->field.integer.max = max;

		field_add(cmc, fld);
	}
}

void cmc_field_add_boolean(CmdCfg *cmc, const gchar *name, const gchar *desc, gsize offset)
{
	if(cmc != NULL)
		field_add(cmc, field_new(CFT_BOOLEAN, name, desc, offset));
}

/* 2003-10-23 -	Split a string "looking|like|this" into enum labels. */
void cmc_field_add_enum(CmdCfg *cmc, const gchar *name, const gchar *desc, gsize offset, const gchar *def)
{
	if(cmc != NULL)
	{
		gchar	*str;
		CField	*fld = field_new(CFT_ENUM, name, desc, offset);

		fld->field.fenum.num = 0;
		fld->field.fenum.def = g_strdup(def);
		for(str = fld->field.fenum.def; *str;)
		{
			fld->field.fenum.label[fld->field.fenum.num++] = str;
			while(*str && *str != '|')
				str++;
			if(*str == '|')
			{
				*str = '\0';
				str++;
			}
		}
		field_add(cmc, fld);
	}
}

void cmc_field_add_size(CmdCfg *cmc, const gchar *name, const gchar *desc, gsize offset, gsize min, gsize max, gsize step)
{
	if(cmc != NULL)
	{
		CField	*fld = field_new(CFT_SIZE, name, desc, offset);

		fld->field.size.min = min;
		fld->field.size.max = max + step;
		fld->field.size.step = step;

		field_add(cmc, fld);
	}
}

void cmc_field_add_string(CmdCfg *cmc, const gchar *name, const gchar *desc, gsize offset, gsize size)
{
	if(cmc != NULL)
	{
		CField	*fld = field_new(CFT_STRING, name, desc, offset);

		fld->field.string.size = size;

		field_add(cmc, fld);
	}
}

void cmc_field_add_path(CmdCfg *cmc, const gchar *name, const gchar *desc, gsize offset, gsize size, gunichar separator)
{
	if(cmc != NULL)
	{
		CField	*fld = field_new(CFT_PATH, name, desc, offset);

		/* Sneaky: use the 'string' member for CFT_PATH, too. */
		fld->field.string.size = size;
		fld->field.string.separator = separator;

		field_add(cmc, fld);
	}
}

/* 1999-04-05 -	Find a named field. */
static CField * field_find(CmdCfg *cmc, const gchar *name)
{
	if((cmc != NULL) && (name != NULL))
	{
		const GList	*iter;

		for(iter = cmc->fields; iter != NULL; iter = g_list_next(iter))
		{
			if(strcmp(((CField *) iter->data)->name, name) == 0)
				return iter->data;
		}
	}
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-05 -	Attach some data handy to have in event handlers. */
static void set_object_data(GObject *obj, CField *fld, gpointer instance)
{
	if(obj != NULL)
	{
		g_object_set_data(obj, "field", fld);
		g_object_set_data(obj, "instance", instance);
	}
}

/* 1999-04-05 -	Retrieve the data set by set_object_data() above. */
static void get_object_data(GObject *obj, CField **fld, gpointer *instance)
{
	if(obj != NULL)
	{
		if(fld != NULL)
			*fld = g_object_get_data(obj, "field");
		if(instance != NULL)
			*instance = g_object_get_data(obj, "instance");
	}
}

/* 1999-04-05 -	Set the mandatory 'modified' field to <value>. */
static void set_modified(CmdCfg *cmc, gpointer instance, gboolean value)
{
	if((cmc != NULL) && (instance != NULL))
	{
		CField	*fld;

		if((fld = field_find(cmc, "modified")) != NULL)
			*(gboolean *) ((gchar *) instance + fld->offset) = value;
	}
}

/* 1999-04-05 -	Return the value of the mandatory 'modified' field. */
static gboolean get_modified(CmdCfg *cmc, gpointer instance)
{
	if((cmc != NULL) && (instance != NULL))
	{
		CField	*fld;

		if((fld = field_find(cmc, "modified")) != NULL)
			return *(gboolean *) ((gchar *) instance + fld->offset);
	}
	return FALSE;		/* A safe default? */
}

/* 1999-04-05 -	User clicked a check button (boolean field). Update field value. */
static gint evt_boolean_clicked(GtkWidget *wid, gpointer user)
{
	CmdCfg		*cmc = user;
	CField		*fld = NULL;
	gpointer	instance = NULL;

	get_object_data(G_OBJECT(wid), &fld, &instance);

	if((cmc != NULL) && (fld != NULL) && (instance != NULL))
	{
		*(gboolean *) ((gchar *) instance + fld->offset) = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid));
		set_modified(cmc, instance, TRUE);
	}
	return TRUE;
}

static gint evt_enum_changed(GtkWidget *wid, gpointer user)
{
	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wid)))
	{
		CmdCfg		*cmc = user;
		CField		*fld = NULL;
		gpointer	instance = NULL;

		get_object_data(G_OBJECT(wid), &fld, &instance);
		if(cmc && fld && instance)
		{
			*(gint *) ((gchar *) instance + fld->offset) = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(wid), "value"));
			set_modified(cmc, instance, TRUE);
		}
	}
	return TRUE;
}

/* 1999-04-05 -	A string has been modified, so we need to update the instance. */
static gint evt_string_changed(GtkWidget *wid, gpointer user)
{
	CmdCfg		*cmc = user;
	CField		*fld;
	gpointer	instance;

	get_object_data(G_OBJECT(wid), &fld, &instance);

	if((cmc != NULL) && (fld != NULL) && (instance != NULL))
	{
		const gchar	*text;

		if((text = gtk_entry_get_text(GTK_ENTRY(wid))) != NULL)
			g_strlcpy((gchar *) instance + fld->offset, text, fld->field.string.size);
		set_modified(cmc, instance, TRUE);
	}
	return TRUE;
}

static gint evt_size_changed(GObject *obj, gpointer user)
{
	CmdCfg		*cmc = user;
	CField		*fld;
	gpointer	instance;

	get_object_data(obj, &fld, &instance);

	if((cmc != NULL) && (fld != NULL) && (instance != NULL))
	{
		gchar		buf[32];
		gsize		value = gtk_adjustment_get_value(GTK_ADJUSTMENT(obj));
		GtkWidget	*label = g_object_get_data(obj, "label");

		value *= fld->field.size.step;
		if(fld->field.size.step >= 512)
			sze_put_offset(buf, sizeof buf, value, SZE_KB, 0, ',');
		else
			sze_put_offset(buf, sizeof buf, value, SZE_BYTES, 0, ',');
		gtk_label_set_text(GTK_LABEL(label), buf);

		*(gsize *) ((gchar *) instance + fld->offset) = value;
		set_modified(cmc, instance, TRUE);
	}

	return TRUE;
}

static void evt_path_pick_clicked(GtkWidget *wid, gpointer user)
{
	CField		*fld;
	gpointer	instance;
	GtkEntry	*entry;

	get_object_data(G_OBJECT(wid), &fld, &instance);
	ldl_dialog_sync_new_wait((gchar *) instance + fld->offset, fld->field.string.size, fld->field.string.separator, fld->desc);
	entry = GTK_ENTRY(g_object_get_data(G_OBJECT(wid), "entry"));
	gtk_entry_set_text(entry, (gchar *) instance + fld->offset);
}

/* 2002-08-01 -	Build a neatly right-aligned label, and stuff it into <table>. */
static void label_build(const gchar *text, GtkWidget *grid, gint x, gint y)
{
	GtkWidget	*lab;

	lab = gtk_label_new(text);
	gtk_widget_set_halign(lab, GTK_ALIGN_END);
	gtk_widget_set_margin_end(lab, 5);
	gtk_grid_attach(GTK_GRID(grid), lab, x, y, 1, 1);
}

/* 1999-04-05 -	Build config widgetry for given <fld>. This function is a bit schizophrenic, since it
**		does one of two things: if <grid> is non-NULL, the widgetry is packed into it, which
**		looks neat. If it is NULL, a new grid is created, packed, and returned.
*/
static GtkWidget * field_build(CmdCfg *cmc, CField *fld, gpointer instance, GtkWidget *grid, gint row)
{
	if((cmc != NULL) && (fld != NULL) && (instance != NULL))
	{
		GtkWidget	*wid, *hbox, *label;
		GtkAdjustment	*adj;

		if(grid == NULL)
		{
			grid = gtk_grid_new();
			row = 0;
		}
		switch(fld->type)
		{
			case CFT_INTEGER:
				label_build(fld->desc, grid, 0, row);
				wid = gtk_spin_button_new(NULL, 1, 0);
				set_object_data(G_OBJECT(wid), fld, instance);
				adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(wid));
				gtk_adjustment_clamp_page(adj, fld->field.integer.min, fld->field.integer.max);
				gtk_adjustment_set_value(adj, *(gint *) ((gchar *) instance + fld->offset));
				gtk_widget_set_hexpand(wid, TRUE);
				gtk_widget_set_halign(wid, GTK_ALIGN_FILL);
				gtk_grid_attach(GTK_GRID(grid), wid, 1, row, 1, 1);
				break;
			case CFT_BOOLEAN:
				wid = gtk_check_button_new_with_label(fld->desc);
				set_object_data(G_OBJECT(wid), fld, instance);
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wid), *(gboolean *) ((gchar *) instance + fld->offset));
				g_signal_connect(G_OBJECT(wid), "clicked", G_CALLBACK(evt_boolean_clicked), cmc);
				gtk_widget_set_hexpand(wid, TRUE);
				gtk_widget_set_halign(wid, GTK_ALIGN_FILL);
				gtk_grid_attach(GTK_GRID(grid), wid, 0, row, 2, 1);
				break;
			case CFT_ENUM:
				wid = gtk_frame_new(fld->desc);
				{
					GtkWidget	*vbox, *radio[sizeof fld->field.fenum.label / sizeof *fld->field.fenum.label];
					gint		i, current = *(gint *) ((gchar *) instance + fld->offset);

					vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
					gui_radio_group_new(fld->field.fenum.num, (const gchar **) fld->field.fenum.label, radio);
					for(i = 0; i < fld->field.fenum.num; i++)
					{
						set_object_data(G_OBJECT(radio[i]), fld, instance);
						g_object_set_data(G_OBJECT(radio[i]), "value", GINT_TO_POINTER(i));
						g_signal_connect(G_OBJECT(radio[i]), "toggled", G_CALLBACK(evt_enum_changed), cmc);
						gtk_box_pack_start(GTK_BOX(vbox), radio[i], FALSE, FALSE, 0);
						if(i == current)
							gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio[i]), TRUE);
					}
					gtk_container_add(GTK_CONTAINER(wid), vbox);
				}
				gtk_widget_set_hexpand(wid, TRUE);
				gtk_widget_set_halign(wid, GTK_ALIGN_FILL);
				gtk_grid_attach(GTK_GRID(grid), wid, 0, row, 2, 1);
				break;
			case CFT_SIZE:
				label_build(fld->desc, grid, 0, row);
				hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
				adj = gtk_adjustment_new((gfloat) *(gint *) ((gchar *) instance + fld->offset) / fld->field.size.step,
							 fld->field.size.min / fld->field.size.step,
							 fld->field.size.max / fld->field.size.step,
							 1, 1, 0);

				set_object_data(G_OBJECT(adj), fld, instance);
				wid = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
				gtk_scale_set_draw_value(GTK_SCALE(wid), FALSE);
				gtk_box_pack_start(GTK_BOX(hbox), wid, TRUE, TRUE, 0);
				label = gtk_label_new("");
				gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);
				g_object_set_data(G_OBJECT(adj), "label", label);
				g_signal_connect(G_OBJECT(adj), "value_changed", G_CALLBACK(evt_size_changed), cmc);
				gtk_widget_set_hexpand(hbox, TRUE);
				gtk_widget_set_halign(hbox, GTK_ALIGN_FILL);
				gtk_grid_attach(GTK_GRID(grid), hbox, 1, row, 1, 1);
				break;
			case CFT_STRING:
				label_build(fld->desc, grid, 0, row);
				wid = gtk_entry_new();
				gtk_entry_set_max_length(GTK_ENTRY(wid), fld->field.string.size - 1);
				set_object_data(G_OBJECT(wid), fld, instance);
				gtk_entry_set_text(GTK_ENTRY(wid), (gchar *) instance + fld->offset);
				g_signal_connect(G_OBJECT(wid), "changed", G_CALLBACK(evt_string_changed), cmc);
				gtk_widget_set_hexpand(wid, TRUE);
				gtk_widget_set_halign(wid, GTK_ALIGN_FILL);
				gtk_grid_attach(GTK_GRID(grid), wid, 1, row, 1, 1);
				break;
			case CFT_PATH:
				{
					GtkWidget	*pick;

					label_build(fld->desc, grid, 0, row);
					hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
					wid = gtk_entry_new();
					gtk_entry_set_max_length(GTK_ENTRY(wid), fld->field.string.size - 1);
					set_object_data(G_OBJECT(wid), fld, instance);
					gtk_entry_set_text(GTK_ENTRY(wid), (gchar *) instance + fld->offset);
					g_signal_connect(G_OBJECT(wid), "changed", G_CALLBACK(evt_string_changed), cmc);
					gtk_box_pack_start(GTK_BOX(hbox), wid, TRUE, TRUE, 0);
					pick = gui_details_button_new();
					set_object_data(G_OBJECT(pick), fld, instance);
					g_object_set_data(G_OBJECT(pick), "entry", wid);
					g_signal_connect(G_OBJECT(pick), "clicked", G_CALLBACK(evt_path_pick_clicked), cmc);
					gtk_box_pack_start(GTK_BOX(hbox), pick, FALSE, FALSE, 0);
					gtk_widget_set_hexpand(hbox, TRUE);
					gtk_widget_set_halign(hbox, GTK_ALIGN_FILL);
					gtk_grid_attach(GTK_GRID(grid), hbox, 1, row, 1, 1);
				}
				break;
		}
		return grid;
	}
	return NULL;
}

/* 1999-04-05 -	Build configuration widgetry for field <name> in given <cmc>. */
GtkWidget * cmc_field_build(CmdCfg *cmc, const gchar *name, gpointer instance)
{
	if((cmc != NULL) && (name != NULL))
	{
		CField	*fld;

		if((fld = field_find(cmc, name)) != NULL)
			return field_build(cmc, fld, instance, NULL, 0);
	}
	return NULL;
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-04 -	Remove a named field from given <cmc>. Rarely used, if ever. */
void cmc_field_remove(CmdCfg *cmc, const gchar *name)
{
	if((cmc != NULL) && (name != NULL))
	{
		CField	*fld;

		if((fld = field_find(cmc, name)) != NULL)
		{
			cmc->fields = g_list_remove(cmc->fields, fld);
			field_destroy(fld);
		}
	}
}

/* ----------------------------------------------------------------------------------------- */

/* 1999-04-05 -	Allocate memory to hold an instance of a command config structure, as described
**		by the fields list in <cmc>.
*/
gpointer cmc_instance_new(CmdCfg *cmc)
{
	if(cmc != NULL)
	{
		const GList	*iter;
		gsize		size = 0, fsize;
		CField		*fld;

		for(iter = cmc->fields; iter != NULL; iter = g_list_next(iter))
		{
			fld = iter->data;

			fsize = fld->offset;
			switch(fld->type)
			{
				case CFT_INTEGER:
					fsize += sizeof (gint);
					break;
				case CFT_BOOLEAN:
					fsize += sizeof (gboolean);
					break;
				case CFT_ENUM:
					fsize += sizeof (gint);
					break;
				case CFT_SIZE:
					fsize += sizeof (gsize);
					break;
				case CFT_STRING:
				case CFT_PATH:
					fsize += fld->field.string.size;
					break;
			}
			if(fsize > size)
				size = fsize;
		}
		if(size)
			return g_malloc(size);
	}
	return NULL;
}

/* 1999-04-05 -	Create a new instance initialized to the current contents of the base one.
**		This is infinitely more useful than an empty instance.
*/
gpointer cmc_instance_new_from_base(CmdCfg *cmc)
{
	if(cmc != NULL)
	{
		gpointer	inst;

		if((inst = cmc_instance_new(cmc)) != NULL)
		{
			cmc_instance_copy(cmc, inst, cmc->base);
			return inst;
		}
	}
	return NULL;
}

/* 1999-04-05 -	Copy the instance <src> into <dst>. This would probably work as a straight
**		memory copy once the correct size of the instance is computed, but doing it
**		field-by-field seems somehow clearer, or something. There's no great rush.
*/
void cmc_instance_copy(CmdCfg *cmc, gpointer dst, gpointer src)
{
	if((cmc != NULL) && (dst != NULL) && (src != NULL))
	{
		const GList	*iter;
		CField		*fld;

		for(iter = cmc->fields; iter != NULL; iter = g_list_next(iter))
		{
			fld = iter->data;

			switch(fld->type)
			{
				case CFT_INTEGER:
					*(gint *) ((gchar *) dst + fld->offset) = *(gint *) ((gchar *) src + fld->offset);
					break;
				case CFT_BOOLEAN:
					*(gboolean *) ((gchar *) dst + fld->offset) = *(gboolean *) ((gchar *) src + fld->offset);
					break;
				case CFT_ENUM:
					*(gint *) ((gchar *) dst + fld->offset) = *(gboolean *) ((gchar *) src + fld->offset);
					break;
				case CFT_SIZE:
					*(gsize *) ((gchar *) dst + fld->offset) = *(gsize *) ((gchar *) src + fld->offset);
					break;
				case CFT_STRING:
				case CFT_PATH:
					g_strlcpy((gchar *) dst + fld->offset, (gchar *) src + fld->offset, fld->field.string.size);
					break;
			}
		}
	}
}

/* 1999-04-05 -	Copy <src> into the given <cmc>'s base instance. The given source instance had
**		better be legit (i.e., created with the same cmc).
*/
void cmc_instance_copy_to_base(CmdCfg *cmc, gpointer src)
{
	if((cmc != NULL) && (src != NULL))
		cmc_instance_copy(cmc, cmc->base, src);
}

/* 1999-04-05 -	Copy <cmc>'s base instance into <dst>. */
void cmc_instance_copy_from_base(CmdCfg *cmc, gpointer dst)
{
	if((cmc != NULL) && (dst != NULL))
		cmc_instance_copy(cmc, dst, cmc->base);
}

/* 1999-04-05 -	Build a container widget containing the widgetry for configuring each field. */
GtkWidget * cmc_instance_build(CmdCfg *cmc, gpointer instance)
{
	GtkWidget	*grid = NULL;

	if((cmc != NULL) && (instance != NULL))
	{
		const GList	*iter;
		gint		row = 0;

		grid = gtk_grid_new();
		for(iter = cmc->fields; iter != NULL; iter = g_list_next(iter))
		{
			if(strcmp(((CField *) iter->data)->name, "modified") == 0)
				continue;
			field_build(cmc, iter->data, instance, grid, row++);
		}
	}
	return grid;
}

/* 1999-04-05 -	Get the 'modified' state of given instance. */
gboolean cmc_instance_get_modified(CmdCfg *cmc, gpointer inst)
{
	return get_modified(cmc, inst);
}

/* 1999-04-05 -	Destroy an instance created by cmc_instance_new() above. Don't call this on
**		a static instance.
*/
void cmc_instance_destroy(gpointer inst)
{
	if(inst != NULL)
		g_free(inst);
}
