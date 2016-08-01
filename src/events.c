/*
** 1999-06-12 -	A little module to temporarily store (and give some sort of controlled 
**		access to) GDK events. This is handy because some commands need data from
**		primarily mouse button events, but I don't want to pass that data down the
**		regular channels (i.e., as parameters to csq_execute()). So the commands
**		will have to go through here to get it.
*/

#include "gentoo.h"

#include "events.h"

/* ----------------------------------------------------------------------------------------- */

typedef struct {
	gboolean	valid;
	GdkEvent	event;
} EventSlot;

typedef struct {
	EventSlot	eventslot[1];
} EventInfo;

static EventInfo	the_events = { { { FALSE } } };

/* ----------------------------------------------------------------------------------------- */

/* 1999-06-12 -	Store given event in event memory, where it can be later accessed when needed.
**		Only a specific set of event types are supported.
*/
void evt_event_set(GdkEvent *evt)
{
	if(evt != NULL)
	{
		switch(evt->type)
		{
			case GDK_BUTTON_PRESS:
				the_events.eventslot[0].valid = TRUE;
				the_events.eventslot[0].event = *evt;
				break;
			default:
				fprintf(stderr, "EVENTS: evt_event_set() called on illegal event type!\n");
		}
	}
}

/* 1999-06-12 -	Retrieve a pointer the the last registered event of type <type>. Returns NULL
**		if there has been no registration, or the type is unsupported.
*/
GdkEvent * evt_event_get(GdkEventType type)
{
	guint	index;

	switch(type)
	{
		case GDK_BUTTON_PRESS:
			index = 0;
			break;
		default:
			fprintf(stderr, "EVENTS: evt_event_get() called on illegal event type!\n");
			return NULL;
	}
	if(the_events.eventslot[index].valid)
		return &the_events.eventslot[index].event;
	return NULL;
}

/* 1999-06-12 -	Clear the memory from events of type <type>. */
void evt_event_clear(GdkEventType type)
{
	guint	index;

	switch(type)
	{
		case GDK_BUTTON_PRESS:
			index = 0;
			break;
		default:
			fprintf(stderr, "EVENTS: evt_event_clear() called on illegal event type!\n");
			return;
	}
	the_events.eventslot[index].valid = FALSE;
}
