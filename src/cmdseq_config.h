/*
** 1999-04-04 -	Interface definitions for the command sequence configuration module.
*/

#if !defined CMDSEQ_CONFIG_H
#define	CMDSEQ_CONFIG_H

#include <stddef.h>

#include "xmlutil.h"

typedef struct CmdCfg	CmdCfg;

CmdCfg *	cmc_config_new(const gchar *cmdname, gpointer base_instance);
const gchar *	cmc_config_get_name(CmdCfg *cmc);
void		cmc_config_base_save(CmdCfg *cmc, FILE *out);
void		cmc_config_base_load(CmdCfg *cmc, const XmlNode *data);
void		cmc_config_register(CmdCfg *cmc);
void		cmc_config_registered_foreach(void (*func)(CmdCfg *cmc, gpointer user), gpointer user);
guint		cmc_config_registered_num(void);
void		cmc_config_unregister(CmdCfg *cmc);
void		cmc_config_destroy(CmdCfg *cmc);

void		cmc_field_add_integer(CmdCfg *cmc, const gchar *name, const gchar *desc, gsize offset, gint min, gint max);
void		cmc_field_add_boolean(CmdCfg *cmc, const gchar *name, const gchar *desc, gsize offset);
void		cmc_field_add_enum   (CmdCfg *cmc, const gchar *name, const gchar *desc, gsize offset, const gchar *def);
void		cmc_field_add_size   (CmdCfg *cmc, const gchar *name, const gchar *desc, gsize offset, gsize min, gsize max, gsize step);
void		cmc_field_add_string (CmdCfg *cmc, const gchar *name, const gchar *desc, gsize offset, gsize size);
void		cmc_field_add_path   (CmdCfg *cmc, const gchar *name, const gchar *desc, gsize offset, gsize size, gunichar separator);

GtkWidget *	cmc_field_build(CmdCfg *cmc, const gchar *name, gpointer instance);

void		cmc_field_remove(CmdCfg *cmc, const gchar *name);

gpointer	cmc_instance_new(CmdCfg *cmc);
gpointer	cmc_instance_new_from_base(CmdCfg *cmc);
GtkWidget *	cmc_instance_build(CmdCfg *cmc, gpointer instance);
void		cmc_instance_copy(CmdCfg *cmc, gpointer dst, gpointer src);
void		cmc_instance_copy_to_base(CmdCfg *cmc, gpointer src);
void		cmc_instance_copy_from_base(CmdCfg *cmc, gpointer dst);
gboolean	cmc_instance_get_modified(CmdCfg *cmc, gpointer inst);
void		cmc_instance_destroy(gpointer instance);

#endif		/* CMDSEQ_CONFIG_H */
