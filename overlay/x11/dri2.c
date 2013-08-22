/*
 * Copyright © 2008 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Soft-
 * ware"), to deal in the Software without restriction, including without
 * limitation the rights to use, copy, modify, merge, publish, distribute,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, provided that the above copyright
 * notice(s) and this permission notice appear in all copies of the Soft-
 * ware and that both the above copyright notice(s) and this permission
 * notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABIL-
 * ITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY
 * RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN
 * THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSE-
 * QUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFOR-
 * MANCE OF THIS SOFTWARE.
 *
 * Except as contained in this notice, the name of a copyright holder shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization of
 * the copyright holder.
 *
 * Authors:
 *   Kristian Høgsberg (krh@redhat.com)
 */

#include <stdio.h>
#include <X11/Xlibint.h>
#include <X11/extensions/Xext.h>
#include <X11/extensions/extutil.h>
#include <X11/extensions/dri2proto.h>
#include <X11/extensions/dri2tokens.h>
#include <xf86drm.h>
#include <drm.h>
#include <fcntl.h>
#include <unistd.h>

#include "dri2.h"

static char dri2ExtensionName[] = DRI2_NAME;
static XExtensionInfo *dri2Info;
static XEXT_GENERATE_CLOSE_DISPLAY (DRI2CloseDisplay, dri2Info)

static /* const */ XExtensionHooks dri2ExtensionHooks = {
	NULL,                   /* create_gc */
	NULL,                   /* copy_gc */
	NULL,                   /* flush_gc */
	NULL,                   /* free_gc */
	NULL,                   /* create_font */
	NULL,                   /* free_font */
	DRI2CloseDisplay,       /* close_display */
};

static XEXT_GENERATE_FIND_DISPLAY (DRI2FindDisplay,
				   dri2Info,
				   dri2ExtensionName,
				   &dri2ExtensionHooks,
				   0, NULL)

static Bool
DRI2Connect(Display *dpy, XID window, char **driverName, char **deviceName)
{
	XExtDisplayInfo *info = DRI2FindDisplay(dpy);
	xDRI2ConnectReply rep;
	xDRI2ConnectReq *req;

	XextCheckExtension(dpy, info, dri2ExtensionName, False);

	LockDisplay(dpy);
	GetReq(DRI2Connect, req);
	req->reqType = info->codes->major_opcode;
	req->dri2ReqType = X_DRI2Connect;
	req->window = window;
	req->driverType = DRI2DriverDRI;
	if (!_XReply(dpy, (xReply *) & rep, 0, xFalse)) {
		UnlockDisplay(dpy);
		SyncHandle();
		return False;
	}

	if (rep.driverNameLength == 0 && rep.deviceNameLength == 0) {
		UnlockDisplay(dpy);
		SyncHandle();
		return False;
	}

	*driverName = Xmalloc(rep.driverNameLength + 1);
	if (*driverName == NULL) {
		_XEatData(dpy,
				((rep.driverNameLength + 3) & ~3) +
				((rep.deviceNameLength + 3) & ~3));
		UnlockDisplay(dpy);
		SyncHandle();
		return False;
	}
	_XReadPad(dpy, *driverName, rep.driverNameLength);
	(*driverName)[rep.driverNameLength] = '\0';

	*deviceName = Xmalloc(rep.deviceNameLength + 1);
	if (*deviceName == NULL) {
		Xfree(*driverName);
		_XEatData(dpy, ((rep.deviceNameLength + 3) & ~3));
		UnlockDisplay(dpy);
		SyncHandle();
		return False;
	}
	_XReadPad(dpy, *deviceName, rep.deviceNameLength);
	(*deviceName)[rep.deviceNameLength] = '\0';

	UnlockDisplay(dpy);
	SyncHandle();

	return True;
}

static Bool
DRI2Authenticate(Display * dpy, XID window, unsigned int magic)
{
	XExtDisplayInfo *info = DRI2FindDisplay(dpy);
	xDRI2AuthenticateReq *req;
	xDRI2AuthenticateReply rep;

	XextCheckExtension(dpy, info, dri2ExtensionName, False);

	LockDisplay(dpy);
	GetReq(DRI2Authenticate, req);
	req->reqType = info->codes->major_opcode;
	req->dri2ReqType = X_DRI2Authenticate;
	req->window = window;
	req->magic = magic;

	if (!_XReply(dpy, (xReply *) & rep, 0, xFalse)) {
		UnlockDisplay(dpy);
		SyncHandle();
		return False;
	}

	UnlockDisplay(dpy);
	SyncHandle();

	return rep.authenticated;
}

int dri2_open(Display *dpy)
{
	drm_auth_t auth;
	char *driver, *device;
	int fd;

	if (!DRI2Connect(dpy, DefaultRootWindow(dpy), &driver, &device))
		return -1;

	fd = open(device, O_RDWR);
	if (fd < 0)
		return -1;

	if (drmIoctl(fd, DRM_IOCTL_GET_MAGIC, &auth))
		goto err_fd;

	if (!DRI2Authenticate(dpy, DefaultRootWindow(dpy), auth.magic))
		goto err_fd;

	return fd;

err_fd:
	close(fd);
	return -1;
}
