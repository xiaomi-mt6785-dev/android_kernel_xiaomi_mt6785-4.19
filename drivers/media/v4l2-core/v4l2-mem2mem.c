/*
 * Memory-to-memory device framework for Video for Linux 2 and videobuf.
 *
 * Helper functions for devices that use videobuf buffers for both their
 * source and destination.
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 * Pawel Osciak, <pawel@osciak.com>
 * Marek Szyprowski, <m.szyprowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <media/media-device.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>

MODULE_DESCRIPTION("Mem to mem device framework for videobuf");
MODULE_AUTHOR("Pawel Osciak, <pawel@osciak.com>");
MODULE_LICENSE("GPL");

static bool debug;
module_param(debug, bool, 0644);

#define dprintk(fmt, arg...)						\
	do {								\
		if (debug)						\
			printk(KERN_DEBUG "%s: " fmt, __func__, ## arg);\
	} while (0)


/* Instance is already queued on the job_queue */
#define TRANS_QUEUED		(1 << 0)
/* Instance is currently running in hardware */
#define TRANS_RUNNING		(1 << 1)
/* Instance is currently aborting */
#define TRANS_ABORT		(1 << 2)


/* Offset base for buffers on the destination queue - used to distinguish
 * between source and destination buffers when mmapping - they receive the same
 * offsets but for different queues */
#define DST_QUEUE_OFF_BASE	(1 << 30)

enum v4l2_m2m_entity_type {
	MEM2MEM_ENT_TYPE_SOURCE,
	MEM2MEM_ENT_TYPE_SINK,
	MEM2MEM_ENT_TYPE_PROC
};

static const char * const m2m_entity_name[] = {
	"source",
	"sink",
	"proc"
};

/**
 * struct v4l2_m2m_dev - per-device context
 * @source:		&struct media_entity pointer with the source entity
 *			Used only when the M2M device is registered via
 *			v4l2_m2m_unregister_media_controller().
 * @source_pad:		&struct media_pad with the source pad.
 *			Used only when the M2M device is registered via
 *			v4l2_m2m_unregister_media_controller().
 * @sink:		&struct media_entity pointer with the sink entity
 *			Used only when the M2M device is registered via
 *			v4l2_m2m_unregister_media_controller().
 * @sink_pad:		&struct media_pad with the sink pad.
 *			Used only when the M2M device is registered via
 *			v4l2_m2m_unregister_media_controller().
 * @proc:		&struct media_entity pointer with the M2M device itself.
 * @proc_pads:		&struct media_pad with the @proc pads.
 *			Used only when the M2M device is registered via
 *			v4l2_m2m_unregister_media_controller().
 * @intf_devnode:	&struct media_intf devnode pointer with the interface
 *			with controls the M2M device.
 * @curr_ctx:		currently running instance
 * @job_queue:		instances queued to run
 * @job_spinlock:	protects job_queue
 * @job_work:		worker to run queued jobs.
 * @m2m_ops:		driver callbacks
 */
struct v4l2_m2m_dev {
	struct v4l2_m2m_ctx	*curr_ctx;
#ifdef CONFIG_MEDIA_CONTROLLER
	struct media_entity	*source;
	struct media_pad	source_pad;
	struct media_entity	sink;
	struct media_pad	sink_pad;
	struct media_entity	proc;
	struct media_pad	proc_pads[2];
	struct media_intf_devnode *intf_devnode;
#endif

	struct list_head	job_queue;
	spinlock_t		job_spinlock;
	struct work_struct	job_work;

	const struct v4l2_m2m_ops *m2m_ops;
};

static struct v4l2_m2m_queue_ctx *get_queue_ctx(struct v4l2_m2m_ctx *m2m_ctx,
						enum v4l2_buf_type type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return &m2m_ctx->out_q_ctx;
	else
		return &m2m_ctx->cap_q_ctx;
}

struct vb2_queue *v4l2_m2m_get_vq(struct v4l2_m2m_ctx *m2m_ctx,
				       enum v4l2_buf_type type)
{
	struct v4l2_m2m_queue_ctx *q_ctx;

	q_ctx = get_queue_ctx(m2m_ctx, type);
	if (!q_ctx)
		return NULL;

	return &q_ctx->q;
}
EXPORT_SYMBOL(v4l2_m2m_get_vq);

void *v4l2_m2m_next_buf(struct v4l2_m2m_queue_ctx *q_ctx)
{
	struct v4l2_m2m_buffer *b;
	unsigned long flags;

	spin_lock_irqsave(&q_ctx->rdy_spinlock, flags);

	if (list_empty(&q_ctx->rdy_queue)) {
		spin_unlock_irqrestore(&q_ctx->rdy_spinlock, flags);
		return NULL;
	}

	b = list_first_entry(&q_ctx->rdy_queue, struct v4l2_m2m_buffer, list);
	spin_unlock_irqrestore(&q_ctx->rdy_spinlock, flags);
	return &b->vb;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_next_buf);

