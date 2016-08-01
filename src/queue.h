/*
** 1998-09-20 -	Header for the very small and simple pseudo-queue module.
*/

#if !defined QUEUE_H
#define	QUEUE_H

typedef struct QueueInfo	QueueInfo;

typedef enum { QEVT_CONTINUE_CMD, QEVT_END_CMD } QEvt;

extern QueueInfo *	que_initialize(void);

extern void		que_enqueue(MainInfo *min, QEvt evt, ...);

#endif		/* QUEUE_H */
