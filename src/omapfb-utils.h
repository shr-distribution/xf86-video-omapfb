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

#ifndef __OMAPFB_UTILS_H__
#define __OMAPFB_UTILS_H__

#include "xorg-server.h"
#include "xf86Modes.h"

#define SYSFS_DSS_DIR "/sys/devices/platform/omapdss"

int read_sysfs_value(const char *fname, char *value, size_t len);
int write_sysfs_value(const char *fname, const char *value);

int read_dss_sysfs_value(const char *target, int index, const char *entry, char *value, size_t len);
int write_dss_sysfs_value(const char *target, int index, const char *entry, const char *value);

int omapfb_timings_to_mode(const char *timings, DisplayModePtr mode);
void mode_to_string(DisplayModePtr mode, char *mode_str, int size);

#endif /* __OMAPFB_UTILS_H__ */