void *v4l2_m2m_last_buf(struct v4l2_m2m_queue_ctx *q_ctx)
{
	struct v4l2_m2m_buffer *b;
	unsigned long flags;

	spin_lock_irqsave(&q_ctx->rdy_spinlock, flags);

	if (list_empty(&q_ctx->rdy_queue)) {
		spin_unlock_irqrestore(&q_ctx->rdy_spinlock, flags);
		return NULL;
	}

	b = list_last_entry(&q_ctx->rdy_queue, struct v4l2_m2m_buffer, list);
	spin_unlock_irqrestore(&q_ctx->rdy_spinlock, flags);
	return &b->vb;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_last_buf);

void *v4l2_m2m_buf_remove(struct v4l2_m2m_queue_ctx *q_ctx)
{
	struct v4l2_m2m_buffer *b;
	unsigned long flags;

	spin_lock_irqsave(&q_ctx->rdy_spinlock, flags);
	if (list_empty(&q_ctx->rdy_queue)) {
		spin_unlock_irqrestore(&q_ctx->rdy_spinlock, flags);
		return NULL;
	}
	b = list_first_entry(&q_ctx->rdy_queue, struct v4l2_m2m_buffer, list);
	list_del(&b->list);
	q_ctx->num_rdy--;
	spin_unlock_irqrestore(&q_ctx->rdy_spinlock, flags);

	return &b->vb;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_buf_remove);

void v4l2_m2m_buf_remove_by_buf(struct v4l2_m2m_queue_ctx *q_ctx,
				struct vb2_v4l2_buffer *vbuf)
{
	struct v4l2_m2m_buffer *b;
	unsigned long flags;

	spin_lock_irqsave(&q_ctx->rdy_spinlock, flags);
	b = container_of(vbuf, struct v4l2_m2m_buffer, vb);
	list_del(&b->list);
	q_ctx->num_rdy--;
	spin_unlock_irqrestore(&q_ctx->rdy_spinlock, flags);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_buf_remove_by_buf);

struct vb2_v4l2_buffer *
v4l2_m2m_buf_remove_by_idx(struct v4l2_m2m_queue_ctx *q_ctx, unsigned int idx)

{
	struct v4l2_m2m_buffer *b, *tmp;
	struct vb2_v4l2_buffer *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&q_ctx->rdy_spinlock, flags);
	list_for_each_entry_safe(b, tmp, &q_ctx->rdy_queue, list) {
		if (b->vb.vb2_buf.index == idx) {
			list_del(&b->list);
			q_ctx->num_rdy--;
			ret = &b->vb;
			break;
		}
	}
	spin_unlock_irqrestore(&q_ctx->rdy_spinlock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_buf_remove_by_idx);

/*
 * Scheduling handlers
 */

void *v4l2_m2m_get_curr_priv(struct v4l2_m2m_dev *m2m_dev)
{
	unsigned long flags;
	void *ret = NULL;

	spin_lock_irqsave(&m2m_dev->job_spinlock, flags);
	if (m2m_dev->curr_ctx)
		ret = m2m_dev->curr_ctx->priv;
	spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);

	return ret;
}
EXPORT_SYMBOL(v4l2_m2m_get_curr_priv);

/**
 * v4l2_m2m_try_run() - select next job to perform and run it if possible
 * @m2m_dev: per-device context
 *
 * Get next transaction (if present) from the waiting jobs list and run it.
 *
 * Note that this function can run on a given v4l2_m2m_ctx context,
 * but call .device_run for another context.
 */
static void v4l2_m2m_try_run(struct v4l2_m2m_dev *m2m_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&m2m_dev->job_spinlock, flags);
	if (NULL != m2m_dev->curr_ctx) {
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);
		dprintk("Another instance is running, won't run now\n");
		return;
	}

	if (list_empty(&m2m_dev->job_queue)) {
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);
		dprintk("No job pending\n");
		return;
	}

	m2m_dev->curr_ctx = list_first_entry(&m2m_dev->job_queue,
				   struct v4l2_m2m_ctx, queue);
	m2m_dev->curr_ctx->job_flags |= TRANS_RUNNING;
	spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);

	dprintk("Running job on m2m_ctx: %p\n", m2m_dev->curr_ctx);
	m2m_dev->m2m_ops->device_run(m2m_dev->curr_ctx->priv);
}

/*
 * __v4l2_m2m_try_queue() - queue a job
 * @m2m_dev: m2m device
 * @m2m_ctx: m2m context
 *
 * Check if this context is ready to queue a job.
 *
 * This function can run in interrupt context.
 */
static void __v4l2_m2m_try_queue(struct v4l2_m2m_dev *m2m_dev,
				 struct v4l2_m2m_ctx *m2m_ctx)
{
	unsigned long flags_job, flags_out, flags_cap;

	dprintk("Trying to schedule a job for m2m_ctx: %p\n", m2m_ctx);

	if (!m2m_ctx->out_q_ctx.q.streaming
	    || !m2m_ctx->cap_q_ctx.q.streaming) {
		dprintk("Streaming needs to be on for both queues\n");
		return;
	}

	spin_lock_irqsave(&m2m_dev->job_spinlock, flags_job);

