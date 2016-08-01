/*
** 2002-07-22 -	Interface to the FAM module. This always gets built, it's the contents of
**		the calls that varies if FAM is disabled or not supported.
** 2009-11-10 -	Internally remodelled, now simply uses GIO's monitoring services.
*/

extern gboolean	fam_initialize(MainInfo *min);
extern gboolean	fam_is_active(void);
extern gboolean	fam_monitor(const DirPane *dp);
extern gboolean	fam_is_monitored(const DirPane *dp);
extern void	fam_rescan_block(void);
extern void	fam_rescan_unblock(void);
extern void	fam_shutdown(MainInfo *min);
