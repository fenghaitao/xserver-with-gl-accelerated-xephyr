/*
 * Xephyr - A kdrive X server thats runs in a host X window.
 *          Authored by Matthew Allum <mallum@openedhand.com>
 *
 * Copyright © 2007 OpenedHand Ltd
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of OpenedHand Ltd not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission. OpenedHand Ltd makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * OpenedHand Ltd DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OpenedHand Ltd BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *    Haitao Feng <haitao.feng@intel.com>
 */
#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#include "scrnintstr.h"
#include <X11/Xutil.h>
#include <X11/Xlibint.h>
#include <GL/glx.h>
#include "hostx.h"
#define _HAVE_XALLOC_DECLS
#include "ephyrlog.h"
#include "dixstruct.h"
#include "pixmapstr.h"
#include "ephyrdri2.h"

#ifndef TRUE
#define TRUE 1
#endif /*TRUE*/

#ifndef FALSE
#define FALSE 0
#endif /*FALSE*/

Bool
ephyrDRI2Connect(ScreenPtr pScreen, unsigned int driverType, int *fd,
	    char **driverName, char **deviceName)
{
    Display *dpy = hostx_get_display () ;
    return DRI2Connect(dpy, RootWindow(dpy, DefaultScreen(dpy)), driverName, deviceName);
}

Bool ephyrDRI2Authenticate(ScreenPtr pScreen, drm_magic_t magic)
{
    Display *dpy = hostx_get_display () ;
    return DRI2Authenticate(dpy, RootWindow(dpy, DefaultScreen(dpy)), magic);
}

int ephyrDRI2CreateDrawable(XID drawable)
{
    Display *dpy = hostx_get_display () ;
    DRI2CreateDrawable(dpy, drawable);
    return Success ;
}

DRI2Buffer *ephyrDRI2GetBuffers(XID drawable,
			     int *width,
			     int *height,
			     unsigned int *attachments,
			     int count,
			     int *out_count)
{
    Display *dpy = hostx_get_display ();
    return DRI2GetBuffers(dpy, drawable, width, height, attachments, count, out_count);
}

DRI2Buffer *ephyrDRI2GetBuffersWithFormat(XID drawable,
	int *width, int *height, unsigned int *attachments, int count,
	int *out_count)
{
    Display *dpy = hostx_get_display ();
    return DRI2GetBuffersWithFormat(dpy, drawable, width, height, attachments, count, out_count);
}

int ephyrDRI2CopyRegion(XID drawable,
		   EphyrDRI2WindowPair *pair,
		   RegionPtr pRegion,
		   unsigned int dest,
		   unsigned int src)
{
    Display *dpy = hostx_get_display ();
    XRectangle xrect;
    XserverRegion region;

    xrect.x = pRegion->extents.x1;
    xrect.y = pRegion->extents.y1;
    xrect.width  = pRegion->extents.x2 - pRegion->extents.x1;
    xrect.height = pRegion->extents.y2 - pRegion->extents.y1;

    region = XFixesCreateRegion(dpy, &xrect, 1);
    DRI2CopyRegion(dpy, pair->remote, region, dest, src);
    XFixesDestroyRegion(dpy, region);

    return Success;
}

void ephyrDRI2DestroyDrawable(XID drawable)
{
    Display *dpy = hostx_get_display ();
    DRI2DestroyDrawable(dpy, drawable);
}
