/*
** 1998-09-30 -	Header file for the progress reporting module. Pretty simple.
*/

#if !defined PROGRESS_H
#define	PROGRESS_H

typedef enum { PGS_CANCEL, PGS_PROCEED } PgsRes;

enum {
	PFLG_COUNT_RECURSIVE	= (1<<0),	/* Count # of items recursively? */
	PFLG_ITEM_VISIBLE	= (1<<1),	/* Display progress for individual items? */
	PFLG_BYTE_VISIBLE	= (1<<2),	/* Display total byte progress? */
	PFLG_BUSY_MODE		= (1<<10)	/* Just indicate "busyness", no direct filesystem coupling. */
};

/* ----------------------------------------------------------------------------------------- */

/* Call this to set up an operation, which is assumed to consist of doing
** something to a bunch of items.
*/
extern void	pgs_progress_begin(MainInfo *min, const gchar *op_name, guint32 flags);

extern GCancellable *	pgs_progress_get_cancellable(void);

/* Use these calls to report the progress of each item. */
extern void	pgs_progress_item_begin(MainInfo *min, const gchar *name, off_t size);
extern void	pgs_progress_item_resize(MainInfo *min, off_t new_size);
extern PgsRes	pgs_progress_item_update(MainInfo *min, off_t pos);
extern void	pgs_progress_item_end(MainInfo *min);

/* Call this when the operation as a whole has completed. */
extern void	pgs_progress_end(MainInfo *min);

#endif		/* PROGRESS_H */
