/* Texas Instruments OMAP framebuffer driver for X.Org
 * Copyright 2008, 2010 Kalle Vahlman, <zuh@iki.fi>
 *
 * The driver setup in this file is adapted from the fbdev driver
 * Original authors of the fbdev driver:
 *           Alan Hourihane, <alanh@fairlite.demon.co.uk>
 *	     Michel Dänzer, <michel@tungstengraphics.com>
 *
 * The OMAPFB parts are heavily influenced by the KDrive OMAP driver,
 * copyright © 2006 Nokia Corporation
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

#include "xf86.h"
#include "xf86_OSlib.h"

#include "xf86Crtc.h"

#include "micmap.h"
#include "mipointer.h"
#include "fb.h"

#include "exa.h"

#ifdef HAVE_XEXTPROTO_71
#include <X11/extensions/dpmsconst.h>
#else
#define DPMS_SERVER
#include <X11/extensions/dpms.h>
#endif

#include <linux/fb.h>

/* TODO: we'd like this to come from kernel headers, but that's not a good
 * dependancy...
 */
#include "omapfb.h"

#include "omapfb-driver.h"
#include "omapfb-crtc.h"
#include "omapfb-output.h"
#include "omapfb-utils.h"

#define OMAPFB_VERSION 1000
#define OMAPFB_DRIVER_NAME "OMAPFB"
#define OMAPFB_NAME "omapfb"

static Bool OMAPFBProbe(DriverPtr drv, int flags);
static Bool OMAPFBPreInit(ScrnInfoPtr pScrn, int flags);
static Bool OMAPFBScreenInit(int scrnIndex, ScreenPtr pScreen, int argc, char **argv);
static Bool OMAPFBEnterVT(int scrnIndex, int flags);
static void OMAPFBLeaveVT(int scrnIndex, int flags);
static Bool OMAPFBSwitchMode(int scrnIndex, DisplayModePtr mode, int flags);

static Bool
OMAPFBEnsureRec(ScrnInfoPtr pScrn)
{
	if (pScrn->driverPrivate != NULL)
		return TRUE;
	
	pScrn->driverPrivate = xnfcalloc(sizeof(OMAPFBRec), 1);
	return TRUE;
}

static void
OMAPFBFreeRec(ScrnInfoPtr pScrn)
{
	if (pScrn->driverPrivate == NULL)
		return;
	free(pScrn->driverPrivate);
	pScrn->driverPrivate = NULL;
}

/*** General driver section */

static SymTabRec OMAPFBChipsets[] = {
    { 0, "omap1/2/3" },
    { 1, "S1D13745" },
    { 2, "HWA742" },
    { -1, NULL }
};

typedef enum {
	OPTION_ACCELMETHOD,
	OPTION_FB,
} FBDevOpts;

static const OptionInfoRec OMAPFBOptions[] = {
	{ OPTION_ACCELMETHOD,	"AccelMethod",	OPTV_STRING,	{0},	FALSE },
	{ OPTION_FB,		"fb",		OPTV_STRING,	{0},	FALSE },
	{ -1,			NULL,		OPTV_NONE,	{0},	FALSE }
};

static const OptionInfoRec *
OMAPFBAvailableOptions(int chipid, int busid)
{
	xf86Msg(X_NOT_IMPLEMENTED, "%s\n", __FUNCTION__);
	return OMAPFBOptions;
}

static void
OMAPFBIdentify(int flags)
{
	xf86PrintChipsets(OMAPFB_NAME,
	                  "Driver for OMAP framebuffer (omapfb) "
	                  "and external LCD controllers",
	                  OMAPFBChipsets);
}

