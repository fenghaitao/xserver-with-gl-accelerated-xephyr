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

#ifndef __EPHYRDRI2_H__
#define __EPHYRDRI2_H__

#include <X11/extensions/dri2tokens.h>
#include "dri2.h"
#include "ephyrdri2ext.h"

void ephyrDRI2CloseScreen(ScreenPtr pScreen);

Bool ephyrDRI2Connect(ScreenPtr pScreen,
		 unsigned int driverType,
		 int *fd,
		 char **driverName,
		 char **deviceName);

Bool ephyrDRI2Authenticate(ScreenPtr pScreen, drm_magic_t magic);

int ephyrDRI2CreateDrawable(XID drawable);

void ephyrDRI2DestroyDrawable(XID drawable);

DRI2Buffer *ephyrDRI2GetBuffers(XID drawable,
			     int *width,
			     int *height,
			     unsigned int *attachments,
			     int count,
			     int *out_count);

int ephyrDRI2CopyRegion(XID drawable,
		   EphyrDRI2WindowPair *pair,
		   RegionPtr pRegion,
		   unsigned int dest,
		   unsigned int src);

/**
 * Determine the major and minor version of the DRI2 extension.
 *
 * Provides a mechanism to other modules (e.g., 2D drivers) to determine the
 * version of the DRI2 extension.  While it is possible to peek directly at
 * the \c XF86ModuleData from a layered module, such a module will fail to
 * load (due to an unresolved symbol) if the DRI2 extension is not loaded.
 *
 * \param major  Location to store the major verion of the DRI2 extension
 * \param minor  Location to store the minor verion of the DRI2 extension
 *
 * \note
 * This interface was added some time after the initial release of the DRI2
 * module.  Layered modules that wish to use this interface must first test
 * its existance by calling \c xf86LoaderCheckSymbol.
 */
extern _X_EXPORT void ephyrDRI2Version(int *major, int *minor);

extern _X_EXPORT DRI2Buffer *ephyrDRI2GetBuffersWithFormat(XID drawable,
	int *width, int *height, unsigned int *attachments, int count,
	int *out_count);

#endif /*__EPHYRDRI2_H__*/