	/* If the context is aborted then don't schedule it */
	if (m2m_ctx->job_flags & TRANS_ABORT) {
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags_job);
		dprintk("Aborted context\n");
		return;
	}

	if (m2m_ctx->job_flags & TRANS_QUEUED) {
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags_job);
		dprintk("On job queue already\n");
		return;
	}

	spin_lock_irqsave(&m2m_ctx->out_q_ctx.rdy_spinlock, flags_out);
	if (list_empty(&m2m_ctx->out_q_ctx.rdy_queue)
	    && !m2m_ctx->out_q_ctx.buffered) {
		spin_unlock_irqrestore(&m2m_ctx->out_q_ctx.rdy_spinlock,
					flags_out);
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags_job);
		dprintk("No input buffers available\n");
		return;
	}
	spin_lock_irqsave(&m2m_ctx->cap_q_ctx.rdy_spinlock, flags_cap);
	if (list_empty(&m2m_ctx->cap_q_ctx.rdy_queue)
	    && !m2m_ctx->cap_q_ctx.buffered) {
		spin_unlock_irqrestore(&m2m_ctx->cap_q_ctx.rdy_spinlock,
					flags_cap);
		spin_unlock_irqrestore(&m2m_ctx->out_q_ctx.rdy_spinlock,
					flags_out);
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags_job);
		dprintk("No output buffers available\n");
		return;
	}
	spin_unlock_irqrestore(&m2m_ctx->cap_q_ctx.rdy_spinlock, flags_cap);
	spin_unlock_irqrestore(&m2m_ctx->out_q_ctx.rdy_spinlock, flags_out);

	if (m2m_dev->m2m_ops->job_ready
		&& (!m2m_dev->m2m_ops->job_ready(m2m_ctx->priv))) {
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags_job);
		dprintk("Driver not ready\n");
		return;
	}

	list_add_tail(&m2m_ctx->queue, &m2m_dev->job_queue);
	m2m_ctx->job_flags |= TRANS_QUEUED;

	spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags_job);
}

/**
 * v4l2_m2m_try_schedule() - schedule and possibly run a job for any context
 * @m2m_ctx: m2m context
 *
 * Check if this context is ready to queue a job. If suitable,
 * run the next queued job on the mem2mem device.
 *
 * This function shouldn't run in interrupt context.
 *
 * Note that v4l2_m2m_try_schedule() can schedule one job for this context,
 * and then run another job for another context.
 */
void v4l2_m2m_try_schedule(struct v4l2_m2m_ctx *m2m_ctx)
{
	struct v4l2_m2m_dev *m2m_dev = m2m_ctx->m2m_dev;

	__v4l2_m2m_try_queue(m2m_dev, m2m_ctx);
	v4l2_m2m_try_run(m2m_dev);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_try_schedule);

/**
 * v4l2_m2m_device_run_work() - run pending jobs for the context
 * @work: Work structure used for scheduling the execution of this function.
 */
static void v4l2_m2m_device_run_work(struct work_struct *work)
{
	struct v4l2_m2m_dev *m2m_dev =
		container_of(work, struct v4l2_m2m_dev, job_work);

	v4l2_m2m_try_run(m2m_dev);
}

/**
 * v4l2_m2m_cancel_job() - cancel pending jobs for the context
 * @m2m_ctx: m2m context with jobs to be canceled
 *
 * In case of streamoff or release called on any context,
 * 1] If the context is currently running, then abort job will be called
 * 2] If the context is queued, then the context will be removed from
 *    the job_queue
 */
static void v4l2_m2m_cancel_job(struct v4l2_m2m_ctx *m2m_ctx)
{
	struct v4l2_m2m_dev *m2m_dev;
	unsigned long flags;

	m2m_dev = m2m_ctx->m2m_dev;
	spin_lock_irqsave(&m2m_dev->job_spinlock, flags);

	m2m_ctx->job_flags |= TRANS_ABORT;
	if (m2m_ctx->job_flags & TRANS_RUNNING) {
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);
		if (m2m_dev->m2m_ops->job_abort)
			m2m_dev->m2m_ops->job_abort(m2m_ctx->priv);
		dprintk("m2m_ctx %p running, will wait to complete\n", m2m_ctx);
		wait_event(m2m_ctx->finished,
				!(m2m_ctx->job_flags & TRANS_RUNNING));
	} else if (m2m_ctx->job_flags & TRANS_QUEUED) {
		list_del(&m2m_ctx->queue);
		m2m_ctx->job_flags &= ~(TRANS_QUEUED | TRANS_RUNNING);
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);
		dprintk("m2m_ctx: %p had been on queue and was removed\n",
			m2m_ctx);
	} else {
		/* Do nothing, was not on queue/running */
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);
	}
}

void v4l2_m2m_job_finish(struct v4l2_m2m_dev *m2m_dev,
			 struct v4l2_m2m_ctx *m2m_ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&m2m_dev->job_spinlock, flags);
	if (!m2m_dev->curr_ctx || m2m_dev->curr_ctx != m2m_ctx) {
		spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);
		dprintk("Called by an instance not currently running\n");
		return;
	}

	list_del(&m2m_dev->curr_ctx->queue);
	m2m_dev->curr_ctx->job_flags &= ~(TRANS_QUEUED | TRANS_RUNNING);
	wake_up(&m2m_dev->curr_ctx->finished);
	m2m_dev->curr_ctx = NULL;

	spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags);

	/* This instance might have more buffers ready, but since we do not
	 * allow more than one job on the job_queue per instance, each has
	 * to be scheduled separately after the previous one finishes. */
	__v4l2_m2m_try_queue(m2m_dev, m2m_ctx);

	/* We might be running in atomic context,
	 * but the job must be run in non-atomic context.
	 */
	schedule_work(&m2m_dev->job_work);
}
EXPORT_SYMBOL(v4l2_m2m_job_finish);

