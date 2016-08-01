/*
** 1999-06-12 -	Header for the little GDK event memory module.
*/

extern void		evt_event_set(GdkEvent *evt);
extern GdkEvent *	evt_event_get(GdkEventType type);
extern void		evt_event_clear(GdkEventType type);
