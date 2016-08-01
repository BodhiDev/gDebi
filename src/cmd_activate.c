/*
** 1998-05-25 -	Another basic GUI command, the native SWAP command handles changing of
**		the current dir pane. Very handy for that Opus-like feeling!
** 1998-07-01 -	Expanded, generalized, and renamed this one. The commands implemented
**		here are now called ActivateOther (which does the same thing the old
**		SWAP command did), ActivateLeft, and ActivateRight.
** 1999-01-30 -	Added the stack support, which is sometimes handy to have.
*/

#include "gentoo.h"
#include "dirpane.h"
#include "cmd_activate.h"

/* ----------------------------------------------------------------------------------------- */

#define	DP_STACK_SIZE	(8)

typedef struct {
	guint	stack_ptr;		/* Points at next free item in stack. */
	guint8	stack[DP_STACK_SIZE];	/* Stack of previously active pane indices. */
} AcStack;

static AcStack	the_stack = { 0 };	/* Makes sure stack pointer is initially 0 (<=> stack empty). */

/* ----------------------------------------------------------------------------------------- */

/* 1998-07-01 -	Activate the pane which is currently not active, i.e. the "other" one. */
gint cmd_activateother(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	dp_activate(dst);
	return 1;
}

/* 1998-07-01 -	Make sure the active pane is the left one. */
gint cmd_activateleft(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	dp_activate(&min->gui->pane[0]);
	return 1;
}

/* 1998-07-01 -	Make sure the active pane is the right one. */
gint cmd_activateright(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	dp_activate(&min->gui->pane[1]);
	return 1;
}

/* 1999-01-30 -	Push currently active pane onto stack. Handy if you want to
**		reactivate it later. Stack is finite and won't wrap; any attempt
**		to push more than it can hold will be (silently) ignored.
*/
gint cmd_activatepush(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	if(the_stack.stack_ptr < (sizeof the_stack.stack / sizeof the_stack.stack[0]))
		the_stack.stack[the_stack.stack_ptr++] = src->index;
	return 1;
}

/* 1999-01-30 -	Activate the entry from the top of the stack. */
gint cmd_activatepop(MainInfo *min, DirPane *src, DirPane *dst, const CmdArg *ca)
{
	if(the_stack.stack_ptr > 0)
		dp_activate(&min->gui->pane[the_stack.stack[--the_stack.stack_ptr]]);
	return 1;
}