int v4l2_m2m_reqbufs(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		     struct v4l2_requestbuffers *reqbufs)
{
	struct vb2_queue *vq;
	int ret;

	vq = v4l2_m2m_get_vq(m2m_ctx, reqbufs->type);
	ret = vb2_reqbufs(vq, reqbufs);
	/* If count == 0, then the owner has released all buffers and he
	   is no longer owner of the queue. Otherwise we have an owner. */
	if (ret == 0)
		vq->owner = reqbufs->count ? file->private_data : NULL;

	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_reqbufs);

static void v4l2_m2m_adjust_mem_offset(struct vb2_queue *vq,
				       struct v4l2_buffer *buf)
{
	/* Adjust MMAP memory offsets for the CAPTURE queue */
	if (buf->memory == V4L2_MEMORY_MMAP && !V4L2_TYPE_IS_OUTPUT(vq->type)) {
		if (V4L2_TYPE_IS_MULTIPLANAR(vq->type)) {
			unsigned int i;

			for (i = 0; i < buf->length; ++i)
				buf->m.planes[i].m.mem_offset
					+= DST_QUEUE_OFF_BASE;
		} else {
			buf->m.offset += DST_QUEUE_OFF_BASE;
		}
	}
}

int v4l2_m2m_querybuf(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		      struct v4l2_buffer *buf)
{
	struct vb2_queue *vq;
	int ret;

	vq = v4l2_m2m_get_vq(m2m_ctx, buf->type);
	ret = vb2_querybuf(vq, buf);
	if (ret)
		return ret;

	/* Adjust MMAP memory offsets for the CAPTURE queue */
	v4l2_m2m_adjust_mem_offset(vq, buf);

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_querybuf);

int v4l2_m2m_qbuf(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		  struct v4l2_buffer *buf)
{
	struct video_device *vdev = video_devdata(file);
	struct vb2_queue *vq;
	int ret;

	vq = v4l2_m2m_get_vq(m2m_ctx, buf->type);
	ret = vb2_qbuf(vq, vdev->v4l2_dev->mdev, buf);
	if (ret)
		return ret;

	/* Adjust MMAP memory offsets for the CAPTURE queue */
	v4l2_m2m_adjust_mem_offset(vq, buf);

	v4l2_m2m_try_schedule(m2m_ctx);

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_qbuf);

int v4l2_m2m_dqbuf(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		   struct v4l2_buffer *buf)
{
	struct vb2_queue *vq;
	int ret;

	vq = v4l2_m2m_get_vq(m2m_ctx, buf->type);
	ret = vb2_dqbuf(vq, buf, file->f_flags & O_NONBLOCK);
	if (ret)
		return ret;

	/* Adjust MMAP memory offsets for the CAPTURE queue */
	v4l2_m2m_adjust_mem_offset(vq, buf);

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_dqbuf);

int v4l2_m2m_prepare_buf(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
			 struct v4l2_buffer *buf)
{
	struct video_device *vdev = video_devdata(file);
	struct vb2_queue *vq;
	int ret;

	vq = v4l2_m2m_get_vq(m2m_ctx, buf->type);
	ret = vb2_prepare_buf(vq, vdev->v4l2_dev->mdev, buf);
	if (ret)
		return ret;

	/* Adjust MMAP memory offsets for the CAPTURE queue */
	v4l2_m2m_adjust_mem_offset(vq, buf);

	v4l2_m2m_try_schedule(m2m_ctx);

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_prepare_buf);

int v4l2_m2m_create_bufs(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
			 struct v4l2_create_buffers *create)
{
	struct vb2_queue *vq;

	vq = v4l2_m2m_get_vq(m2m_ctx, create->format.type);
	return vb2_create_bufs(vq, create);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_create_bufs);

int v4l2_m2m_expbuf(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		  struct v4l2_exportbuffer *eb)
{
	struct vb2_queue *vq;

	vq = v4l2_m2m_get_vq(m2m_ctx, eb->type);
	return vb2_expbuf(vq, eb);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_expbuf);

int v4l2_m2m_streamon(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		      enum v4l2_buf_type type)
{
	struct vb2_queue *vq;
	int ret;

	vq = v4l2_m2m_get_vq(m2m_ctx, type);
	ret = vb2_streamon(vq, type);
	if (!ret)
		v4l2_m2m_try_schedule(m2m_ctx);

	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_streamon);

int v4l2_m2m_streamoff(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
		       enum v4l2_buf_type type)
{
	struct v4l2_m2m_dev *m2m_dev;
	struct v4l2_m2m_queue_ctx *q_ctx;
	unsigned long flags_job, flags;
	int ret;

	/* wait until the current context is dequeued from job_queue */
	v4l2_m2m_cancel_job(m2m_ctx);

	q_ctx = get_queue_ctx(m2m_ctx, type);
	ret = vb2_streamoff(&q_ctx->q, type);
	if (ret)
		return ret;