static void
OMAPFBProbeController(char *ctrl_name)
{
	int fd;
	Bool found = FALSE;

/* FIXME: fetch this from hal? */
#define SYSFS_LCTRL_FILE "/sys/devices/platform/omapfb/ctrl/name"

	/* Try to read the LCD controller name */
	fd = open(SYSFS_LCTRL_FILE, O_RDONLY, 0);
	if (fd == -1) {
		xf86Msg(X_WARNING, "Error opening %s: %s\n",
		        SYSFS_LCTRL_FILE, strerror(errno));
	} else {
		int s = read(fd, ctrl_name, 31);
		if (s > 0) {
			ctrl_name[s-1] = '\0';
			found = TRUE;
		} else {
			xf86Msg(X_WARNING, "Error reading from %s: %s\n",
				SYSFS_LCTRL_FILE, strerror(errno));
		}
		close(fd);
	}

	/* Fall back to "internal" as controller */
	if (!found) {
		xf86Msg(X_WARNING,
			"Can't autodetect LCD controller, assuming internal\n");
		strcpy(ctrl_name, "internal");
	}

	xf86Msg(X_INFO, "LCD controller: %s\n", ctrl_name);
}

static Bool
OMAPFBProbe(DriverPtr drv, int flags)
{
	int i;
       	GDevPtr *devSections;
	int numDevSections;
	char *dev;
	ScrnInfoPtr pScrn = NULL;
	Bool foundScreen = FALSE;

	if (flags & PROBE_DETECT) return FALSE;

	/* Search for device sections for us */
	if ((numDevSections = xf86MatchDevice(OMAPFB_NAME, &devSections)) <= 0) 
		return FALSE;

/* FIXME: We don't really want to do it like this... */
#define DEFAULT_DEVICE "/dev/fb"

	for (i = 0; i < numDevSections; i++) {
		int fd;
		
		/* Fetch the device path */
		dev = xf86FindOptionValue(devSections[i]->options, "fb");

		/* Try opening it to see if we can access it */
		fd = open(dev != NULL ? dev : DEFAULT_DEVICE, O_RDWR, 0);
		if (fd > 0) {
			int entity;
			struct fb_fix_screeninfo info;

			if (ioctl (fd, FBIOGET_FSCREENINFO, &info)) {
				xf86Msg(X_WARNING,
				        "%s: Reading hardware info failed: %s\n",
				        __FUNCTION__, strerror(errno));
				close(fd);
				continue;
			}
			close(fd);

			/* We only check that the platform driver is correct
			 * here, detecting LCD controller and other capabilities
			 * are probed for in PreInit
			 */
			if (strcmp(info.id, "omapfb") &&
				strcmp(info.id, "omap24xxfb")) {
				xf86Msg(X_WARNING,
				        "%s: Not an omapfb device: %s\n",
				        __FUNCTION__, info.id);
				continue;
			}

			foundScreen = TRUE;

			/* Tell the rest of the drivers that this one is ours */
			entity = xf86ClaimFbSlot(drv, 0, devSections[i], TRUE);
			pScrn = xf86ConfigFbEntity(pScrn, 0, entity,
			                           NULL, NULL, NULL, NULL);

			pScrn->driverVersion = OMAPFB_VERSION;
			pScrn->driverName    = OMAPFB_NAME;
			pScrn->name          = OMAPFB_NAME;
			pScrn->Probe         = OMAPFBProbe;
			pScrn->PreInit       = OMAPFBPreInit;
			pScrn->ScreenInit    = OMAPFBScreenInit;
			pScrn->SwitchMode    = OMAPFBSwitchMode;
			pScrn->EnterVT       = OMAPFBEnterVT;
			pScrn->LeaveVT       = OMAPFBLeaveVT;

		} else {
			xf86Msg(X_WARNING, "Could not open '%s': %s",
			        dev ? dev : DEFAULT_DEVICE, strerror(errno));
		}

	}

	free(devSections);

	return foundScreen;
}

