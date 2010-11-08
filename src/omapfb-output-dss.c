/* Texas Instruments OMAP framebuffer driver for X.Org
 * Copyright 2008 Kalle Vahlman, <zuh@iki.fi>
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
#include "omapfb-utils.h"

/*** Utility functions */

static int
OMAPFBDSSOutputIndex(xf86OutputPtr output)
{
	int i;
	int output_idx = -1;
	OMAPFBPtr ofb = OMAPFB(output->scrn);

	for (i = 0; i < OMAPFB_MAX_DISPLAYS; i++)
		if (ofb->outputs[i] == output)
			output_idx = i;

	return output_idx;
}

static int
OMAPFBDSSOutputWriteValue(xf86OutputPtr output,
                          const char *entry, const char *value)
{
	int rc = 0;
	int output_idx = OMAPFBDSSOutputIndex(output);

	if (output_idx < 0)
	{
		xf86DrvMsg(output->scrn->scrnIndex, X_WARNING,
		           "%s: %p is not a known output!\n", __FUNCTION__, output);
		return rc;
	}

	rc = write_dss_sysfs_value("display", output_idx, entry, value); 
	if (rc)
	{
		xf86DrvMsg(output->scrn->scrnIndex, X_WARNING,
		           "%s: writing value '%s' to sysfs entry %s for %s failed: %s\n",
			           __FUNCTION__, value, entry, output->name,
			           strerror(errno));
	}

	return rc;
}

static int
OMAPFBDSSOutputReadValue(xf86OutputPtr output,
                         const char *entry, char *value, size_t len)
{
	int rc = 0;
	int output_idx = OMAPFBDSSOutputIndex(output);

	if (output_idx < 0)
	{
		xf86DrvMsg(output->scrn->scrnIndex, X_WARNING,
		           "%s: %p is not a known output!\n", __FUNCTION__, output);
		return rc;
	}

	rc = read_dss_sysfs_value("display", output_idx, entry, value, len);
	if (rc < 1)
	{
		xf86DrvMsg(output->scrn->scrnIndex, X_WARNING,
		           "%s: reading value from sysfs entry %s for %s failed: %s\n",
			           __FUNCTION__, entry, output->name,
			           strerror(errno));
	}

	return rc;
}


static void
OMAPFBDSSOutputDPMS (xf86OutputPtr output, int mode)
{
	switch (mode) {
		case DPMSModeOn:
			OMAPFBDSSOutputWriteValue(output, "enabled", "1");
			break;
		case DPMSModeStandby:
		case DPMSModeSuspend:
			/* TODO: Maybe we would want to use the above modes for
			 * dimming the LCD? That'd match the functionality
			 * (save power)
			 */
		case DPMSModeOff:
			OMAPFBDSSOutputWriteValue(output, "enabled", "0");
			break;
		default:
			return;
	}
}

static int
OMAPFBDSSOutputValidateMode (xf86OutputPtr output,
                          DisplayModePtr mode)
{
	/* We accept pretty much any mode */
	return MODE_OK;
}

static Bool
OMAPFBDSSOutputFixMode (xf86OutputPtr output,
                     DisplayModePtr mode,
                     DisplayModePtr adjusted_mode)
{
	/* We accept pretty much any mode (since we don't change it yet) */
	return TRUE;
}

static void
OMAPFBDSSOutputPrepareChangeMode(xf86OutputPtr output)
{
	/* Here just because the server tends to crash without... */
}

static void
OMAPFBDSSOutputCommitChangeMode(xf86OutputPtr output)
{
	/* Here just because the server tends to crash without... */
}

static void
OMAPFBDSSOutputSetMode (xf86OutputPtr  output,
                     DisplayModePtr mode,
                     DisplayModePtr adjusted_mode)
{
	xf86Msg(X_NOT_IMPLEMENTED, "%s\n", __FUNCTION__);
	/* Wake the ouput up, for some reason this is not done by X */
	OMAPFBDSSOutputDPMS(output, DPMSModeOn);
}

static xf86OutputStatus
OMAPFBDSSOutputDetect (xf86OutputPtr output)
{
	OMAPFBPtr ofb = OMAPFB(output->scrn);
	int idx = OMAPFBDSSOutputIndex(output);

	if(ofb->timings[idx][0] == '\0')
		return XF86OutputStatusDisconnected;

	return XF86OutputStatusConnected;
}