	m2m_dev = m2m_ctx->m2m_dev;
	spin_lock_irqsave(&m2m_dev->job_spinlock, flags_job);
	/* We should not be scheduled anymore, since we're dropping a queue. */
	if (m2m_ctx->job_flags & TRANS_QUEUED)
		list_del(&m2m_ctx->queue);
	m2m_ctx->job_flags = 0;

	spin_lock_irqsave(&q_ctx->rdy_spinlock, flags);
	/* Drop queue, since streamoff returns device to the same state as after
	 * calling reqbufs. */
	INIT_LIST_HEAD(&q_ctx->rdy_queue);
	q_ctx->num_rdy = 0;
	spin_unlock_irqrestore(&q_ctx->rdy_spinlock, flags);

	if (m2m_dev->curr_ctx == m2m_ctx) {
		m2m_dev->curr_ctx = NULL;
		wake_up(&m2m_ctx->finished);
	}
	spin_unlock_irqrestore(&m2m_dev->job_spinlock, flags_job);

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_streamoff);

__poll_t v4l2_m2m_poll(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
			   struct poll_table_struct *wait)
{
	struct video_device *vfd = video_devdata(file);
	__poll_t req_events = poll_requested_events(wait);
	struct vb2_queue *src_q, *dst_q;
	struct vb2_buffer *src_vb = NULL, *dst_vb = NULL;
	__poll_t rc = 0;
	unsigned long flags;

	if (test_bit(V4L2_FL_USES_V4L2_FH, &vfd->flags)) {
		struct v4l2_fh *fh = file->private_data;

		if (v4l2_event_pending(fh))
			rc = EPOLLPRI;
		else if (req_events & EPOLLPRI)
			poll_wait(file, &fh->wait, wait);
		if (!(req_events & (EPOLLOUT | EPOLLWRNORM | EPOLLIN | EPOLLRDNORM)))
			return rc;
	}

	src_q = v4l2_m2m_get_src_vq(m2m_ctx);
	dst_q = v4l2_m2m_get_dst_vq(m2m_ctx);

	/*
	 * There has to be at least one buffer queued on each queued_list, which
	 * means either in driver already or waiting for driver to claim it
	 * and start processing.
	 */
	if ((!src_q->streaming || list_empty(&src_q->queued_list))
		&& (!dst_q->streaming || list_empty(&dst_q->queued_list))) {
		rc |= EPOLLERR;
		goto end;
	}

	spin_lock_irqsave(&src_q->done_lock, flags);
	if (list_empty(&src_q->done_list))
		poll_wait(file, &src_q->done_wq, wait);
	spin_unlock_irqrestore(&src_q->done_lock, flags);

	spin_lock_irqsave(&dst_q->done_lock, flags);
	if (list_empty(&dst_q->done_list)) {
		/*
		 * If the last buffer was dequeued from the capture queue,
		 * return immediately. DQBUF will return -EPIPE.
		 */
		if (dst_q->last_buffer_dequeued) {
			spin_unlock_irqrestore(&dst_q->done_lock, flags);
			return rc | EPOLLIN | EPOLLRDNORM;
		}

		poll_wait(file, &dst_q->done_wq, wait);
	}
	spin_unlock_irqrestore(&dst_q->done_lock, flags);

	spin_lock_irqsave(&src_q->done_lock, flags);
	if (!list_empty(&src_q->done_list))
		src_vb = list_first_entry(&src_q->done_list, struct vb2_buffer,
						done_entry);
	if (src_vb && (src_vb->state == VB2_BUF_STATE_DONE
			|| src_vb->state == VB2_BUF_STATE_ERROR))
		rc |= EPOLLOUT | EPOLLWRNORM;
	spin_unlock_irqrestore(&src_q->done_lock, flags);

	spin_lock_irqsave(&dst_q->done_lock, flags);
	if (!list_empty(&dst_q->done_list))
		dst_vb = list_first_entry(&dst_q->done_list, struct vb2_buffer,
						done_entry);
	if (dst_vb && (dst_vb->state == VB2_BUF_STATE_DONE
			|| dst_vb->state == VB2_BUF_STATE_ERROR))
		rc |= EPOLLIN | EPOLLRDNORM;
	spin_unlock_irqrestore(&dst_q->done_lock, flags);

end:
	return rc;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_poll);

int v4l2_m2m_mmap(struct file *file, struct v4l2_m2m_ctx *m2m_ctx,
			 struct vm_area_struct *vma)
{
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	struct vb2_queue *vq;

	if (offset < DST_QUEUE_OFF_BASE) {
		vq = v4l2_m2m_get_src_vq(m2m_ctx);
	} else {
		vq = v4l2_m2m_get_dst_vq(m2m_ctx);
		vma->vm_pgoff -= (DST_QUEUE_OFF_BASE >> PAGE_SHIFT);
	}

	return vb2_mmap(vq, vma);
}
EXPORT_SYMBOL(v4l2_m2m_mmap);

#if defined(CONFIG_MEDIA_CONTROLLER)
void v4l2_m2m_unregister_media_controller(struct v4l2_m2m_dev *m2m_dev)
{
	media_remove_intf_links(&m2m_dev->intf_devnode->intf);
	media_devnode_remove(m2m_dev->intf_devnode);

	media_entity_remove_links(m2m_dev->source);
	media_entity_remove_links(&m2m_dev->sink);
	media_entity_remove_links(&m2m_dev->proc);
	media_device_unregister_entity(m2m_dev->source);
	media_device_unregister_entity(&m2m_dev->sink);
	media_device_unregister_entity(&m2m_dev->proc);
	kfree(m2m_dev->source->name);
	kfree(m2m_dev->sink.name);
	kfree(m2m_dev->proc.name);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_unregister_media_controller);

