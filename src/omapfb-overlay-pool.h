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

#include "omapfb-driver.h"

#ifndef __OMAPFB_OVERLAY_POOL_H__
#define __OMAPFB_OVERLAY_POOL_H__

#include "omapfb-output.h"

typedef struct _OverlayPoolRec {
	/* So we can print warnings, mostly */
	ScrnInfoPtr scrn;

	/* How many framebuffers, overlays and managers we have */
	int framebuffers;
	int overlays;
	int managers;
	
	/* overlay -> framebuffer mapping, indexed by overlays */
	int fb_map[OMAPFB_MAX_DISPLAYS];

	/* overlay -> manager mapping, indexed by overlays
	 * managers are 1:1 mapped to displays, but have different names
	 * we use display names in the API, and only indexes for managers
	 */
	int mgr_map[OMAPFB_MAX_DISPLAYS];

	int mapping_dirty[OMAPFB_MAX_DISPLAYS];
	
} OverlayPoolRec, *OverlayPoolPtr;

OverlayPoolPtr overlayPoolInit(ScrnInfoPtr pScrn);

/* Returns the first free overlay */
int overlayPoolGetFreeOverlay(OverlayPoolPtr pool);

/* Makes the framebuffer -> overlay -> display connection */
int overlayPoolConnect(OverlayPoolPtr pool, int fb, int overlay, char *display);

/* Disconnect a display from active configuration */
int overlayPoolDisconnect(OverlayPoolPtr pool, char *display);

/* Is the display connected? */
int overlayPoolDisplayConnected(OverlayPoolPtr pool, char *display);

/* Commit the current setup */
int overlayPoolApplyConnections(OverlayPoolPtr pool);

#endif /* __OMAPFB_OVERLAY_POOL_H__ */

