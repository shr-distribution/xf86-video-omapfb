/* Texas Instruments OMAP framebuffer driver for X.Org
 * Copyright 2010 Kalle Vahlman, <kalle.vahlman@movial.com>
 *
 * Permission to use, copy, modify, distribute and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the names of the authors and/or copyright holders
 * not be used in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  The authors and
 * copyright holders make no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without any express
 * or implied warranty.
 *
 * THE AUTHORS AND COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "omapfb-overlay-pool.h"
#include "omapfb-utils.h"

#define MAX_FRAMEBUFFERS 5
#define MAX_OVERLAYS 10

static void
probeFramebuffers(OverlayPoolPtr pool)
{
	int i;
	for (i = 0; i < MAX_FRAMEBUFFERS; i++)
	{
		struct stat st;
		char dir[512];
		snprintf(dir, 512, SYSFS_FB_DIR "/graphics/fb%i", i);
		if (stat(dir, &st) == -1)
			break;

		pool->framebuffers++;

		/* Reset overlays */
		write_fb_sysfs_value(i, "overlays", "\n");
	}
}

static void
probeOverlays(OverlayPoolPtr pool)
{
	int i;
	for (i = 0; i < MAX_OVERLAYS; i++)
	{
		struct stat st;
		char dir[512];
		snprintf(dir, 512, SYSFS_DSS_DIR "/overlay%i", i);
		if (stat(dir, &st) == -1)
			break;

		pool->overlays++;

		/* Disable, so we can change settings freely */
		write_dss_sysfs_value("overlay", i, "enabled", "0");
		/* TODO: should we sync here? */
		write_dss_sysfs_value("overlay", i, "manager", "");
	}
}

static void
probeManagers(OverlayPoolPtr pool)
{
	int i;
	for (i = 0; i < MAX_OVERLAYS; i++)
	{
		struct stat st;
		char dir[512];
		snprintf(dir, 512, SYSFS_DSS_DIR "/manager%i", i);
		if (stat(dir, &st) == -1)
			break;

		pool->managers++;
	}
}

/* Finds the manager for named display */
static int
overlayPoolManagerForDisplay(OverlayPoolPtr pool, char *display)
{
	int i;

	for (i = 0; i < pool->managers; i++)
	{
		char dpy[32];
		if (read_dss_sysfs_value("manager", i, "display", dpy, 32) != -1
		 && strncmp(dpy, display, strlen(display)) == 0)
			return i;
	}

	return -1;
}

static void
overlayPoolDisconnectOverlay(OverlayPoolPtr pool, int overlay)
{
	char overlays[32];
	int i;
	int s = 0;
	int n_overlays = 0;
	int fb = pool->fb_map[overlay];

	/* Disable overlay */
	write_dss_sysfs_value("overlay", overlay, "enabled", "0");

	/* Disconnect manager, except there is no way to do that... */

	/* Disconnect from fb */
	for (i = 0; i < pool->overlays; i++)
	{
		if (i == overlay)
			continue;
		if (pool->fb_map[i] == fb)
		{
			n_overlays++;
			s += snprintf(overlays + s, 3, "%s%i", n_overlays > 1 ? "," : "", i);
		}
	}
	overlays[s] = '\0';
	write_fb_sysfs_value(fb, "overlays", overlays);
}

static void
overlayPoolConnectOverlay(OverlayPoolPtr pool, int overlay)
{
	char overlays[32];
	char mgr_name[32];
	int i;
	int s = 0;
	int n_overlays = 0;
	int fb = pool->fb_map[overlay];
	int mgr = pool->mgr_map[overlay];

	/* Connect manager */
	s = read_dss_sysfs_value("manager", mgr, "name", mgr_name, 32);
	if (s == -1)
		return;
	mgr_name[s-1] = '\0';
	write_dss_sysfs_value("overlay", overlay, "manager", mgr_name);

	/* Connect to fb */
	s = 0;
	for (i = 0; i < pool->overlays; i++)
	{
		if (pool->fb_map[i] == fb)
		{
			n_overlays++;
			s += snprintf(overlays + s, 3, "%s%i", n_overlays > 1 ? "," : "", i);
		}
	}
	overlays[s] = '\0';
	write_fb_sysfs_value(fb, "overlays", overlays);

	/* Enable overlay */
	write_dss_sysfs_value("overlay", overlay, "enabled", "1");
}