static int v4l2_m2m_register_entity(struct media_device *mdev,
	struct v4l2_m2m_dev *m2m_dev, enum v4l2_m2m_entity_type type,
	struct video_device *vdev, int function)
{
	struct media_entity *entity;
	struct media_pad *pads;
	char *name;
	unsigned int len;
	int num_pads;
	int ret;

	switch (type) {
	case MEM2MEM_ENT_TYPE_SOURCE:
		entity = m2m_dev->source;
		pads = &m2m_dev->source_pad;
		pads[0].flags = MEDIA_PAD_FL_SOURCE;
		num_pads = 1;
		break;
	case MEM2MEM_ENT_TYPE_SINK:
		entity = &m2m_dev->sink;
		pads = &m2m_dev->sink_pad;
		pads[0].flags = MEDIA_PAD_FL_SINK;
		num_pads = 1;
		break;
	case MEM2MEM_ENT_TYPE_PROC:
		entity = &m2m_dev->proc;
		pads = m2m_dev->proc_pads;
		pads[0].flags = MEDIA_PAD_FL_SINK;
		pads[1].flags = MEDIA_PAD_FL_SOURCE;
		num_pads = 2;
		break;
	default:
		return -EINVAL;
	}

	entity->obj_type = MEDIA_ENTITY_TYPE_BASE;
	if (type != MEM2MEM_ENT_TYPE_PROC) {
		entity->info.dev.major = VIDEO_MAJOR;
		entity->info.dev.minor = vdev->minor;
	}
	len = strlen(vdev->name) + 2 + strlen(m2m_entity_name[type]);
	name = kmalloc(len, GFP_KERNEL);
	if (!name)
		return -ENOMEM;
	snprintf(name, len, "%s-%s", vdev->name, m2m_entity_name[type]);
	entity->name = name;
	entity->function = function;

	ret = media_entity_pads_init(entity, num_pads, pads);
	if (ret) {
		kfree(entity->name);
		entity->name = NULL;
		return ret;
	}
	ret = media_device_register_entity(mdev, entity);
	if (ret) {
		kfree(entity->name);
		entity->name = NULL;
		return ret;
	}

	return 0;
}

int v4l2_m2m_register_media_controller(struct v4l2_m2m_dev *m2m_dev,
		struct video_device *vdev, int function)
{
	struct media_device *mdev = vdev->v4l2_dev->mdev;
	struct media_link *link;
	int ret;

	if (!mdev)
		return 0;

	/* A memory-to-memory device consists in two
	 * DMA engine and one video processing entities.
	 * The DMA engine entities are linked to a V4L interface
	 */

	/* Create the three entities with their pads */
	m2m_dev->source = &vdev->entity;
	ret = v4l2_m2m_register_entity(mdev, m2m_dev,
			MEM2MEM_ENT_TYPE_SOURCE, vdev, MEDIA_ENT_F_IO_V4L);
	if (ret)
		return ret;
	ret = v4l2_m2m_register_entity(mdev, m2m_dev,
			MEM2MEM_ENT_TYPE_PROC, vdev, function);
	if (ret)
		goto err_rel_entity0;
	ret = v4l2_m2m_register_entity(mdev, m2m_dev,
			MEM2MEM_ENT_TYPE_SINK, vdev, MEDIA_ENT_F_IO_V4L);
	if (ret)
		goto err_rel_entity1;

	/* Connect the three entities */
	ret = media_create_pad_link(m2m_dev->source, 0, &m2m_dev->proc, 0,
			MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED);
	if (ret)
		goto err_rel_entity2;

	ret = media_create_pad_link(&m2m_dev->proc, 1, &m2m_dev->sink, 0,
			MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED);
	if (ret)
		goto err_rm_links0;

	/* Create video interface */
	m2m_dev->intf_devnode = media_devnode_create(mdev,
			MEDIA_INTF_T_V4L_VIDEO, 0,
			VIDEO_MAJOR, vdev->minor);
	if (!m2m_dev->intf_devnode) {
		ret = -ENOMEM;
		goto err_rm_links1;
	}

	/* Connect the two DMA engines to the interface */
	link = media_create_intf_link(m2m_dev->source,
			&m2m_dev->intf_devnode->intf,
			MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED);
	if (!link) {
		ret = -ENOMEM;
		goto err_rm_devnode;
	}

	link = media_create_intf_link(&m2m_dev->sink,
			&m2m_dev->intf_devnode->intf,
			MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED);
	if (!link) {
		ret = -ENOMEM;
		goto err_rm_intf_link;
	}
	return 0;

err_rm_intf_link:
	media_remove_intf_links(&m2m_dev->intf_devnode->intf);
err_rm_devnode:
	media_devnode_remove(m2m_dev->intf_devnode);
err_rm_links1:
	media_entity_remove_links(&m2m_dev->sink);
err_rm_links0:
	media_entity_remove_links(&m2m_dev->proc);
	media_entity_remove_links(m2m_dev->source);
err_rel_entity2:
	media_device_unregister_entity(&m2m_dev->proc);
	kfree(m2m_dev->proc.name);
err_rel_entity1:
	media_device_unregister_entity(&m2m_dev->sink);
	kfree(m2m_dev->sink.name);
err_rel_entity0:
	media_device_unregister_entity(m2m_dev->source);
	kfree(m2m_dev->source->name);
	return ret;
	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_register_media_controller);