static DisplayModePtr
OMAPFBDSSOutputGetModes(xf86OutputPtr output)
{
	OMAPFBPtr ofb = OMAPFB(output->scrn);
	DisplayModePtr mode = NULL;
	DisplayModePtr modes = NULL;
	int idx = OMAPFBDSSOutputIndex(output);

	/* No timings, no modes */
	if(ofb->timings[idx][0] == '\0')
		return modes;

	/* Only populate the native (current) mode */
	mode = calloc(1, sizeof(DisplayModeRec));
	mode->type      |= M_T_PREFERRED;

	omapfb_timings_to_mode(ofb->timings[idx], mode);
	xf86SetModeDefaultName(mode);
	modes = xf86ModesAdd(modes, mode);

	return modes;
}

static xf86OutputFuncsRec OMAPFBDSSOutputFuncs = {
	NULL, /* Create resources */
	OMAPFBDSSOutputDPMS, /* DPMS */
	NULL, /* Save state */
	NULL, /* Restore state */
	OMAPFBDSSOutputValidateMode, /* Mode validation */
	OMAPFBDSSOutputFixMode, /* Mode fixup */
	OMAPFBDSSOutputPrepareChangeMode, /* Mode change prepare */
	OMAPFBDSSOutputCommitChangeMode, /* Mode change commit */
	OMAPFBDSSOutputSetMode, /* Mode set */
	OMAPFBDSSOutputDetect, /* Detect */
	OMAPFBDSSOutputGetModes, /* Get modes */
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

static xf86OutputPtr
OMAPFBDSSOutputProbe(ScrnInfoPtr pScrn, int index)
{
	int s, fd;
	char fname[512], output_name[512];
	xf86OutputPtr output;

	snprintf(fname, 512, SYSFS_DSS_DIR "/display%i/name", index);
	fd = open(fname, O_RDONLY, 0);
	if (fd == -1)
		return NULL;

	s = read(fd, output_name, 512);
	close(fd);
	if (s < 1){
		return NULL;
	}

	output_name[s-1] = '\0';
	output = xf86OutputCreate(pScrn, &OMAPFBDSSOutputFuncs, output_name);
	output->possible_crtcs = 0x1;
	output->possible_clones = 0xff;
	output->interlaceAllowed = FALSE;
	output->doubleScanAllowed = FALSE;

	return output;
}

/* For newer omapfb DSS kernel API */
void
OMAPFBOutputInitDSS(ScrnInfoPtr pScrn)
{
	OMAPFBPtr ofb = OMAPFB(pScrn);
	int i;
	/* Probe the available displays
	 * FIXME: We should have an ignore mechanism to disable broken
	 * or otherwise controlled stuff
	 */
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Detecting outputs\n");
	for (i = 0; i < OMAPFB_MAX_DISPLAYS; i++)
	{
		ofb->outputs[i] = OMAPFBDSSOutputProbe(pScrn, i);

		if (ofb->outputs[i] == NULL)
			continue;

		/* Read initial timings to base modes off */
		if (OMAPFBDSSOutputReadValue(ofb->outputs[i], "timings",
		                             ofb->timings[i], 64) < 1)
			memset(ofb->timings[i], 0, 64);

	}

	/* Reset the fb, overlays and managers */
	/* FIXME: This is currently Blaze/4430SDP specific, but works with beagle too */
	/* FIXME: we need to do this dynamically, so we could do XV too */
	write_sysfs_value("/sys/devices/platform/omapfb/graphics/fb1/overlays", "");
	write_sysfs_value("/sys/devices/platform/omapfb/graphics/fb2/overlays", "");
	write_sysfs_value("/sys/devices/platform/omapfb/graphics/fb0/overlays", "0,1,2");

	/* FIXME: these should be attached by name, I guess */
	write_dss_sysfs_value("overlay", 0, "enabled", "0");
	write_dss_sysfs_value("overlay", 0, "manager", "lcd");
	write_dss_sysfs_value("overlay", 0, "enabled", "1");

	write_dss_sysfs_value("overlay", 1, "enabled", "0");
	write_dss_sysfs_value("overlay", 1, "manager", "2lcd");
	write_dss_sysfs_value("overlay", 1, "enabled", "1");

	write_dss_sysfs_value("overlay", 2, "enabled", "0");
	write_dss_sysfs_value("overlay", 2, "manager", "tv");
	write_dss_sysfs_value("overlay", 2, "enabled", "1");

}

