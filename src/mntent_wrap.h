/*
** 1999-01-27 -	Wrapper around the mntent API functions. These are used to parse system
**		databases holding information about filesystems that can be mounted, and
**		that are mounted. Needs to be wrapped since it seems to differ wildly
**		between various OSs (e.g. between Linux and Solaris).
*/

#if !defined GENTOO_MNTENT_H
#define GENTOO_MNTENT_H

#include <stdio.h>

/* Contains (at least) one (device,mountpoint) pair. Opaque, use accessor functions
** declared below to manipulate.
*/
typedef struct MntEnt	MntEnt;

/* The following three calls need platform-specific implementations. ----------------------- */

/* Open database file. Some platforms might ignore the <filename>. If a FILE * is not
** the most handy datatype for your platform, feel free to return some other pointer.
** Hm, this should probably be rewritten to use a gpointer instead of a FILE pointer...
*/
FILE *		mne_setmntent(const gchar *filename);

/* Get next mount entry from file. Returns NULL if there are no more entries available.
** The entry returned is strictly read-only, and lives in static memory, so it will change
** with the next (successful) call. To keep it, use mne_copy() to create a copy of it.
*/
const MntEnt *	mne_getmntent(FILE *filep);

/* Call this when you're done scanning the database, to close it down properly. */
gint		mne_endmntent(FILE *filep);

/* The rest of the calls are platform-neutral. --------------------------------------------- */

/* Create a copy of <src>, in dynamically allocated memory. Returns a pointer to
** the copy, or NULL on failure. The copy shares no memory with the original.
*/
MntEnt *	mne_copy(const MntEnt *src);

/* Destroy a MntEnt, freeing all memory used by it. Only valid on entries created
** by mne_copy().
*/
void		mne_destroy(MntEnt *me);

/* Return a pointer to a string containing the mountpoint path for the given entry (e.g. "/cdrom"). */
const gchar *	mne_get_mountpoint(const MntEnt *me);

/* Return pointer to a string with device name for given mount entry (e.g. "/dev/hdc"). */
const gchar *	mne_get_device(const MntEnt *me);

#endif
