/*
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <string.h>

#include <xf86drm.h>

#include "private.h"

int drm_tegra_get_excl_syncpt(struct drm_tegra_channel *channel,
			      struct drm_tegra_syncpoint *syncpt,
			      bool with_base)
{
	struct drm_tegra_get_exclusive_syncpt args;
	int err;

	if (!channel || !syncpt)
		return -EINVAL;

	memset(&args, 0, sizeof(args));
	args.context = channel->context;

	if (with_base)
		args.flags |= DRM_TEGRA_EXCL_SYNCPT_WITH_BASE;

	err = drmCommandWriteRead(channel->drm->fd,
				  DRM_TEGRA_GET_EXCLUSIVE_SYNCPT,
				  &args, sizeof(args));
	if (err < 0)
		return err;

	syncpt->id = args.id;
	syncpt->drm = channel->drm;
	syncpt->value = args.value;
	syncpt->index = args.index;
	syncpt->context = channel->context;

	return 0;
}

int drm_tegra_put_excl_syncpt(struct drm_tegra_syncpoint *syncpt)
{
	struct drm_tegra_put_exclusive_syncpt args;
	int err;

	if (!syncpt)
		return -EINVAL;

	memset(&args, 0, sizeof(args));
	args.index = syncpt->index;
	args.context = syncpt->context;

	err = drmCommandWriteRead(syncpt->drm->fd,
				  DRM_TEGRA_PUT_EXCLUSIVE_SYNCPT,
				  &args, sizeof(args));
	if (err < 0)
		return err;

	return 0;
}