static Bool
OMAPFBPreInit(ScrnInfoPtr pScrn, int flags)
{
	OMAPFBPtr ofb;
	EntityInfoPtr pEnt;
	char *dev;
	rgb zeros = { 0, 0, 0 };
	struct stat st;

	if (flags & PROBE_DETECT) return FALSE;
	
	/* We only support single entity */
	if (pScrn->numEntities != 1)
		return FALSE;
	
	/* Setup the configured monitor */
	pScrn->monitor = pScrn->confScreen->monitor;
	
	/* Get our private data */
	OMAPFBEnsureRec(pScrn);
	ofb = OMAPFB(pScrn);

	pEnt = xf86GetEntityInfo(pScrn->entityList[0]);
	
	/* Open the device node */
	dev = xf86FindOptionValue(pEnt->device->options, "fb");
	ofb->fd = open(dev != NULL ? dev : DEFAULT_DEVICE, O_RDWR, 0);
	if (ofb->fd == -1) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "%s: Opening '%s' failed: %s\n", __FUNCTION__,
		           dev != NULL ? dev : DEFAULT_DEVICE, strerror(errno));
		OMAPFBFreeRec(pScrn);
		return FALSE;
	}

	if (ioctl (ofb->fd, FBIOGET_FSCREENINFO, &ofb->fixed_info)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "%s: Reading hardware info failed: %s\n",
		           __FUNCTION__, strerror(errno));
		OMAPFBFreeRec(pScrn);
		return FALSE;
	}

	/* Try to detect what LCD controller we're using */
	OMAPFBProbeController(ofb->ctrl_name);

	/* Do we have the DSS kernel API? */
	if (stat(SYSFS_DSS_DIR, &st) == 0) {
		ofb->dss = TRUE;
	} else {
		ofb->dss = FALSE;
	}

	/* Print out capabilities, if available */
	if (!ioctl (ofb->fd, OMAPFB_GET_CAPS, &ofb->caps)) {
		OMAPFBPrintCapabilities(pScrn, &ofb->caps,
		                        "Base plane");
	}

	/* Check the memory setup. */
	if (ioctl (ofb->fd, OMAPFB_QUERY_MEM, &ofb->mem_info)) {
		/* As a fallback, set up the mem_info struct from info we know */
		ofb->mem_info.type = OMAPFB_MEMTYPE_SDRAM;
		ofb->mem_info.size = ofb->fixed_info.smem_len;
	}

	pScrn->videoRam  = ofb->fixed_info.smem_len;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "VideoRAM: %iKiB (%s)\n",
	           pScrn->videoRam/1024,
	           ofb->mem_info.type == OMAPFB_MEMTYPE_SDRAM ? "SDRAM" : "SRAM");

	if (ioctl (ofb->fd, FBIOGET_VSCREENINFO, &ofb->state_info)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "%s: Reading screen state info failed: %s\n",
		           __FUNCTION__, strerror(errno));
		OMAPFBFreeRec(pScrn);
		return FALSE;
	}
	
	if (!xf86SetDepthBpp(pScrn,
			     0, /* depth: overall server default */
			     ofb->state_info.bits_per_pixel,
			     ofb->state_info.bits_per_pixel,
			     Support24bppFb | Support32bppFb))
		return FALSE;

	xf86PrintDepthBpp(pScrn);

	/* This apparently sets the color weights. We're feeding it zeros. */
	if (!xf86SetWeight(pScrn, zeros, zeros)) {
		return FALSE;
	}

	/* Initialize default visual */
	if (!xf86SetDefaultVisual(pScrn, -1))
		return FALSE;

	pScrn->progClock = TRUE;
	pScrn->chipset   = "omapfb";
	
	/* Start with configured virtual size */
	pScrn->virtualX = pScrn->display->virtualX;
	pScrn->virtualY = pScrn->display->virtualY;
	pScrn->displayWidth = ofb->fixed_info.line_length / (ofb->state_info.bits_per_pixel >> 3);

	/* Clamp to actual virtual resolution */
	if (pScrn->virtualX < ofb->state_info.xres_virtual)
		pScrn->virtualX = ofb->state_info.xres_virtual;
	if (pScrn->virtualY < ofb->state_info.yres_virtual)
		pScrn->virtualY = ofb->state_info.yres_virtual;
	
	/* Setup viewport */
	pScrn->frameX0 = 0;
	pScrn->frameY0 = 0;
	pScrn->frameX1 = ofb->state_info.xres;
	pScrn->frameY1 = ofb->state_info.yres;

	pScrn->maxVValue = ofb->state_info.xres_virtual;
	pScrn->maxHValue = ofb->state_info.yres_virtual;

	pScrn->offset.red   = ofb->state_info.red.offset;
	pScrn->offset.green = ofb->state_info.green.offset;
	pScrn->offset.blue  = ofb->state_info.blue.offset;
	pScrn->mask.red     = ((1 << ofb->state_info.red.length) - 1)
	                          << ofb->state_info.red.offset;
	pScrn->mask.green   = ((1 << ofb->state_info.green.length) - 1)
	                          << ofb->state_info.green.offset;
	pScrn->mask.blue    = ((1 << ofb->state_info.blue.length) - 1)
	                          << ofb->state_info.blue.offset;

	pScrn->modes = NULL;

	OMAPFBCRTCInit(pScrn);

	if (ofb->dss) {
		OMAPFBOutputInitDSS(pScrn);
	} else {
		OMAPFBOutputInit(pScrn);
	}

	if (xf86InitialConfiguration(pScrn, TRUE)) {
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "XRandR extension initialized\n");
	} else {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Initial output configuration failed!\n");
	}

	pScrn->currentMode = pScrn->modes;
	ofb->crtc->mode = *pScrn->currentMode;


	/* Disable outputs that are not used */
	xf86DisableUnusedFunctions(pScrn);

	xf86PrintModes(pScrn);
	
	/* Set the screen dpi value (we don't give defaults) */
	xf86SetDpi(pScrn, 0, 0);

	return TRUE;
}

