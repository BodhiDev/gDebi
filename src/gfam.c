/*
** 2002-07-22 -	Finally, gentoo has learned to use FAM (File Alteration Monitor) if available. FAM,
**		from SGI, provides an application with the ability to be notified when a file or
**		directory changes. We use this to trigger pane reloads, so gentoo knows what's going
**		on in the filesystem.
**
**		FAM support is entierly optional: if you don't want it, or you system doesn't have it,
**		it will be built as stubs that do nothing. Use --disable-fam to manually turn it off.
**
**		Btw, this module is named "gfam" because plain "fam.h" collided with <fam.h>. Weird.
** 2009-11-10 -	Totally rewritten (and I do mean *totally*), now simply wraps GIO's monitoring. For
**		the usual dried up grapes' sake, I left the name of the module alone. Later ...
*/

#include "gentoo.h"

#include "dialog.h"
#include "dirpane.h"
#include "miscutil.h"

#include "gfam.h"

/* ----------------------------------------------------------------------------------------- */

static const guint RESCAN_INTERVAL = 50;	/* Milliseconds. */

static struct {
	struct {
	DirPane		*dp;
	GFileMonitor	*monitor;
	gulong		sig_changed;
	}		pane[2];
	gulong		block_count;
	guint		rescan_mask;
	guint		timeout;
} monitor_info;

/* ----------------------------------------------------------------------------------------- */

gboolean fam_initialize(MainInfo *min)
{
	guint	i;

	for(i = 0; i < sizeof monitor_info.pane / sizeof *monitor_info.pane; i++)
	{
		monitor_info.pane[i].dp = &min->gui->pane[i];
		monitor_info.pane[i].monitor = NULL;
		monitor_info.pane[i].sig_changed = 0;
	}
	monitor_info.block_count = 0;
	monitor_info.rescan_mask = 0;
	monitor_info.timeout = 0;

	return TRUE;
}

gboolean fam_is_active(void)
{
	return TRUE;
}

static gboolean evt_timeout(gpointer data)
{
	guint	i, mask;

	if(monitor_info.block_count > 0)
		goto remove_and_exit;	/* It's not often, but here's one! */
	mask = monitor_info.rescan_mask;
	for(i = 0; i < sizeof monitor_info.pane / sizeof *monitor_info.pane; i++)
	{
		if(mask & (1U << i))
		{
			GTimer	*timer;

			timer = g_timer_new();
			dp_rescan(monitor_info.pane[i].dp);
			if(timer != NULL)
			{
				gint	limit;

				g_timer_stop(timer);
				limit = g_timer_elapsed(timer, NULL);
				g_timer_destroy(timer);
				if(limit < 50)
					limit = 50;
				else if(limit > 10000)
					limit = 10000;
				g_file_monitor_set_rate_limit(monitor_info.pane[i].monitor, limit);
			}
		}
	}
	monitor_info.rescan_mask = 0;

remove_and_exit:
	/* The timeout is always a one-shot; clear handle and ask glib to delete. */
	monitor_info.timeout = 0;
	return FALSE;
}

static void queue_rescan(guint pane)
{
	monitor_info.rescan_mask |= (1U << pane);
	if(monitor_info.block_count == 0)
	{
		if(monitor_info.timeout == 0)
			monitor_info.timeout = g_timeout_add(RESCAN_INTERVAL, evt_timeout, NULL);
	}
}

static void evt_monitor_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event, gpointer user)
{
	DirPane	*dp = user;

	if(event == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || event == G_FILE_MONITOR_EVENT_CREATED || event == G_FILE_MONITOR_EVENT_DELETED)
		queue_rescan(dp->index);
}

gboolean fam_monitor(const DirPane *dp)
{
	if(dp == NULL)
		return FALSE;
	if(dp->index >= sizeof monitor_info.pane / sizeof *monitor_info.pane)
	{
		g_error("Too many panes, can't monitor pane with index %u", dp->index);
		return FALSE;
	}
	if(monitor_info.pane[dp->index].monitor != NULL)
	{
		g_object_unref(monitor_info.pane[dp->index].monitor);
		monitor_info.pane[dp->index].sig_changed = 0;
	}
	if(dp->dir.root == NULL)
	{
		g_error("No root GFile, failing to monitor pane with index %u\n", dp->index);
		return FALSE;
	}
	if((monitor_info.pane[dp->index].monitor = g_file_monitor(dp->dir.root, G_FILE_MONITOR_WATCH_MOUNTS, NULL, NULL)) != NULL)
		monitor_info.pane[dp->index].sig_changed = g_signal_connect(G_OBJECT(monitor_info.pane[dp->index].monitor), "changed", G_CALLBACK(evt_monitor_changed), (gpointer) dp);
	else
		g_warning("GIO failed to attach monitor to '%s'", dp->dir.path);
	return monitor_info.pane[dp->index].monitor != NULL;
}

gboolean fam_is_monitored(const DirPane *dp)
{
	if(dp == NULL)
		return FALSE;
	if(dp->index >= sizeof monitor_info.pane / sizeof *monitor_info.pane)
		return FALSE;
	return monitor_info.pane[dp->index].monitor != NULL;
}

void fam_rescan_block(void)
{
	monitor_info.block_count++;
}

void fam_rescan_unblock(void)
{
	guint	i;

	if(--monitor_info.block_count == 0)
	{
		for(i = 0; i < sizeof monitor_info.pane / sizeof *monitor_info.pane; i++)
		{
			if(monitor_info.rescan_mask & (1U << i))
			{
				queue_rescan(i);
				monitor_info.rescan_mask &= ~(1U << i);
			}
		}
	}
}

void fam_shutdown(MainInfo *min)
{
	guint	i;

	if(monitor_info.timeout != 0)
		g_source_remove(monitor_info.timeout);

	for(i = 0; i < sizeof monitor_info.pane / sizeof *monitor_info.pane; i++)
	{
		if(monitor_info.pane[i].monitor != NULL && monitor_info.pane[i].sig_changed != 0)
		{
			g_object_unref(monitor_info.pane[i].monitor);
			monitor_info.pane[i].sig_changed = 0;
		}
	}
}