/*** Public API below this */

OverlayPoolPtr
overlayPoolInit(ScrnInfoPtr pScrn)
{
	int i;
	OverlayPoolPtr pool;

	pool = malloc(sizeof(OverlayPoolRec));

	pool->scrn = pScrn;
	for (i = 0; i < OMAPFB_MAX_DISPLAYS; i++)
	{
		pool->fb_map[i] = -1;
		pool->mgr_map[i] = -1;
		pool->mapping_dirty[i] = FALSE;
	}

	pool->framebuffers = 0;
	probeFramebuffers(pool);
	pool->overlays = 0;
	probeOverlays(pool);
	pool->managers = 0;
	probeManagers(pool);

	xf86DrvMsg(pool->scrn->scrnIndex, X_INFO,
		       "%s: Found %i framebuffers, %i overlays and %i managers\n",
			       __FUNCTION__, pool->framebuffers, pool->overlays, pool->managers);

	return pool;
}

/* Returns the first free overlay */
int
overlayPoolGetFreeOverlay(OverlayPoolPtr pool)
{
	int i;
	for (i = 0; i < OMAPFB_MAX_DISPLAYS; i++) {
		if (pool->mgr_map[i] == -1)
		{
			return i;
		}
	}

	xf86DrvMsg(pool->scrn->scrnIndex, X_WARNING, "%s: no free overlays\n", __FUNCTION__);

	return -1;
}

/* Makes the framebuffer -> overlay -> display connection */
int
overlayPoolConnect(OverlayPoolPtr pool, int fb, int overlay, char *display)
{
	int manager = overlayPoolManagerForDisplay(pool, display);

	if (manager == -1)
		return FALSE;

	pool->fb_map[overlay] = fb;
	pool->mgr_map[overlay] = manager;
	pool->mapping_dirty[overlay] = TRUE;

	return TRUE;
}

/* Disconnect a display from active configuration */
int
overlayPoolDisconnect(OverlayPoolPtr pool, char *display)
{
	int i;
	int overlay = -1;
	int manager = overlayPoolManagerForDisplay(pool, display);

	if (manager == -1)
		return FALSE;

	for (i = 0; i < pool->overlays; i++)
	{
		if (pool->mgr_map[i] == manager)
			overlay = i;
	}

	if (overlay == -1)
		return FALSE;

	pool->fb_map[overlay] = -1;
	pool->mgr_map[overlay] = -1;
	pool->mapping_dirty[overlay] = TRUE;

	return TRUE;
}

/* Is the display connected? */
int
overlayPoolDisplayConnected(OverlayPoolPtr pool, char *display)
{
	int i;
	int overlay = -1;
	int manager = overlayPoolManagerForDisplay(pool, display);

	if (manager == -1)
		return FALSE;

	for (i = 0; i < pool->overlays; i++)
	{
		if (pool->mgr_map[i] == manager)
			overlay = i;
	}

	if (overlay == -1)
		return FALSE;

	if (pool->fb_map[overlay] == -1 || pool->mgr_map[overlay] == -1)
		return FALSE;

	return TRUE;
}

/* Commit the current setup */
int overlayPoolApplyConnections(OverlayPoolPtr pool)
{
	int i;

	for (i = 0; i < pool->overlays; i++)
	{
		if (pool->mapping_dirty[i])
		{
	xf86DrvMsg(pool->scrn->scrnIndex, X_WARNING, "%s: Disconnecting overlay %i\n", __FUNCTION__, i);
			overlayPoolDisconnectOverlay(pool, i);
		}
	}

	for (i = 0; i < pool->overlays; i++)
	{
		if (pool->mapping_dirty[i])
		{
			if (pool->fb_map[i] != -1 || pool->mgr_map[i] != -1)
			{
				xf86DrvMsg(pool->scrn->scrnIndex, X_WARNING, "%s: Connecting %i -> %i -> %i\n", __FUNCTION__, pool->fb_map[i], i, pool->mgr_map[i]);
				overlayPoolConnectOverlay(pool, i);
			}
			pool->mapping_dirty[i] = FALSE;
		}
	}
	/* TODO: propagate failures */
	return TRUE;
}