static void
OMAPFBXvScreenInit(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	XF86VideoAdaptorPtr *ptr = NULL;
	XF86VideoAdaptorPtr *omap_adaptors = NULL;
	int on = 0;

	int n = xf86XVListGenericAdaptors(pScrn, &ptr);

	/* Get the omap adaptors */
	on = OMAPFBXVInit(pScrn, &omap_adaptors);

	/* Merge the adaptor lists */
	if (n > 0 || on > 0) {
		int i;
		XF86VideoAdaptorPtr *generic_adaptors = ptr;
		ptr = malloc((n + on) * sizeof(XF86VideoAdaptorPtr));
		for (i = 0; i < n; i++) {
			ptr[i] = generic_adaptors[i];
		}
		for (i = n; i < on; i++) {
			ptr[i] = omap_adaptors[i-n];
		}
		n = n + on;
	}

	if (n == 0 || !xf86XVScreenInit(pScreen, ptr, n)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "XVScreenInit failed\n");
		return;
	}
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "XVideo extension initialized\n");
}

static Bool
OMAPFBCloseScreen(int scrnIndex, ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	OMAPFBPtr ofb = OMAPFB(pScrn);

	munmap(ofb->fb, ofb->mem_info.size);

	pScreen->CloseScreen = ofb->CloseScreen;
	
	return (*pScreen->CloseScreen)(scrnIndex, pScreen);
}