#endif

struct v4l2_m2m_dev *v4l2_m2m_init(const struct v4l2_m2m_ops *m2m_ops)
{
	struct v4l2_m2m_dev *m2m_dev;

	if (!m2m_ops || WARN_ON(!m2m_ops->device_run))
		return ERR_PTR(-EINVAL);

	m2m_dev = kzalloc(sizeof *m2m_dev, GFP_KERNEL);
	if (!m2m_dev)
		return ERR_PTR(-ENOMEM);

	m2m_dev->curr_ctx = NULL;
	m2m_dev->m2m_ops = m2m_ops;
	INIT_LIST_HEAD(&m2m_dev->job_queue);
	spin_lock_init(&m2m_dev->job_spinlock);
	INIT_WORK(&m2m_dev->job_work, v4l2_m2m_device_run_work);

	return m2m_dev;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_init);

void v4l2_m2m_release(struct v4l2_m2m_dev *m2m_dev)
{
	kfree(m2m_dev);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_release);

struct v4l2_m2m_ctx *v4l2_m2m_ctx_init(struct v4l2_m2m_dev *m2m_dev,
		void *drv_priv,
		int (*queue_init)(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq))
{
	struct v4l2_m2m_ctx *m2m_ctx;
	struct v4l2_m2m_queue_ctx *out_q_ctx, *cap_q_ctx;
	int ret;

	m2m_ctx = kzalloc(sizeof *m2m_ctx, GFP_KERNEL);
	if (!m2m_ctx)
		return ERR_PTR(-ENOMEM);

	m2m_ctx->priv = drv_priv;
	m2m_ctx->m2m_dev = m2m_dev;
	init_waitqueue_head(&m2m_ctx->finished);

	out_q_ctx = &m2m_ctx->out_q_ctx;
	cap_q_ctx = &m2m_ctx->cap_q_ctx;

	INIT_LIST_HEAD(&out_q_ctx->rdy_queue);
	INIT_LIST_HEAD(&cap_q_ctx->rdy_queue);
	spin_lock_init(&out_q_ctx->rdy_spinlock);
	spin_lock_init(&cap_q_ctx->rdy_spinlock);

	INIT_LIST_HEAD(&m2m_ctx->queue);

	ret = queue_init(drv_priv, &out_q_ctx->q, &cap_q_ctx->q);

	if (ret)
		goto err;
	/*
	 * If both queues use same mutex assign it as the common buffer
	 * queues lock to the m2m context. This lock is used in the
	 * v4l2_m2m_ioctl_* helpers.
	 */
	if (out_q_ctx->q.lock == cap_q_ctx->q.lock)
		m2m_ctx->q_lock = out_q_ctx->q.lock;

	return m2m_ctx;
err:
	kfree(m2m_ctx);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_ctx_init);

void v4l2_m2m_ctx_release(struct v4l2_m2m_ctx *m2m_ctx)
{
	/* wait until the current context is dequeued from job_queue */
	v4l2_m2m_cancel_job(m2m_ctx);

	vb2_queue_release(&m2m_ctx->cap_q_ctx.q);
	vb2_queue_release(&m2m_ctx->out_q_ctx.q);

	kfree(m2m_ctx);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_ctx_release);

void v4l2_m2m_buf_queue(struct v4l2_m2m_ctx *m2m_ctx,
		struct vb2_v4l2_buffer *vbuf)
{
	struct v4l2_m2m_buffer *b = container_of(vbuf,
				struct v4l2_m2m_buffer, vb);
	struct v4l2_m2m_queue_ctx *q_ctx;
	unsigned long flags;

	q_ctx = get_queue_ctx(m2m_ctx, vbuf->vb2_buf.vb2_queue->type);
	if (!q_ctx)
		return;

	spin_lock_irqsave(&q_ctx->rdy_spinlock, flags);
	list_add_tail(&b->list, &q_ctx->rdy_queue);
	q_ctx->num_rdy++;
	spin_unlock_irqrestore(&q_ctx->rdy_spinlock, flags);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_buf_queue);

void v4l2_m2m_buf_copy_data(const struct vb2_v4l2_buffer *out_vb,
			    struct vb2_v4l2_buffer *cap_vb,
			    bool copy_frame_flags)
{
	u32 mask = V4L2_BUF_FLAG_TIMECODE | V4L2_BUF_FLAG_TSTAMP_SRC_MASK;

	if (copy_frame_flags)
		mask |= V4L2_BUF_FLAG_KEYFRAME | V4L2_BUF_FLAG_PFRAME |
			V4L2_BUF_FLAG_BFRAME;

	cap_vb->vb2_buf.timestamp = out_vb->vb2_buf.timestamp;

	if (out_vb->flags & V4L2_BUF_FLAG_TIMECODE)
		cap_vb->timecode = out_vb->timecode;
	cap_vb->field = out_vb->field;
	cap_vb->flags &= ~mask;
	cap_vb->flags |= out_vb->flags & mask;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_buf_copy_data);

