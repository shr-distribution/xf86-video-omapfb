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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xorg-server.h"
#include "xf86.h"
#include "xf86Crtc.h"

#ifdef HAVE_XEXTPROTO_71
#include <X11/extensions/dpmsconst.h>
#else
#define DPMS_SERVER
#include <X11/extensions/dpms.h>
#endif

#include "omapfb-driver.h"
#include "omapfb-output.h"


static void
OMAPFBOutputDPMS (xf86OutputPtr output, int mode)
{
	OMAPFBPtr ofb = OMAPFB(output->scrn);
	switch (mode) {
		case DPMSModeOn:
			if (ioctl(ofb->fd, FBIOBLANK, (void *)VESA_NO_BLANKING)) {
				xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
				           "FBIOBLANK: %s\n", strerror(errno));
			}
			break;
		case DPMSModeStandby:
		case DPMSModeSuspend:
			/* TODO: Maybe we would want to use the above modes for
			 * dimming the LCD? That'd match the functionality
			 * (save power)
			 */
		case DPMSModeOff:
			if (ioctl(ofb->fd, FBIOBLANK, (void *)VESA_POWERDOWN)) {
				xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
				           "FBIOBLANK: %s\n", strerror(errno));
			}
			break;
		default:
			return;
	}

}

static int
OMAPFBOutputValidateMode (xf86OutputPtr output,
                          DisplayModePtr mode)
{
	/* We accept pretty much any mode */
	return MODE_OK;
}

static Bool
OMAPFBOutputFixMode (xf86OutputPtr output,
                     DisplayModePtr mode,
                     DisplayModePtr adjusted_mode)
{
	/* We accept pretty much any mode (since we don't change it yet) */
	return TRUE;
}

static void
OMAPFBOutputPrepareChangeMode(xf86OutputPtr output)
{
	/* Here just because the server tends to crash without... */
}

static void
OMAPFBOutputCommitChangeMode(xf86OutputPtr output)
{
	/* Here just because the server tends to crash without... */
}

static void
OMAPFBOutputSetMode (xf86OutputPtr  output,
                     DisplayModePtr mode,
                     DisplayModePtr adjusted_mode)
{
	xf86Msg(X_NOT_IMPLEMENTED, "%s\n", __FUNCTION__);
	/* Wake the ouput up, for some reason this is not done by X */
	OMAPFBOutputDPMS(output, DPMSModeOn);
}

static xf86OutputStatus
OMAPFBOutputDetect (xf86OutputPtr output)
{
	/* We claim that our outputs are always connected */
	return XF86OutputStatusConnected;
}

static DisplayModePtr
OMAPFBOutputGetModes(xf86OutputPtr output)
{
	OMAPFBPtr ofb = OMAPFB(output->scrn);
	DisplayModePtr mode = NULL;
	DisplayModePtr modes = NULL;

	/* Only populate the native (current) mode */
	mode = calloc(1, sizeof(DisplayModeRec));
	mode->type      |= M_T_PREFERRED;
	mode->Clock = PICOS2KHZ(ofb->state_info.pixclock);
	mode->SynthClock = PICOS2KHZ(ofb->state_info.pixclock);
	mode->HDisplay   = ofb->state_info.xres;
	mode->HSyncStart = mode->HDisplay
	                  + ofb->state_info.right_margin;
	mode->HSyncEnd   = mode->HSyncStart
	                  + ofb->state_info.hsync_len;
	mode->HTotal     = mode->HSyncEnd
	                  + ofb->state_info.left_margin;
	mode->VDisplay   = ofb->state_info.yres;
	mode->VSyncStart = mode->VDisplay
	                  + ofb->state_info.lower_margin;
	mode->VSyncEnd   = mode->VSyncStart
	                  + ofb->state_info.vsync_len;
	mode->VTotal     = mode->VSyncEnd
	                  + ofb->state_info.upper_margin;

	xf86SetModeDefaultName(mode);
	modes = xf86ModesAdd(modes, mode);

	return modes;
}

static xf86OutputFuncsRec OMAPFBOutputFuncs = {
	NULL, /* Create resources */
	OMAPFBOutputDPMS, /* DPMS */
	NULL, /* Save state */
	NULL, /* Restore state */
	OMAPFBOutputValidateMode, /* Mode validation */
	OMAPFBOutputFixMode, /* Mode fixup */
	OMAPFBOutputPrepareChangeMode, /* Mode change prepare */
	OMAPFBOutputCommitChangeMode, /* Mode change commit */
	OMAPFBOutputSetMode, /* Mode set */
	OMAPFBOutputDetect, /* Detect */
	OMAPFBOutputGetModes, /* Get modes */
#ifdef RANDR_12_INTERFACE
	NULL, /* Set property */
#endif
#ifdef RANDR_13_INTERFACE
	NULL, /* Get property */
#endif
#ifdef RANDR_GET_CRTC_INTERFACE
	NULL, /* Get CRTC */
#endif
	NULL /* Destroy */
};

/* For basic omapfb driver kernel API */
void
OMAPFBOutputInit(ScrnInfoPtr pScrn)
{
	OMAPFBPtr ofb = OMAPFB(pScrn);

	/* Using old-style omapfb, we only support one output */
	ofb->outputs[0] = xf86OutputCreate(pScrn, &OMAPFBOutputFuncs, "LCD");
	ofb->outputs[0]->possible_crtcs = 1;
	ofb->outputs[0]->possible_clones = 0;

}

