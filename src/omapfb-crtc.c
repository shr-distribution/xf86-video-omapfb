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

#include "xorg-server.h"

#include "xf86Crtc.h"

#include "omapfb-driver.h"
#include "omapfb-crtc.h"

static Bool
OMAPFBCrtcResize (ScrnInfoPtr pScrn, int width, int height)
{
	OMAPFBPtr ofb = OMAPFB(pScrn);

	if (width == pScrn->virtualX && height == pScrn->virtualY)
		return TRUE;

	/* We never change the actual size of the fb, so we just update
	 * the required values here. The mode is actually applied in SetMode.
	 */
	pScrn->virtualX = width;
	pScrn->virtualY = height;

	return TRUE;
}

static xf86CrtcConfigFuncsRec OMAPFBCrtcConfigFuncs = {
	OMAPFBCrtcResize /* Resize */
};

static void
OMAPFBCrtcDPMS (xf86CrtcPtr crtc, int mode)
{
}

static Bool
OMAPFBCrtcLock (xf86CrtcPtr crtc)
{
	/* Returning FALSE should leave Unlock uncalled */
	return FALSE;
}

static Bool
OMAPFBCrtcFixMode (xf86CrtcPtr crtc,
                   DisplayModePtr mode,
                   DisplayModePtr adjusted_mode)
{
	/* We do not fix things... */
	return TRUE;
}

static void
OMAPFBCrtcPrepareChangeMode (xf86CrtcPtr crtc)
{
	/* Here just because the server tends to crash without... */
}

static void
OMAPFBCrtcSetMode (xf86CrtcPtr crtc,
                   DisplayModePtr mode,
                   DisplayModePtr adjusted_mode,
                   int x,
                   int y)
{
	crtc->mode = *mode;

	/* We need the output to be configured first, so defer mode setting to commit */
}

static void
OMAPFBCrtcCommitChangeMode (xf86CrtcPtr crtc)
{
	struct fb_var_screeninfo v;
	OMAPFBPtr ofb = OMAPFB(crtc->scrn);
	DisplayModePtr mode = &crtc->mode;

	v = ofb->state_info;
	v.xres = mode->HDisplay;
	v.yres = mode->VDisplay;
	v.xres_virtual = crtc->scrn->virtualX;
	v.yres_virtual = crtc->scrn->virtualY;
	v.activate = FB_ACTIVATE_NOW;
	v.pixclock = KHZ2PICOS(mode->Clock ? mode->Clock : 56000);
	v.left_margin = mode->HTotal - mode->HSyncEnd;
	v.right_margin = mode->HSyncStart - mode->HDisplay;
	v.upper_margin = mode->VTotal - mode->VSyncEnd;
	v.lower_margin = mode->VSyncStart - mode->VDisplay;
	v.hsync_len = mode->HSyncEnd - mode->HSyncStart;
	v.vsync_len = mode->VSyncEnd - mode->VSyncStart;

	if (ioctl (ofb->fd, FBIOPUT_VSCREENINFO, &v))
	{
		xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR,
		           "%s: Setting mode failed: %s\n",
		           __FUNCTION__, strerror(errno));
	}

	if (ioctl (ofb->fd, FBIOGET_VSCREENINFO, &ofb->state_info))
	{
		xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR,
		           "%s: Reading resolution info failed: %s\n",
		           __FUNCTION__, strerror(errno));
	}

	if (ioctl (ofb->fd, FBIOGET_FSCREENINFO, &ofb->fixed_info)) {
		xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR,
		           "%s: Reading hardware info failed: %s\n",
		           __FUNCTION__, strerror(errno));
	}

	/* Update stride */
	crtc->scrn->displayWidth = ofb->fixed_info.line_length /
	                          (ofb->state_info.bits_per_pixel>>3);;

}

static void *
OMAPFBCrtcShadowAllocate (xf86CrtcPtr crtc, int width, int height)
{
	/* Here just because the server tends to crash without...
	 * Returning NULL should avoid getting the other Shadow* functions called
	 */
	return NULL;
}

static void
OMAPFBCrtcSetGamma (xf86CrtcPtr crtc,
                    CARD16 *red, CARD16 *green, CARD16 *blue,
                    int size)
{
	/* Here just because the server tends to crash without... */
}

static xf86CrtcFuncsRec OMAPFBCrtcFuncs = {
	OMAPFBCrtcDPMS, /* DPMS */
	NULL, /* State save */
	NULL, /* State restore */
	OMAPFBCrtcLock, /* Lock */
	NULL, /* UnLock */
	OMAPFBCrtcFixMode, /* Mode fixup */
	OMAPFBCrtcPrepareChangeMode, /* Mode change prepare */
	OMAPFBCrtcSetMode, /* Mode set */
	OMAPFBCrtcCommitChangeMode, /* Mode change commit */
	OMAPFBCrtcSetGamma, /* Gamma */
	OMAPFBCrtcShadowAllocate, /* Shadow allocate */
	NULL, /* Shadow create */
	NULL, /* Shadow destroy */
	NULL, /* Cursor colors */
	NULL, /* Set cursor position */
	NULL, /* Show cursor */
	NULL, /* Hide cursor */
	NULL, /* Load cursor image */
	NULL, /* Load cursor argb */
	NULL, /* Destroy */
	NULL, /* Set mode major */
	NULL  /* Set origin (panning) */	
};


void
OMAPFBCRTCInit(ScrnInfoPtr pScrn)
{
	OMAPFBPtr ofb = OMAPFB(pScrn);
	xf86CrtcConfigInit(pScrn, &OMAPFBCrtcConfigFuncs);

	/* We can support small sizes with output scaling (pixel doubling)
	 * and in theory multiple (unique) outputs with large virtual
	 * resolution by scanning out different parts of the the framebuffer.
	 * In practise, this doesn't seem to be supported.
	 * (no way to setup the overlay offset/base address)
	 */
	 /* FIXME: figure out what makes sense here. A known max resolution?
	  * framebuffer size?
	  */
	xf86CrtcSetSizeRange(pScrn,
	                     8, 8, 2048, 2048);

	ofb->crtc = xf86CrtcCreate(pScrn, &OMAPFBCrtcFuncs);
}


