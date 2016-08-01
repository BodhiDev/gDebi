/*
** 1999-01-27 -	Slim wrapper for the mntent-family of API calls. Required because it
**		seems to be a very unstandardized standard (different between Linux and
**		Solaris, for example). This has been modeled after the way it works on
**		Linux, so systems doing it in some other way will have to have emulation
**		code in here... Yuck. Since we're not into writing to these databases,
**		that part of the API is not supported. For details about which operations
**		need to be implemented for a platform, see "mntent_wrap.h".
*/

#include "gentoo.h"

#include <stdio.h>
#include <stdlib.h>

#include "mntent_wrap.h"

/* ----------------------------------------------------------------------------------------- */

struct MntEnt {
	gchar	*fsname;
	gchar	*dir;
};

/* ----------------------------------------------------------------------------------------- */

#if defined __CYGWIN__ || defined __linux__ || defined __sgi	/* Linux and IRIX implementation. */

#include <mntent.h>

FILE * mne_setmntent(const gchar *filename)
{
	return setmntent(filename, "r");
}

const MntEnt * mne_getmntent(FILE *filep)
{
	static MntEnt	me;
	struct mntent	*ment;

	if((ment = getmntent(filep)) != NULL)
	{
		me.fsname = ment->mnt_fsname;
		me.dir	  = ment->mnt_dir;
		return &me;
	}
	return NULL;
}

gint mne_endmntent(FILE *filep)
{
	return endmntent(filep);
}

/* ----------------------------------------------------------------------------------------- */

#elif defined __svr4__		/* Very weak Solaris detection, I'm sure. */

#include <sys/mnttab.h>

/* 1999-01-27 -	Since Solaris doesn't include the setmntent() function as a part of their
**		mount database access API, but rather just use a plain fopen(), I guess
**		the following is a pretty natural way to wrap it.
*/
FILE * mne_setmntent(const gchar *filename)
{
	return fopen(filename, "rt");
}

/* 1999-01-27 -	Grab a new entry from <filep>, and package it up in our platform neutral
**		format. If all wrappers were this simple, I would write more of them. :)
*/
const MntEnt * mne_getmntent(FILE *filep)
{
	static MntEnt	me;
	struct mnttab	mtab;

	if((getmntent(filep, &mtab)) == 0)
	{
		me.fsname = mtab.mnt_special;
		me.dir	  = mtab.mnt_mountp;
		return &me;
	}
	return NULL;
}

/* 1999-01-27 -	Close down a mount database. The natural complement to mne_setmntent(). */
gint mne_endmntent(FILE *filep)
{
	if(filep != NULL)
		return fclose(filep);
	return -1;
}

#elif defined __OpenBSD__ || defined __FreeBSD__ || defined __NetBSD__ || (defined __osf__ && defined __alpha__)

/* Here is the implementation for BSD and Alpha Tru64 systems. */

#include <fstab.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/ucred.h>

/* A pointer to one of these is returned by mne_setmntent(), depending on which file
** name is given as an input. Note that on BSD systems, the system calls themselves
** deal with the reading of files, so gentoo will never in fact open any files. But
** since the calls used to access currently mounted file systems is very dissimilar
** to the one used to check _available_ file systems, we need a way of keeping track
** of what kind of mount entries we're supposed to deal with. Since gentoo will
** pass a FILE pointer to mne_getmntent() anyway, it seems natural to use it.
*/
static FILE	f_fstab, f_mtab;

/* These are used when we're accessing the currently mounted filesystems, using
** a call to getmntinfo(). The mtab_pos and mtab_num integers are then used to
** keep track of where in the returned array of statfs structs we are.
*/
static struct statfs	*mtab = NULL;
static guint		mtab_pos = 0, mtab_num = 0;

/* 1999-05-09 -	An attempt at a BSD implementation, after having received input from
**		Alexander M. Tahk <tahk@MIT.EDU>, who went ahead and ported gentoo over.
**		Seems the BSD interface is even less flexible than the ones on Linux and
**		Solaris. Big deal.
*/
FILE * mne_setmntent(const gchar *filename)
{
	/* This is possibly incredibly annoying and useless, but I feel serious today. :) */
	if((strcmp(filename, "/etc/fstab") != 0) || (strcmp(filename, "/etc/mtab") != 0))
		g_warning("mntent_wrap.c: Ignoring filename '%s'", filename);

	if(strcmp(filename, "/etc/fstab") == 0)		/* Looking for available filesystems? */
	{
		if(setfsent() == 1)
			return &f_fstab;
	}
	else if(strcmp(filename, "/proc/mtab") == 0)	/* Looking for mounted filesystems? */
	{
		if((mtab_num = getmntinfo(&mtab, 0)) > 0)
		{
			mtab_pos = 0;
			return &f_mtab;
		}
	}
	return NULL;
}

/* 1999-05-09 -	Get another entry of data, either about mounted (filep == &f_mtab) or available
**		(filep == &f_fstab) filesystems. Returns NULL when the respective data source
**		is exhausted.
*/
const MntEnt * mne_getmntent(FILE *filep)
{
	static MntEnt	me;

	if(filep == &f_fstab)
	{
		struct fstab	*ment;

		if((ment = getfsent()) != NULL)
		{
			me.fsname = ment->fs_spec;
			me.dir	  = ment->fs_file;
			return &me;
		}
	}
	else if(filep == &f_mtab)
	{
		if(mtab_pos == mtab_num)		/* Array exhausted? */
			return NULL;
		me.fsname = mtab[mtab_pos].f_mntfromname;
		me.dir	  = mtab[mtab_pos].f_mntonname;
		mtab_pos++;
		return &me;
	}
	else
		g_warning("MNTENT: Bad pointer to mnt_getmntent() (%p)", filep);

	return NULL;
}

/* 1999-05-09 -	Stop traversing mount/fs data. */
gint mne_endmntent(FILE *filep)
{
	if(filep == &f_fstab)
		endfsent();

	return 0;
}

#endif

/* ----------------------------------------------------------------------------------------- */

/* Functions below are platform independant. Nice. */

/* 1999-01-27 -	Create a copy of <src>, using dynamically allocated memory. The
**		copy is done so that a) it shares no memory with <src>, and b)
**		it is contigous, because that is nice. :)
*/
MntEnt * mne_copy(const MntEnt *src)
{
	MntEnt	*ne = NULL;
	gsize	s_fs, s_dir;

	if(src != NULL)
	{
		s_fs  = strlen(src->fsname) + 1;
		s_dir = strlen(src->dir) + 1;
		ne = g_malloc(sizeof *ne + s_fs + s_dir);
		ne->fsname = (gchar *) (ne + 1);
		ne->dir    = (gchar *) (ne + 1) + s_fs;
		strcpy(ne->fsname, src->fsname);
		strcpy(ne->dir, src->dir);
	}
	return ne;
}

/* 1999-01-27 -	Destroy (free) a previously created mount entry descriptor. You can
**		only call this with entries created by mne_copy() above.
*/
void mne_destroy(MntEnt *me)
{
	if(me != NULL)
		g_free(me);	/* Thanks to funky g_malloc()ing, I can do this. */
}

/* 1999-01-27 -	Return a pointer to a string giving the full path of the entry's mount
**		point.
*/
const gchar * mne_get_mountpoint(const MntEnt *me)
{
	return me ? me->dir : NULL;
}

/* 1999-01-27 -	Return pointer to string giving the device the entry describes. */
const gchar * mne_get_device(const MntEnt *me)
{
	return me ? me->fsname : NULL;
}