static Bool
OMAPFBScreenInit(int scrnIndex, ScreenPtr pScreen, int argc, char **argv)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	OMAPFBPtr ofb = OMAPFB(pScrn);

	ofb->CloseScreen = pScreen->CloseScreen;
	pScreen->CloseScreen = OMAPFBCloseScreen;

	/* Map our framebuffer memory */
	ofb->fb = mmap (NULL, ofb->mem_info.size,
	                PROT_READ | PROT_WRITE, MAP_SHARED,
	                ofb->fd, 0);
	if (ofb->fb == NULL) {
		xf86DrvMsg(scrnIndex, X_ERROR, "Mapping framebuffer memory failed\n");
		return FALSE;
	}

	/* Reset visuals */
	miClearVisualTypes();

	/* Only support TrueColor for now */
	if (!miSetVisualTypes(pScrn->depth, TrueColorMask,
		pScrn->rgbBits, pScrn->defaultVisual)) {
		xf86DrvMsg(scrnIndex, X_ERROR, "visual type setup failed"
		           " for %d bits per pixel [1]\n",
		           pScrn->bitsPerPixel);
		return FALSE;
	}

	/* Set up pixmap depth information */
	if (!miSetPixmapDepths()) {
		xf86DrvMsg(scrnIndex,X_ERROR,"pixmap depth setup failed\n");
		return FALSE;
	}

	/* Load the fallback module */
	xf86LoadSubModule(pScrn, "fb");

	/* Initialize fallbacks for the screen */
	if (!fbScreenInit(pScreen, ofb->fb, pScrn->virtualX,
	                  pScrn->virtualY, pScrn->xDpi,
	                  pScrn->yDpi, pScrn->displayWidth,
	                  pScrn->bitsPerPixel)) {
		xf86DrvMsg(scrnIndex, X_ERROR, "fbScreenInit failed\n");
		return FALSE;
	}

	/* Setup visual RGB properties */
	if (pScrn->bitsPerPixel > 8) {
		VisualPtr visual = pScreen->visuals + pScreen->numVisuals;
		while (--visual >= pScreen->visuals) {
			if ((visual->class | DynamicClass) == DirectColor) {
				visual->offsetRed = pScrn->offset.red;
				visual->offsetGreen = pScrn->offset.green;
				visual->offsetBlue = pScrn->offset.blue;
				visual->redMask = pScrn->mask.red;
				visual->greenMask = pScrn->mask.green;
				visual->blueMask = pScrn->mask.blue;
			}
		}
	}

	/* Initialize XRender fallbacks */
	if (!fbPictureInit(pScreen, NULL, 0)) {
		xf86DrvMsg(scrnIndex, X_ERROR, "fbPictureInit failed\n");
		return FALSE;
	}
	
	/* Setup default colors */
	xf86SetBlackWhitePixels(pScreen);
	
	/* Initialize software cursor */
	miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

	/* Initialize default colormap */
	if (!miCreateDefColormap(pScreen)) {
		xf86DrvMsg(scrnIndex, X_ERROR,
		           "creating default colormap failed\n");
		return FALSE;
	}

	/* Make sure the plane is up and running */
	if (ioctl (ofb->fd, OMAPFB_QUERY_PLANE, &ofb->plane_info)) {
		/* This is non-fatal since we might be running against older
		 * kernel driver in which case we only do basic 2D stuff...
		 */
		xf86DrvMsg(scrnIndex, X_ERROR, "Reading plane info failed\n");
	} else if (!ofb->dss) {

		ofb->plane_info.enabled = 1;
		ofb->plane_info.out_width = ofb->state_info.xres;
		ofb->plane_info.out_height = ofb->state_info.yres;

		if (ioctl (ofb->fd, OMAPFB_SETUP_PLANE, &ofb->plane_info)) {
			xf86DrvMsg(scrnIndex, X_ERROR,
			            "%s: Plane setup failed: %s\n",
			            __FUNCTION__, strerror(errno));
			return FALSE;
		}
	}

	if (ioctl(ofb->fd, FBIOBLANK, (void *)VESA_NO_BLANKING)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
		           "FBIOBLANK: %s\n", strerror(errno));
	}

	/* Setup screen saving and DPMS support, these will go through the
	 * Output functions
	 */
	pScreen->SaveScreen = xf86SaveScreen;
	xf86DPMSInit(pScreen, xf86DPMSSet, 0);

#ifdef USE_EXA
	/* EXA init */
	xf86LoadSubModule(pScrn, "exa");

	/* TODO: This should depend on the AccelMethod option */
	ofb->exa = exaDriverAlloc();
	if (OMAPFBSetupExa(ofb)) {
		exaDriverInit(pScreen, ofb->exa);
	} else {
		free(ofb->exa);
		ofb->exa = NULL;
	}
#endif

	/* Initialize XVideo support */
	/* FIXME: Currently dss & XV do not co-exist */
	if (!ofb->dss)
		OMAPFBXvScreenInit(pScreen);

	/* Initialize RANDR support */
	xf86CrtcScreenInit(pScreen);

	return TRUE;
}

static Bool OMAPFBSwitchMode(int scrnIndex, DisplayModePtr mode, int flags)
{
	return xf86SetSingleMode (xf86Screens[scrnIndex], mode, RR_Rotate_0);
}

