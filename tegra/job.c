/*
 * Copyright © 2012, 2013 Thierry Reding
 * Copyright © 2013 Erik Faye-Lund
 * Copyright © 2014 NVIDIA Corporation
 *
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
#include <stdlib.h>
#include <string.h>

#include <xf86drm.h>

#include "private.h"

drm_private
int drm_tegra_job_add_waitchk(struct drm_tegra_job *job,
			      const struct drm_tegra_waitchk *waitchk)
{
	struct drm_tegra_waitchk *waitchks;
	size_t size;

	size = (job->num_waitchks + 1) * sizeof(*waitchk);

	waitchks = realloc(job->waitchks, size);
	if (!waitchks)
		return -ENOMEM;

	job->waitchks = waitchks;

	job->waitchks[job->num_waitchks++] = *waitchk;

	return 0;
}

drm_private
int drm_tegra_job_add_reloc(struct drm_tegra_job *job,
			    const struct drm_tegra_reloc *reloc)
{
	struct drm_tegra_reloc *relocs;
	size_t size;

	size = (job->num_relocs + 1) * sizeof(*reloc);

	relocs = realloc(job->relocs, size);
	if (!relocs)
		return -ENOMEM;

	job->relocs = relocs;

	job->relocs[job->num_relocs++] = *reloc;

	return 0;
}

drm_private
int drm_tegra_job_add_cmdbuf(struct drm_tegra_job *job,
			     const struct drm_tegra_cmdbuf *cmdbuf)
{
	struct drm_tegra_cmdbuf *cmdbufs;
	size_t size;

	size = (job->num_cmdbufs + 1) * sizeof(*cmdbuf);

	cmdbufs = realloc(job->cmdbufs, size);
	if (!cmdbufs)
		return -ENOMEM;

	cmdbufs[job->num_cmdbufs++] = *cmdbuf;
	job->cmdbufs = cmdbufs;

	return 0;
}

int drm_tegra_job_new(struct drm_tegra_job **jobp,
		      struct drm_tegra_channel *channel)
{
	struct drm_tegra_job_private *priv;
	struct drm_tegra_job *job;

	if (!jobp || !channel)
		return -EINVAL;

	priv = calloc(1, sizeof(*priv));
	if (!priv)
		return -ENOMEM;

	job = &priv->base;

	DRMINITLISTHEAD(&priv->pushbufs);
	job->channel = channel;
	job->syncpt = channel->syncpt;

	*jobp = job;

	return 0;
}

int drm_tegra_job_free(struct drm_tegra_job *job)
{
	struct drm_tegra_pushbuf_private *pushbuf;
	struct drm_tegra_pushbuf_private *temp;
	struct drm_tegra_job_private *priv;

	if (!job)
		return -EINVAL;

	priv = drm_tegra_job(job);

	DRMLISTFOREACHENTRYSAFE(pushbuf, temp, &priv->pushbufs, list)
		drm_tegra_pushbuf_free(&pushbuf->base);

	free(job->bos);
	free(job->cmdbufs);
	free(job->relocs);
	free(job->waitchks);
	free(priv);

	return 0;
}

int drm_tegra_job_submit(struct drm_tegra_job *job,
			 struct drm_tegra_fence **fencep)
{
	struct drm_tegra *drm;
	struct drm_tegra_fence *fence = NULL;
	struct drm_tegra_syncpt *syncpts;
	struct drm_tegra_submit args;
	struct drm_tegra_job_private *priv;
	int err;

	if (!job)
		return -EINVAL;

	priv = drm_tegra_job(job);

	/*
	 * Make sure the current command stream buffer is queued for
	 * submission.
	 */
	err = drm_tegra_pushbuf_queue(priv->pushbuf, false);
	if (err < 0)
		return err;

	priv->pushbuf = NULL;

	if (fencep) {
		fence = calloc(1, sizeof(*fence));
		if (!fence)
			return -ENOMEM;
	}

	syncpts = calloc(1, sizeof(*syncpts));
	if (!syncpts) {
		free(fence);
		return -ENOMEM;
	}

	syncpts[0].id = job->syncpt;
	syncpts[0].incrs = job->increments;

	memset(&args, 0, sizeof(args));
	args.context = job->channel->context;
	args.num_syncpts = 1;
	args.num_bos = job->num_bos;
	args.num_cmdbufs = job->num_cmdbufs;
	args.num_relocs = job->num_relocs;
	args.num_waitchks = job->num_waitchks;
	args.timeout = 1000;

	args.syncpts = (uintptr_t)syncpts;
	args.bos = (uintptr_t)job->bos;
	args.cmdbufs = (uintptr_t)job->cmdbufs;
	args.relocs = (uintptr_t)job->relocs;
	args.waitchks = (uintptr_t)job->waitchks;

	drm = job->channel->drm;
	err = drmCommandWriteRead(drm->fd, DRM_TEGRA_SUBMIT, &args,
				  sizeof(args));
	if (err < 0) {
		free(syncpts);
		free(fence);
		return err;
	}

	if (fence) {
		fence->syncpt = job->syncpt;
		fence->value = args.fence;
		fence->drm = drm;
		*fencep = fence;
	}

	free(syncpts);

	return 0;
}

int drm_tegra_job_set_class(struct drm_tegra_job *job, uint32_t class_id)
{
	struct drm_tegra_job_private *priv;
	int err;

	if (!job)
		return -EINVAL;

	priv = drm_tegra_job(job);

	if (priv->pushbuf &&
	    priv->pushbuf->start != priv->pushbuf->base.ptr) {
		err = drm_tegra_pushbuf_queue(priv->pushbuf, true);
		if (err < 0)
			return err;
	}

	job->current_class = class_id;

	return 0;
}

drm_private
int drm_tegra_job_add_bo(struct drm_tegra_job *job,
			 const struct drm_tegra_bo *bo,
			 bool cmdbuf, bool write)
{
	struct drm_tegra_submit_bo *bos;
	struct drm_tegra_submit_bo sbo;
	unsigned int i;
	size_t size;

	memset(&sbo, 0, sizeof(sbo));

	sbo.handle = bo->handle;

	if (write)
		sbo.flags |= DRM_TEGRA_SUBMIT_BO_WRITE_MADV;

	if (cmdbuf)
		sbo.flags |= DRM_TEGRA_SUBMIT_BO_IS_CMDBUF;

	/* squash duplicated BO */
	for (i = 0; i < job->num_bos; i++) {
		if (job->bos[i].handle == sbo.handle) {
			job->bos[i].flags |= sbo.flags;
			return i;
		}
	}

	size = (job->num_bos + 1) * sizeof(sbo);

	bos = realloc(job->bos, size);
	if (!bos)
		return -ENOMEM;

	job->bos = bos;

	job->bos[job->num_bos++] = sbo;

	return job->num_bos - 1;
}