void v4l2_m2m_request_queue(struct media_request *req)
{
	struct media_request_object *obj, *obj_safe;
	struct v4l2_m2m_ctx *m2m_ctx = NULL;

	/*
	 * Queue all objects. Note that buffer objects are at the end of the
	 * objects list, after all other object types. Once buffer objects
	 * are queued, the driver might delete them immediately (if the driver
	 * processes the buffer at once), so we have to use
	 * list_for_each_entry_safe() to handle the case where the object we
	 * queue is deleted.
	 */
	list_for_each_entry_safe(obj, obj_safe, &req->objects, list) {
		struct v4l2_m2m_ctx *m2m_ctx_obj;
		struct vb2_buffer *vb;

		if (!obj->ops->queue)
			continue;

		if (vb2_request_object_is_buffer(obj)) {
			/* Sanity checks */
			vb = container_of(obj, struct vb2_buffer, req_obj);
			WARN_ON(!V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type));
			m2m_ctx_obj = container_of(vb->vb2_queue,
						   struct v4l2_m2m_ctx,
						   out_q_ctx.q);
			WARN_ON(m2m_ctx && m2m_ctx_obj != m2m_ctx);
			m2m_ctx = m2m_ctx_obj;
		}

		/*
		 * The buffer we queue here can in theory be immediately
		 * unbound, hence the use of list_for_each_entry_safe()
		 * above and why we call the queue op last.
		 */
		obj->ops->queue(obj);
	}

	WARN_ON(!m2m_ctx);

	if (m2m_ctx)
		v4l2_m2m_try_schedule(m2m_ctx);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_request_queue);

/* Videobuf2 ioctl helpers */

int v4l2_m2m_ioctl_reqbufs(struct file *file, void *priv,
				struct v4l2_requestbuffers *rb)
{
	struct v4l2_fh *fh = file->private_data;

	return v4l2_m2m_reqbufs(file, fh->m2m_ctx, rb);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_ioctl_reqbufs);

int v4l2_m2m_ioctl_create_bufs(struct file *file, void *priv,
				struct v4l2_create_buffers *create)
{
	struct v4l2_fh *fh = file->private_data;

	return v4l2_m2m_create_bufs(file, fh->m2m_ctx, create);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_ioctl_create_bufs);

int v4l2_m2m_ioctl_querybuf(struct file *file, void *priv,
				struct v4l2_buffer *buf)
{
	struct v4l2_fh *fh = file->private_data;

	return v4l2_m2m_querybuf(file, fh->m2m_ctx, buf);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_ioctl_querybuf);

int v4l2_m2m_ioctl_qbuf(struct file *file, void *priv,
				struct v4l2_buffer *buf)
{
	struct v4l2_fh *fh = file->private_data;

	return v4l2_m2m_qbuf(file, fh->m2m_ctx, buf);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_ioctl_qbuf);

int v4l2_m2m_ioctl_dqbuf(struct file *file, void *priv,
				struct v4l2_buffer *buf)
{
	struct v4l2_fh *fh = file->private_data;

	return v4l2_m2m_dqbuf(file, fh->m2m_ctx, buf);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_ioctl_dqbuf);

int v4l2_m2m_ioctl_prepare_buf(struct file *file, void *priv,
			       struct v4l2_buffer *buf)
{
	struct v4l2_fh *fh = file->private_data;

	return v4l2_m2m_prepare_buf(file, fh->m2m_ctx, buf);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_ioctl_prepare_buf);

int v4l2_m2m_ioctl_expbuf(struct file *file, void *priv,
				struct v4l2_exportbuffer *eb)
{
	struct v4l2_fh *fh = file->private_data;

	return v4l2_m2m_expbuf(file, fh->m2m_ctx, eb);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_ioctl_expbuf);

int v4l2_m2m_ioctl_streamon(struct file *file, void *priv,
				enum v4l2_buf_type type)
{
	struct v4l2_fh *fh = file->private_data;

	return v4l2_m2m_streamon(file, fh->m2m_ctx, type);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_ioctl_streamon);

int v4l2_m2m_ioctl_streamoff(struct file *file, void *priv,
				enum v4l2_buf_type type)
{
	struct v4l2_fh *fh = file->private_data;

	return v4l2_m2m_streamoff(file, fh->m2m_ctx, type);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_ioctl_streamoff);

/*
 * v4l2_file_operations helpers. It is assumed here same lock is used
 * for the output and the capture buffer queue.
 */

int v4l2_m2m_fop_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct v4l2_fh *fh = file->private_data;

	return v4l2_m2m_mmap(file, fh->m2m_ctx, vma);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_fop_mmap);

__poll_t v4l2_m2m_fop_poll(struct file *file, poll_table *wait)
{
	struct v4l2_fh *fh = file->private_data;
	struct v4l2_m2m_ctx *m2m_ctx = fh->m2m_ctx;
	__poll_t ret;

	if (m2m_ctx->q_lock)
		mutex_lock(m2m_ctx->q_lock);

	ret = v4l2_m2m_poll(file, m2m_ctx, wait);

	if (m2m_ctx->q_lock)
		mutex_unlock(m2m_ctx->q_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_fop_poll);

