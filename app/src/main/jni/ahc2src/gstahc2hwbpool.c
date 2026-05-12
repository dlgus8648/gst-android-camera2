/*
 * Copyright (C) 2026, KIMRIHYEON <dlgus8648@naver.com>
 *
 * Inspired by gst-android-camera (ahcsrc) by Justin Kim, Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "gstahc2hwbpool.h"

GST_DEBUG_CATEGORY_STATIC (gst_ahc2_hwb_pool_debug);

#define GST_CAT_DEFAULT gst_ahc2_hwb_pool_debug

struct _GstAhc2HwbPool
{

  GstBufferPool parent;

  GstCaps      *caps;

  GstVideoInfo  vinfo;

  gboolean      add_video_meta;

  gboolean      configured;
};

G_DEFINE_TYPE (GstAhc2HwbPool, gst_ahc2_hwb_pool, GST_TYPE_BUFFER_POOL)

GstBufferPool *
gst_ahc2_hwb_pool_new (void)
{

  return g_object_new (GST_TYPE_AHC2_HWB_POOL, NULL);
}

static gboolean
gst_ahc2_hwb_pool_set_config (GstBufferPool * bpool, GstStructure * config)
{

  GstAhc2HwbPool *self = GST_AHC2_HWB_POOL (bpool);

  GstCaps *caps = NULL;

  guint    size = 0, min = 0, max = 0;

  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min, &max)) {
    GST_ERROR_OBJECT (self, "set_config: invalid params");
    return FALSE;
  }
  if (!caps) {
    GST_ERROR_OBJECT (self, "set_config: caps is NULL");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&self->vinfo, caps)) {
    GST_ERROR_OBJECT (self, "set_config: caps not video/x-raw");
    return FALSE;
  }

  if (size == 0)
    size = GST_VIDEO_INFO_SIZE (&self->vinfo);

  gst_caps_replace (&self->caps, caps);

  self->add_video_meta = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (max != 0 && min > max) min = max;

  GST_INFO_OBJECT (self, "set_config: %dx%d %s, size=%u, min=%u, max=%u, "
      "video_meta=%d",
      GST_VIDEO_INFO_WIDTH (&self->vinfo),
      GST_VIDEO_INFO_HEIGHT (&self->vinfo),
      GST_VIDEO_INFO_FORMAT (&self->vinfo) != GST_VIDEO_FORMAT_UNKNOWN
          ? gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&self->vinfo))
          : "?",
      size, min, max, self->add_video_meta);

  gst_buffer_pool_config_set_params (config, caps, size, min, max);

  self->configured = TRUE;

  return GST_BUFFER_POOL_CLASS (gst_ahc2_hwb_pool_parent_class)
      ->set_config (bpool, config);
}

static GstFlowReturn
gst_ahc2_hwb_pool_alloc_buffer (GstBufferPool * bpool, GstBuffer ** out,
    GstBufferPoolAcquireParams * params)
{
  GstAhc2HwbPool *self = GST_AHC2_HWB_POOL (bpool);

  GstBuffer *buf;

  buf = gst_buffer_new_allocate (NULL, GST_VIDEO_INFO_SIZE (&self->vinfo), NULL);
  if (!buf)
    return GST_FLOW_ERROR;

  if (self->add_video_meta) {

    gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (&self->vinfo),
        GST_VIDEO_INFO_WIDTH (&self->vinfo),
        GST_VIDEO_INFO_HEIGHT (&self->vinfo),
        GST_VIDEO_INFO_N_PLANES (&self->vinfo),
        self->vinfo.offset, self->vinfo.stride);
  }

  *out = buf;
  return GST_FLOW_OK;
}

static gboolean
gst_ahc2_hwb_pool_start (GstBufferPool * bpool)
{
  GST_DEBUG_OBJECT (bpool, "pool start");

  return GST_BUFFER_POOL_CLASS (gst_ahc2_hwb_pool_parent_class)->start (bpool);
}

static gboolean
gst_ahc2_hwb_pool_stop (GstBufferPool * bpool)
{
  GST_DEBUG_OBJECT (bpool, "pool stop");

  return GST_BUFFER_POOL_CLASS (gst_ahc2_hwb_pool_parent_class)->stop (bpool);
}

static void
gst_ahc2_hwb_pool_finalize (GObject * obj)
{
  GstAhc2HwbPool *self = GST_AHC2_HWB_POOL (obj);

  gst_caps_replace (&self->caps, NULL);

  G_OBJECT_CLASS (gst_ahc2_hwb_pool_parent_class)->finalize (obj);
}

static void
gst_ahc2_hwb_pool_class_init (GstAhc2HwbPoolClass * klass)
{

  GObjectClass        *gobj = G_OBJECT_CLASS (klass);

  GstBufferPoolClass  *bp   = GST_BUFFER_POOL_CLASS (klass);

  gobj->finalize    = gst_ahc2_hwb_pool_finalize;

  bp->set_config   = gst_ahc2_hwb_pool_set_config;

  bp->start        = gst_ahc2_hwb_pool_start;

  bp->stop         = gst_ahc2_hwb_pool_stop;

  bp->alloc_buffer = gst_ahc2_hwb_pool_alloc_buffer;

  GST_DEBUG_CATEGORY_INIT (gst_ahc2_hwb_pool_debug, "ahc2hwbpool", 0,
      "ahc2src buffer pool");
}

static void
gst_ahc2_hwb_pool_init (GstAhc2HwbPool * self)
{

  self->add_video_meta = FALSE;

  self->configured     = FALSE;

  gst_video_info_init (&self->vinfo);
}
