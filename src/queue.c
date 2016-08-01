/*
** 1998-09-20 -	This module helps maintain a queue of ("small") events that need to be
**		taken care of as soon as is convenient (i.e., in our idle handler).
**		The most typical event is "continue running a command sequence".
**			Besides, anything that gives me reason to type "queue" more
**		often is worth doing. My fingers just love it. :^)
*/

#include "gentoo.h"

#include <stdarg.h>

#include "cmdseq.h"
#include "children.h"
#include "queue.h"

/* ----------------------------------------------------------------------------------------- */

struct QueueInfo {
        guint32         queue;                  /* The queue state is just a bit vector. */
        guint           tag;                    /* Tag for the timeout function. */
        GList           *end_list;              /* List of after-flags for running commands. */
};

/* ----------------------------------------------------------------------------------------- */

/* 1999-03-31 -	Just initialize the queue data, and return a pointer to the private data
**		structure defining the queue.
*/
QueueInfo * que_initialize(void)
{
	QueueInfo	*que;

	que = g_malloc(sizeof *que);
	que->queue = 0UL;
	que->tag   = 0;
	que->end_list = NULL;

	return que;
}

/* ----------------------------------------------------------------------------------------- */

static gint idle_handler(gpointer user)
{
	MainInfo	*min = user;

	if(min->que->queue & (1 << QEVT_END_CMD))
	{
		GList	*iter;

		for(iter = min->que->end_list; iter != NULL; iter = g_list_next(iter))
			csq_handle_ba_flags(min, GPOINTER_TO_UINT(iter->data));
		g_list_free(min->que->end_list);
		min->que->end_list = NULL;
		min->que->queue &= ~ (1 << QEVT_END_CMD);
	}
	if(min->que->queue & (1 << QEVT_CONTINUE_CMD)) 
	{
		min->que->queue &= ~(1 << QEVT_CONTINUE_CMD);
		gtk_widget_set_sensitive(min->gui->window, TRUE);
		gtk_widget_grab_focus(GTK_WIDGET(min->gui->cur_pane->view));
		chd_clear_lock();
		csq_continue(min);
	}

	if(min->que->queue == 0)		/* No longer any need for the idle function? */
	{
		g_source_remove(min->que->tag);
		min->que->tag = 0;
		return FALSE;		/* Then let GTK+ know that. */
	}
	return TRUE;
}

/* ----------------------------------------------------------------------------------------- */

/* 1998-09-20 -	Enqueue an event of type <evt> for execution some time later. */
void que_enqueue(MainInfo *min, QEvt evt, ...)
{
	min->que->queue |= (1 << evt);

	if(evt == QEVT_END_CMD)		/* A command that needs to be finished? */
	{
		va_list	arg;

		va_start(arg, evt);
		min->que->end_list = g_list_append(min->que->end_list, GUINT_TO_POINTER(va_arg(arg, guint32)));
		va_end(arg);
	}

	if(min->que->tag == 0)
	{
		if(min->que->tag == 0)		/* Since we treat 0 specially, pretend GTK+ does, too. */
			min->que->tag = g_idle_add(idle_handler, min);
	}
}
