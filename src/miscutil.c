/*
** 2004-05-01 -	Miscellanous utility functions that are needed in more than one place. A very
**		late addition, so hopefully will grow slowly.
*/

#include "gentoo.h"

#include "miscutil.h"

/* ----------------------------------------------------------------------------------------- */

/* 2004-05-01 -	Compute elapsed time (in seconds) from <t1> to <t2>. */
gfloat msu_diff_timeval(const GTimeVal *t1, const GTimeVal *t2)
{
	return 1E-6 * (t2->tv_usec - t1->tv_usec) + (t2->tv_sec - t1->tv_sec);
}