void
OMAPFBPrintCapabilities(ScrnInfoPtr pScrn,
                        struct omapfb_caps *caps,
                        const char *plane_name)
{
	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	           "%s capabilities:\n%s%s%s%s%s%s%s%s%s",
	           plane_name,
	           (caps->ctrl & OMAPFB_CAPS_MANUAL_UPDATE) ?
	             "\tManual updates\n" : "",
	           (caps->ctrl & OMAPFB_CAPS_TEARSYNC) ?
	             "\tTearsync\n" : "",
	           (caps->ctrl & OMAPFB_CAPS_PLANE_RELOCATE_MEM) ?
	             "\tPlane memory relocation\n" : "",
	           (caps->ctrl & OMAPFB_CAPS_PLANE_SCALE) ?
	             "\tPlane scaling\n" : "",
	           (caps->ctrl & OMAPFB_CAPS_WINDOW_PIXEL_DOUBLE) ?
	             "\tUpdate window pixel doubling\n" : "",
	           (caps->ctrl & OMAPFB_CAPS_WINDOW_SCALE) ?
	             "\tUpdate window scaling\n" : "",
	           (caps->ctrl & OMAPFB_CAPS_WINDOW_OVERLAY) ?
	             "\tOverlays\n" : "",
	           (caps->ctrl & OMAPFB_CAPS_WINDOW_ROTATE) ?
	             "\tRotation\n" : "",
	           (caps->ctrl & OMAPFB_CAPS_SET_BACKLIGHT) ?
	             "\tBacklight control\n" : ""
	           );

#define MAKE_STR(f) #f
#define PRINT_FORMAT(f) (caps->plane_color & OMAPFB_COLOR_##f) ? MAKE_STR(\t##f\n) : ""

	xf86DrvMsg(pScrn->scrnIndex, X_INFO,
	           "%s supports the following image formats:\n%s%s%s%s%s%s%s%s%s",
	           plane_name,
	           PRINT_FORMAT(RGB565),
	           PRINT_FORMAT(YUV422),
	           PRINT_FORMAT(YUV420),
	           PRINT_FORMAT(CLUT_8BPP),
	           PRINT_FORMAT(CLUT_4BPP),
	           PRINT_FORMAT(CLUT_2BPP),
	           PRINT_FORMAT(CLUT_1BPP),
	           PRINT_FORMAT(RGB444),
	           PRINT_FORMAT(YUY422)
	           );
}

/*** Unimplemented: */

static Bool
OMAPFBEnterVT(int scrnIndex, int flags)
{
	xf86Msg(X_NOT_IMPLEMENTED, "%s\n", __FUNCTION__);
	return TRUE;
}

static void
OMAPFBLeaveVT(int scrnIndex, int flags)
{
	xf86Msg(X_NOT_IMPLEMENTED, "%s\n", __FUNCTION__);
}

static Bool
OMAPFBDriverFunc(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr)
{
	xorgHWFlags *flag;

	switch (op) {
		case GET_REQUIRED_HW_INTERFACES:
			flag = (CARD32*)ptr;
			(*flag) = 0;
			return TRUE;
		default:
			return FALSE;
	}
}

/*** Module and driver setup */

_X_EXPORT DriverRec OMAPFB = {
	OMAPFB_VERSION,
	OMAPFB_DRIVER_NAME,
	OMAPFBIdentify,
	OMAPFBProbe,
	OMAPFBAvailableOptions,
	NULL,
	0,
	OMAPFBDriverFunc
};

/** Module loader support */

MODULESETUPPROTO(OMAPFBSetup);

static XF86ModuleVersionInfo OMAPFBVersRec =
{
	OMAPFB_NAME,
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
	ABI_CLASS_VIDEODRV,
	ABI_VIDEODRV_VERSION,
	NULL,
	{0,0,0,0}
};

_X_EXPORT XF86ModuleData omapfbModuleData = { &OMAPFBVersRec, OMAPFBSetup, NULL };

pointer
OMAPFBSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
	static Bool setupDone = FALSE;

	if (!setupDone) {
		setupDone = TRUE;
		xf86AddDriver(&OMAPFB, module, HaveDriverFuncs);
		return (pointer)1;
	} else {
		if (errmaj) *errmaj = LDR_ONCEONLY;
		return NULL;
	}
}


