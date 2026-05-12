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

#include "gstahc2src.h"

#include <string.h>

#include <camera/NdkCameraManager.h>

#include <camera/NdkCameraDevice.h>

#include <camera/NdkCameraError.h>

#include <camera/NdkCameraMetadata.h>

#include <camera/NdkCameraMetadataTags.h>

#include <camera/NdkCameraCaptureSession.h>

#include <camera/NdkCaptureRequest.h>

#include <media/NdkImageReader.h>

#include <gst/base/gstdataqueue.h>

#include "gstahc2hwbpool.h"

#include "gstahc2hwbmemory.h"

#include "gstahc2protocol.h"

#include <gst/gl/gl.h>

#include <gst/gl/gstglutils.h>

#ifndef EGL_EGLEXT_PROTOTYPES
#define EGL_EGLEXT_PROTOTYPES 1
#endif

#include <EGL/egl.h>

#include <EGL/eglext.h>

#include <gst/gl/egl/gsteglimage.h>

#include <gst/gl/egl/gstglmemoryegl.h>

typedef struct {

  AHardwareBuffer *ahb;

  GstGLContext    *ctx;

  EGLImageKHR      out_image;

  EGLDisplay       out_display;
} Ahc2EglCreateCtx;

GST_DEBUG_CATEGORY_STATIC (gst_ahc2_src_debug);

#define GST_CAT_DEFAULT gst_ahc2_src_debug

struct _GstAhc2Src
{

  GstPushSrc parent;

  guint64    frame_count;

  ACameraManager *mgr;

  char           *camera_id;

  ACameraDevice  *device;

  AImageReader                   *reader;
  ANativeWindow                  *reader_window;

  ACaptureSessionOutput          *output;

  ACaptureSessionOutputContainer *outputs;
  ACameraOutputTarget            *output_target;

  ACameraCaptureSession          *session;

  ACaptureRequest                *request;

  ACameraDevice_StateCallbacks         dev_cb;

  ACameraCaptureSession_stateCallbacks sess_cb;

  AImageReader_ImageListener           img_listener;

  gint width, height;

  gint fps_n, fps_d;

  volatile gint cb_count;

  GstDataQueue *queue;

  gint flushing;

  GstBufferPool *pool;
  volatile gint  acq_count;

  GstAllocator *hwb_allocator;
  volatile gint hwb_mem_count;

  GstGLDisplay *gl_display;

  GstGLContext *gl_context;

  GstGLContext *gl_other_context;

  gboolean use_gl_memory;

  ANativeWindow *bypass_window;

  gboolean use_surface_bypass;

  guint64  bypass_frame_count;

  GType    bypass_meta_api_type;
};

G_DEFINE_TYPE (GstAhc2Src, gst_ahc2_src, GST_TYPE_PUSH_SRC)

#define AHC2SRC_RAW_CAPS \
    "video/x-raw, "                                       \
    "format = (string) { NV12, NV21, I420 }, "            \
    "width  = (int)    [ 16, 4096 ], "                    \
    "height = (int)    [ 16, 4096 ], "                    \
    "framerate = (fraction) [ 0/1, 240/1 ]"

#define AHC2SRC_GL_CAPS \
    "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GL_MEMORY "), " \
    "format = (string) { RGBA, NV12 }, "                  \
    "width  = (int)    [ 16, 4096 ], "                    \
    "height = (int)    [ 16, 4096 ], "                    \
    "framerate = (fraction) [ 0/1, 240/1 ], "             \
    "texture-target = (string) { 2D, external-oes }"

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE (
    "src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (AHC2SRC_GL_CAPS ";" AHC2SRC_RAW_CAPS));

static char *
gst_ahc2_src_pick_back_camera (GstAhc2Src * self, ACameraManager * mgr)
{
  ACameraIdList *list = NULL;
  camera_status_t cs;

  cs = ACameraManager_getCameraIdList (mgr, &list);
  if (cs != ACAMERA_OK || !list) {
    GST_ERROR_OBJECT (self, "getCameraIdList failed cs=%d", cs);
    if (list) ACameraManager_deleteCameraIdList (list);
    return NULL;
  }
  if (list->numCameras < 1) {
    GST_ERROR_OBJECT (self, "no cameras on device");
    ACameraManager_deleteCameraIdList (list);
    return NULL;
  }

  char *picked = NULL;
  for (int i = 0; i < list->numCameras; i++) {
    ACameraMetadata *meta = NULL;
    if (ACameraManager_getCameraCharacteristics (mgr, list->cameraIds[i], &meta)
            != ACAMERA_OK)
      continue;

    ACameraMetadata_const_entry e;
    if (ACameraMetadata_getConstEntry (meta, ACAMERA_LENS_FACING, &e) == ACAMERA_OK
        && e.count == 1 && e.data.u8[0] == ACAMERA_LENS_FACING_BACK) {
      picked = g_strdup (list->cameraIds[i]);
    }
    ACameraMetadata_free (meta);
    if (picked) break;
  }
  if (!picked) {

    picked = g_strdup (list->cameraIds[0]);
    GST_INFO_OBJECT (self, "no BACK camera; falling back to id=%s", picked);
  }
  ACameraManager_deleteCameraIdList (list);
  return picked;
}

static void
gst_ahc2_src_on_device_disconnected (void * ctx, ACameraDevice * dev)
{

  GstAhc2Src *self = GST_AHC2_SRC (ctx);
  GST_WARNING_OBJECT (self, "camera disconnected (device=%p)", dev);
}

static void
gst_ahc2_src_on_device_error (void * ctx, ACameraDevice * dev, int err)
{
  GstAhc2Src *self = GST_AHC2_SRC (ctx);

  GST_ERROR_OBJECT (self, "camera error code=%d (device=%p)", err, dev);
}

static void
gst_ahc2_src_on_session_active (void * ctx, ACameraCaptureSession * sess)
{
  GstAhc2Src *self = GST_AHC2_SRC (ctx);
  GST_INFO_OBJECT (self, "session active (sess=%p)", sess);
}

static void
gst_ahc2_src_on_session_ready (void * ctx, ACameraCaptureSession * sess)
{
  GstAhc2Src *self = GST_AHC2_SRC (ctx);

  GST_INFO_OBJECT (self, "session ready (sess=%p)", sess);
}

static void
gst_ahc2_src_on_session_closed (void * ctx, ACameraCaptureSession * sess)
{
  GstAhc2Src *self = GST_AHC2_SRC (ctx);
  GST_INFO_OBJECT (self, "session closed (sess=%p)", sess);
}

typedef struct
{
  GstDataQueueItem item;

  AImage *img;
} Ahc2QueueItem;

static void
ahc2_queue_item_destroy (gpointer data)
{
  Ahc2QueueItem *qi = (Ahc2QueueItem *) data;
  if (!qi) return;
  if (qi->img) {
    AImage_delete (qi->img);
    qi->img = NULL;
  }
  g_free (qi);
}

static GstBuffer *
ahc2_aimage_to_buffer_m1 (GstAhc2Src * self, AImage * img)
{

  uint8_t *yp = NULL, *up = NULL, *vp = NULL;

  int      ylen = 0, ulen = 0, vlen = 0;

  int32_t  yRow = 0, uRow = 0, vRow = 0;

  int32_t  uPx  = 0;

  int64_t  ts_ns = 0;

  if (AImage_getPlaneData (img, 0, &yp, &ylen) != AMEDIA_OK ||
      AImage_getPlaneData (img, 1, &up, &ulen) != AMEDIA_OK ||
      AImage_getPlaneData (img, 2, &vp, &vlen) != AMEDIA_OK) {
    GST_ERROR_OBJECT (self, "AImage_getPlaneData failed");
    return NULL;
  }
  if (AImage_getPlaneRowStride (img, 0, &yRow) != AMEDIA_OK ||
      AImage_getPlaneRowStride (img, 1, &uRow) != AMEDIA_OK ||
      AImage_getPlaneRowStride (img, 2, &vRow) != AMEDIA_OK ||
      AImage_getPlanePixelStride (img, 1, &uPx) != AMEDIA_OK) {
    GST_ERROR_OBJECT (self, "AImage_getPlaneRow/PixelStride failed");
    return NULL;
  }

  AImage_getTimestamp (img, &ts_ns);

  const gint W = self->width;
  const gint H = self->height;

  GstVideoInfo vinfo;
  gst_video_info_set_format (&vinfo, GST_VIDEO_FORMAT_NV12, W, H);

  GstBuffer *buf = NULL;
  if (self->pool && gst_buffer_pool_is_active (self->pool)) {
    GstFlowReturn r = gst_buffer_pool_acquire_buffer (self->pool, &buf, NULL);
    if (r != GST_FLOW_OK || !buf) {
      GST_WARNING_OBJECT (self, "pool_acquire_buffer ret=%s — fallback to alloc",
          gst_flow_get_name (r));
      buf = NULL;
    } else {
      gint n = g_atomic_int_add (&self->acq_count, 1);
      if ((n % 30) == 0)
        GST_INFO_OBJECT (self, "pool acquire count=%d", n);
    }
  }
  if (!buf) {
    buf = gst_buffer_new_allocate (NULL, GST_VIDEO_INFO_SIZE (&vinfo), NULL);
    if (!buf) return NULL;
    gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (&vinfo), W, H,
        GST_VIDEO_INFO_N_PLANES (&vinfo), vinfo.offset, vinfo.stride);
  }

  GstMapInfo m;
  if (!gst_buffer_map (buf, &m, GST_MAP_WRITE)) {
    gst_buffer_unref (buf);
    return NULL;
  }

  for (gint y = 0; y < H; y++)
    memcpy (m.data + y * vinfo.stride[0],
            yp + (gsize) y * yRow,
            (gsize) W);

  guint8 *dst_uv = m.data + vinfo.offset[1];
  if (uPx == 2) {

    guint8 *src = (up < vp) ? up : vp;
    for (gint y = 0; y < H / 2; y++)
      memcpy (dst_uv + y * vinfo.stride[1],
              src + (gsize) y * uRow,
              (gsize) W);
  } else {

    for (gint y = 0; y < H / 2; y++) {
      guint8 *dst = dst_uv + y * vinfo.stride[1];
      const guint8 *u = up + (gsize) y * uRow;
      const guint8 *v = vp + (gsize) y * vRow;
      for (gint x = 0; x < W / 2; x++) {
        dst[2 * x + 0] = u[x];
        dst[2 * x + 1] = v[x];
      }
    }
  }

  gst_buffer_unmap (buf, &m);

  return buf;
}

static void
ahc2_egl_create_image_on_gl_thread (GstGLContext * ctx, Ahc2EglCreateCtx * c)
{
  EGLDisplay display = eglGetCurrentDisplay ();
  if (display == EGL_NO_DISPLAY) {
    GST_ERROR ("ahc2hwbmem: eglGetCurrentDisplay returned NO_DISPLAY");
    c->out_image = EGL_NO_IMAGE_KHR;
    return;
  }

  EGLClientBuffer egl_buf = eglGetNativeClientBufferANDROID (c->ahb);
  if (!egl_buf) {
    GST_ERROR ("ahc2hwbmem: eglGetNativeClientBufferANDROID failed (err=0x%x)",
        eglGetError ());
    c->out_image = EGL_NO_IMAGE_KHR;
    return;
  }

  EGLint attribs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
  EGLImageKHR img = eglCreateImageKHR (display, EGL_NO_CONTEXT,
      EGL_NATIVE_BUFFER_ANDROID, egl_buf, attribs);
  if (img == EGL_NO_IMAGE_KHR) {
    GST_ERROR ("ahc2hwbmem: eglCreateImageKHR failed (err=0x%x)",
        eglGetError ());
    c->out_image = EGL_NO_IMAGE_KHR;
    return;
  }

  c->out_image   = img;
  c->out_display = display;
}

static void
ahc2_destroy_egl_image_cb (GstEGLImage * eimg, gpointer user_data)
{
  EGLDisplay display = (EGLDisplay) user_data;
  EGLImageKHR raw_img = (EGLImageKHR) gst_egl_image_get_image (eimg);
  if (display && raw_img != EGL_NO_IMAGE_KHR) {
    eglDestroyImageKHR (display, raw_img);
  }
}

static GstBuffer *
ahc2_aimage_to_gl_buffer (GstAhc2Src * self, AImage * aimg)
{
  if (!self->gl_context) {
    GST_DEBUG_OBJECT (self, "no gl_context yet — skip GL path");
    return NULL;
  }

  AHardwareBuffer *ahb = NULL;
  if (AImage_getHardwareBuffer (aimg, &ahb) != AMEDIA_OK || !ahb) {
    GST_WARNING_OBJECT (self, "AImage_getHardwareBuffer failed (GL path)");
    return NULL;
  }
  AHardwareBuffer_acquire (ahb);

  Ahc2EglCreateCtx cctx = {
    .ahb = ahb, .ctx = self->gl_context,
    .out_image = EGL_NO_IMAGE_KHR, .out_display = EGL_NO_DISPLAY,
  };
  gst_gl_context_thread_add (self->gl_context,
      (GstGLContextThreadFunc) ahc2_egl_create_image_on_gl_thread, &cctx);

  if (cctx.out_image == EGL_NO_IMAGE_KHR) {
    AHardwareBuffer_release (ahb);
    return NULL;
  }

  GstEGLImage *eimg = gst_egl_image_new_wrapped (self->gl_context,
      cctx.out_image, GST_GL_RGBA,
      cctx.out_display,
      (GstEGLImageDestroyNotify) ahc2_destroy_egl_image_cb);
  if (!eimg) {
    GST_WARNING_OBJECT (self, "gst_egl_image_new_wrapped failed");
    eglDestroyImageKHR (cctx.out_display, cctx.out_image);
    AHardwareBuffer_release (ahb);
    return NULL;
  }

  GstVideoInfo vinfo;
  gst_video_info_set_format (&vinfo, GST_VIDEO_FORMAT_RGBA,
      self->width, self->height);

  GstGLBaseMemoryAllocator *gl_alloc = (GstGLBaseMemoryAllocator *)
      gst_allocator_find (GST_GL_MEMORY_EGL_ALLOCATOR_NAME);
  if (!gl_alloc) {

    GST_WARNING_OBJECT (self, "GstGLMemoryEGLAllocator not registered");
    gst_egl_image_unref (eimg);
    AHardwareBuffer_release (ahb);
    return NULL;
  }

  GstGLVideoAllocationParams *params =
      gst_gl_video_allocation_params_new_wrapped_gl_handle (
          self->gl_context, NULL, &vinfo,  0, NULL,
          GST_GL_TEXTURE_TARGET_EXTERNAL_OES, GST_GL_RGBA,
          eimg, eimg,
           (GDestroyNotify) gst_egl_image_unref);

  GstGLMemory *gl_mem = (GstGLMemory *) gst_gl_base_memory_alloc (gl_alloc,
      (GstGLAllocationParams *) params);
  gst_gl_allocation_params_free ((GstGLAllocationParams *) params);
  gst_object_unref (gl_alloc);

  if (!gl_mem) {
    GST_WARNING_OBJECT (self, "gst_gl_base_memory_alloc failed for EGL mem");
    gst_egl_image_unref (eimg);
    AHardwareBuffer_release (ahb);
    return NULL;
  }

  GstBuffer *buf = gst_buffer_new ();
  gst_buffer_append_memory (buf, GST_MEMORY_CAST (gl_mem));

  GstAhc2HwbMemory *hwb_mem = gst_ahc2_hwb_memory_new_from_aimage (
      self->hwb_allocator, aimg,  1);
  if (hwb_mem)
    gst_buffer_append_memory (buf, GST_MEMORY_CAST (hwb_mem));

  AHardwareBuffer_release (ahb);

  gst_buffer_add_video_meta (buf, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_FORMAT_RGBA, self->width, self->height);

  return buf;
}

static void
gst_ahc2_src_on_image_available (void * ctx, AImageReader * r)
{
  GstAhc2Src *self = GST_AHC2_SRC (ctx);
  AImage *img = NULL;
  media_status_t ms;

  ms = AImageReader_acquireLatestImage (r, &img);
  if (ms != AMEDIA_OK || !img) {
    if (img) AImage_delete (img);
    return;
  }

  if (g_atomic_int_get (&self->flushing)) {
    AImage_delete (img);
    return;
  }

  Ahc2QueueItem *qi = g_new0 (Ahc2QueueItem, 1);
  qi->img = img;
  qi->item.object = NULL;
  qi->item.size    = 1;
  qi->item.visible = TRUE;
  qi->item.duration = GST_CLOCK_TIME_NONE;
  qi->item.destroy = ahc2_queue_item_destroy;

  if (!gst_data_queue_push (self->queue, &qi->item)) {

    ahc2_queue_item_destroy (qi);
    return;
  }

  gint n = g_atomic_int_add (&self->cb_count, 1);
  if ((n % 30) == 0)
    GST_INFO_OBJECT (self, "OnImageAvailable count=%d (queued)", n);
}

static gboolean
gst_ahc2_src_start (GstBaseSrc * bsrc)
{
  GstAhc2Src *self = GST_AHC2_SRC (bsrc);
  camera_status_t cs;

  self->frame_count = 0;
  GST_INFO_OBJECT (self, "ahc2src start()");

  self->mgr = ACameraManager_create ();
  if (!self->mgr) {
    GST_ERROR_OBJECT (self, "ACameraManager_create failed");
    return FALSE;
  }

  self->camera_id = gst_ahc2_src_pick_back_camera (self, self->mgr);
  if (!self->camera_id) goto fail;

  self->dev_cb = (ACameraDevice_StateCallbacks) {
    .context = self,
    .onDisconnected = gst_ahc2_src_on_device_disconnected,
    .onError = gst_ahc2_src_on_device_error,
  };

  cs = ACameraManager_openCamera (self->mgr, self->camera_id, &self->dev_cb,
      &self->device);
  if (cs != ACAMERA_OK) {
    GST_ERROR_OBJECT (self,
        "openCamera failed cs=%d for id=%s "
        "(IN_USE=%d, MAX_IN_USE=%d, DISABLED=%d, DEVICE=%d, SERVICE=%d)",
        cs, self->camera_id,
        ACAMERA_ERROR_CAMERA_IN_USE, ACAMERA_ERROR_MAX_CAMERA_IN_USE,
        ACAMERA_ERROR_CAMERA_DISABLED, ACAMERA_ERROR_CAMERA_DEVICE,
        ACAMERA_ERROR_CAMERA_SERVICE);
    goto fail;
  }

  GST_INFO_OBJECT (self, "Opened camera id=%s", self->camera_id);
  return TRUE;

fail:

  if (self->device)    { ACameraDevice_close (self->device); self->device = NULL; }
  g_clear_pointer (&self->camera_id, g_free);
  if (self->mgr)       { ACameraManager_delete (self->mgr); self->mgr = NULL; }
  return FALSE;
}

static void
gst_ahc2_src_cleanup_pool (GstAhc2Src * self)
{
  if (self->pool) {
    gst_buffer_pool_set_active (self->pool, FALSE);
    gst_object_unref (self->pool);
    self->pool = NULL;
  }
  g_atomic_int_set (&self->acq_count, 0);
}

static void
gst_ahc2_src_cleanup_capture (GstAhc2Src * self)
{
  if (self->session) {
    ACameraCaptureSession_stopRepeating (self->session);
    ACameraCaptureSession_close (self->session);
    self->session = NULL;
  }
  if (self->queue) {
    gst_data_queue_set_flushing (self->queue, TRUE);
    gst_data_queue_flush (self->queue);
    gst_data_queue_set_flushing (self->queue, FALSE);
  }
  if (self->request) { ACaptureRequest_free (self->request); self->request = NULL; }
  if (self->output_target) {
    ACameraOutputTarget_free (self->output_target);
    self->output_target = NULL;
  }
  if (self->outputs) { ACaptureSessionOutputContainer_free (self->outputs); self->outputs = NULL; }
  if (self->output)  { ACaptureSessionOutput_free (self->output); self->output = NULL; }
  if (self->reader)  { AImageReader_delete (self->reader); self->reader = NULL; }
  self->reader_window = NULL;

  self->bypass_frame_count = 0;

}

static GstCaps *
gst_ahc2_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstAhc2Src *self = GST_AHC2_SRC (bsrc);

  if (!self->mgr || !self->camera_id) {
    GstCaps *t = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (bsrc));
    if (filter) {
      GstCaps *r = gst_caps_intersect_full (filter, t, GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref (t);
      return r;
    }
    return t;
  }

  ACameraMetadata *meta = NULL;
  if (ACameraManager_getCameraCharacteristics (self->mgr, self->camera_id, &meta)
          != ACAMERA_OK || !meta) {
    GST_WARNING_OBJECT (self, "getCameraCharacteristics failed; using template caps");
    GstCaps *t = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (bsrc));
    if (filter) {
      GstCaps *r = gst_caps_intersect_full (filter, t, GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref (t);
      return r;
    }
    return t;
  }

  GstCaps *caps = gst_caps_new_empty ();

  ACameraMetadata_const_entry e;
  if (ACameraMetadata_getConstEntry (meta,
          ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &e) == ACAMERA_OK
      && e.data.i32 != NULL) {

    for (uint32_t i = 0; i + 3 < e.count; i += 4) {
      int32_t fmt = e.data.i32[i];
      int32_t w   = e.data.i32[i + 1];
      int32_t h   = e.data.i32[i + 2];
      int32_t in  = e.data.i32[i + 3];

      if (in != 0) continue;
      if (fmt != AIMAGE_FORMAT_YUV_420_888) continue;
      if ((w & 1) || (h & 1)) continue;
      if (w < 16 || h < 16 || w > 4096 || h > 4096) continue;

      if (w > 1920 || h > 1920) continue;

      if (self->use_surface_bypass) {

        GstStructure *bypass_struct = gst_structure_new ("video/x-raw",
                "format",    G_TYPE_STRING,      "NV12",
                "width",     G_TYPE_INT,         w,
                "height",    G_TYPE_INT,         h,
                "framerate", GST_TYPE_FRACTION,  30, 1,
                NULL);
        GstCaps *bypass_caps = gst_caps_new_full (bypass_struct, NULL);
        GstCapsFeatures *bypass_feat = gst_caps_features_new_static_str (
            GST_AHC2_CAPS_FEATURE_SURFACE_BYPASS, NULL);
        gst_caps_set_features (bypass_caps, 0, bypass_feat);
        gst_caps_append (caps, bypass_caps);
        continue;
      }

      if (self->gl_display) {
        GstStructure *gl_struct = gst_structure_new ("video/x-raw",
                "format",         G_TYPE_STRING,         "RGBA",
                "width",          G_TYPE_INT,            w,
                "height",         G_TYPE_INT,            h,
                "framerate",      GST_TYPE_FRACTION,     30, 1,
                "texture-target", G_TYPE_STRING,         "external-oes",
                NULL);
        GstCaps *gl_caps = gst_caps_new_full (gl_struct, NULL);
        GstCapsFeatures *gl_feat = gst_caps_features_new_static_str (
            GST_CAPS_FEATURE_MEMORY_GL_MEMORY, NULL);
        gst_caps_set_features (gl_caps, 0, gl_feat);
        gst_caps_append (caps, gl_caps);
      }

      gst_caps_append (caps, gst_caps_new_simple ("video/x-raw",
              "format",    G_TYPE_STRING,         "NV12",
              "width",     G_TYPE_INT,            w,
              "height",    G_TYPE_INT,            h,
              "framerate", GST_TYPE_FRACTION,     30, 1,
              NULL));
    }
  }
  ACameraMetadata_free (meta);

  if (gst_caps_is_empty (caps)) {
    GST_WARNING_OBJECT (self, "no YUV_420_888 stream config; falling back to template");
    gst_caps_unref (caps);
    caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (bsrc));
  } else {
    GST_DEBUG_OBJECT (self, "advertising %u stream configurations",
        gst_caps_get_size (caps));
  }

  if (filter) {
    GstCaps *r = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    return r;
  }
  return caps;
}

static gboolean
gst_ahc2_src_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstAhc2Src *self = GST_AHC2_SRC (bsrc);
  GstVideoInfo info;
  camera_status_t cs;
  media_status_t  ms;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "set_caps: gst_video_info_from_caps failed");
    return FALSE;
  }
  self->width  = GST_VIDEO_INFO_WIDTH (&info);
  self->height = GST_VIDEO_INFO_HEIGHT (&info);
  self->fps_n  = GST_VIDEO_INFO_FPS_N (&info);
  self->fps_d  = GST_VIDEO_INFO_FPS_D (&info);

  if ((self->width & 1) || (self->height & 1)) {
    GST_ERROR_OBJECT (self, "set_caps: odd size %dx%d not allowed for YUV420",
        self->width, self->height);
    return FALSE;
  }

  GstCapsFeatures *feat = gst_caps_get_features (caps, 0);
  self->use_gl_memory = (feat != NULL &&
      gst_caps_features_contains (feat, GST_CAPS_FEATURE_MEMORY_GL_MEMORY));
  GST_INFO_OBJECT (self, "set_caps: %dx%d@%d/%d, use_gl_memory=%s",
      self->width, self->height, self->fps_n, self->fps_d,
      self->use_gl_memory ? "TRUE" : "FALSE");

  if (self->use_gl_memory && self->gl_display && !self->gl_context) {
    GError *err = NULL;
    self->gl_context = gst_gl_context_new (self->gl_display);
    if (!gst_gl_context_create (self->gl_context, self->gl_other_context, &err)) {
      GST_WARNING_OBJECT (self, "gst_gl_context_create failed: %s",
          err ? err->message : "unknown");
      g_clear_error (&err);
      gst_clear_object (&self->gl_context);
    } else {
      GST_INFO_OBJECT (self, "created own GstGLContext=%p (shared with %p)",
          self->gl_context, self->gl_other_context);
    }
  }

  gst_ahc2_src_cleanup_capture (self);

  gst_ahc2_src_cleanup_pool (self);

  if (self->use_surface_bypass && self->bypass_window) {
    GST_INFO_OBJECT (self,
        "Mode A: skipping AImageReader; using bypass_window=%p as direct "
        "camera output target (zero-copy)", self->bypass_window);
    self->reader        = NULL;
    self->reader_window = self->bypass_window;

  } else {

    const uint64_t reader_usage =
        AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
        AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN;
    ms = AImageReader_newWithUsage (self->width, self->height,
        AIMAGE_FORMAT_YUV_420_888, reader_usage,
         8, &self->reader);
    if (ms != AMEDIA_OK) {
      GST_ERROR_OBJECT (self, "AImageReader_newWithUsage failed ms=%d", ms);
      goto fail;
    }
    ms = AImageReader_getWindow (self->reader, &self->reader_window);
    if (ms != AMEDIA_OK) {
      GST_ERROR_OBJECT (self, "AImageReader_getWindow failed ms=%d", ms);
      goto fail;
    }

    self->img_listener = (AImageReader_ImageListener) {
      .context = self,
      .onImageAvailable = gst_ahc2_src_on_image_available,
    };
    ms = AImageReader_setImageListener (self->reader, &self->img_listener);
    if (ms != AMEDIA_OK) {
      GST_ERROR_OBJECT (self, "AImageReader_setImageListener failed ms=%d", ms);
      goto fail;
    }
  }

  cs = ACaptureSessionOutput_create (self->reader_window, &self->output);
  if (cs != ACAMERA_OK) {
    GST_ERROR_OBJECT (self, "ACaptureSessionOutput_create cs=%d", cs);
    goto fail;
  }
  cs = ACaptureSessionOutputContainer_create (&self->outputs);
  if (cs != ACAMERA_OK) {
    GST_ERROR_OBJECT (self, "ACaptureSessionOutputContainer_create cs=%d", cs);
    goto fail;
  }
  cs = ACaptureSessionOutputContainer_add (self->outputs, self->output);
  if (cs != ACAMERA_OK) {
    GST_ERROR_OBJECT (self, "ACaptureSessionOutputContainer_add cs=%d", cs);
    goto fail;
  }

  self->sess_cb = (ACameraCaptureSession_stateCallbacks) {
    .context  = self,
    .onClosed = gst_ahc2_src_on_session_closed,
    .onReady  = gst_ahc2_src_on_session_ready,
    .onActive = gst_ahc2_src_on_session_active,
  };

  cs = ACameraDevice_createCaptureSession (self->device, self->outputs,
      &self->sess_cb, &self->session);
  if (cs != ACAMERA_OK) {
    GST_ERROR_OBJECT (self, "createCaptureSession FAILED cs=%d", cs);
    goto fail;
  }

  cs = ACameraDevice_createCaptureRequest (self->device, TEMPLATE_PREVIEW,
      &self->request);
  if (cs != ACAMERA_OK) {
    GST_ERROR_OBJECT (self, "createCaptureRequest cs=%d", cs);
    goto fail;
  }

  cs = ACameraOutputTarget_create (self->reader_window, &self->output_target);
  if (cs != ACAMERA_OK) {
    GST_ERROR_OBJECT (self, "ACameraOutputTarget_create cs=%d", cs);
    goto fail;
  }
  cs = ACaptureRequest_addTarget (self->request, self->output_target);
  if (cs != ACAMERA_OK) {
    GST_ERROR_OBJECT (self, "ACaptureRequest_addTarget cs=%d", cs);
    goto fail;
  }

  ACaptureRequest *reqs[1] = { self->request };
  cs = ACameraCaptureSession_setRepeatingRequest (self->session,
       NULL,  1, reqs,  NULL);
  if (cs != ACAMERA_OK) {
    GST_ERROR_OBJECT (self, "setRepeatingRequest cs=%d", cs);
    goto fail;
  }

  GST_INFO_OBJECT (self, "set_caps complete (capture session ready)");
  return TRUE;

fail:
  gst_ahc2_src_cleanup_capture (self);
  return FALSE;
}

static gboolean
gst_ahc2_src_stop (GstBaseSrc * bsrc)
{
  GstAhc2Src *self = GST_AHC2_SRC (bsrc);

  gst_ahc2_src_cleanup_capture (self);

  gst_ahc2_src_cleanup_pool (self);

  gst_clear_object (&self->gl_context);
  gst_clear_object (&self->gl_other_context);
  gst_clear_object (&self->gl_display);
  self->use_gl_memory = FALSE;

  if (self->device) {
    ACameraDevice_close (self->device);
    self->device = NULL;
  }
  g_clear_pointer (&self->camera_id, g_free);
  if (self->mgr) {
    ACameraManager_delete (self->mgr);
    self->mgr = NULL;
  }

  GST_INFO_OBJECT (self,
      "Closed camera; ahc2src stop() — total %" G_GUINT64_FORMAT " frames "
      "(hwb_mem alloc total=%d)",
      self->frame_count, g_atomic_int_get (&self->hwb_mem_count));
  return TRUE;
}

static GstFlowReturn
gst_ahc2_src_create (GstPushSrc * src, GstBuffer ** outbuf)
{
  GstAhc2Src *self = GST_AHC2_SRC (src);
  GstDataQueueItem *it = NULL;

  if (self->use_surface_bypass) {
    GstBuffer    *buf;
    GstClockTime  now, duration, ts;
    GstClock     *clock;

    if (self->fps_n > 0 && self->fps_d > 0) {
      duration = gst_util_uint64_scale_int (GST_SECOND,
          self->fps_d, self->fps_n);
    } else {
      duration = gst_util_uint64_scale_int (GST_SECOND, 1, 30);
    }

    clock = gst_element_get_clock (GST_ELEMENT (self));
    if (clock) {
      GstClockTime base = gst_element_get_base_time (GST_ELEMENT (self));
      GstClockTime target;

      now = gst_clock_get_time (clock);
      target = base + (self->bypass_frame_count * duration);
      if (target > now) {
        GstClockID id = gst_clock_new_single_shot_id (clock, target);
        gst_clock_id_wait (id, NULL);
        gst_clock_id_unref (id);
      }
      ts = (target >= base) ? (target - base) : 0;
      gst_object_unref (clock);
    } else {
      g_usleep (G_USEC_PER_SEC / 30);
      ts = self->bypass_frame_count * duration;
    }

    buf = gst_buffer_new ();
    GST_BUFFER_PTS (buf)      = ts;
    GST_BUFFER_DTS (buf)      = ts;
    GST_BUFFER_DURATION (buf) = duration;

    if (self->bypass_meta_api_type != 0) {
      GstMeta *m;
      const GstMetaInfo *info = gst_meta_get_info ("GstAhc2SurfaceBypassMeta");
      if (info) {
        m = gst_buffer_add_meta (buf, info, NULL);
        if (m) {

          GstAhc2SurfaceBypassMeta *bm = (GstAhc2SurfaceBypassMeta *) m;
          bm->arrival_time = ts;
        }
      }
    }

    if ((self->bypass_frame_count % 60) == 0) {
      GST_INFO_OBJECT (self,
          "ahc2src #%" G_GUINT64_FORMAT " (BYPASS, size=0, ts=%" GST_TIME_FORMAT ") — zero-copy",
          self->bypass_frame_count, GST_TIME_ARGS (ts));
    }
    self->bypass_frame_count++;
    *outbuf = buf;
    return GST_FLOW_OK;
  }

  if (!gst_data_queue_pop (self->queue, &it)) {

    return GST_FLOW_FLUSHING;
  }

  Ahc2QueueItem *qi = (Ahc2QueueItem *) it;

  GstBuffer *buf = NULL;
  if (self->use_gl_memory && self->gl_context) {
    buf = ahc2_aimage_to_gl_buffer (self, qi->img);
    if (buf) {
      qi->img = NULL;
      if ((self->frame_count % 30) == 0)
        GST_INFO_OBJECT (self, "ahc2src #%" G_GUINT64_FORMAT
            " (GL frame, zero-copy)", self->frame_count);
      ahc2_queue_item_destroy (qi);
      self->frame_count++;
      *outbuf = buf;
      return GST_FLOW_OK;
    }

    GST_WARNING_OBJECT (self, "GL path failed, falling back to raw");
  }

  buf = ahc2_aimage_to_buffer_m1 (self, qi->img);

  if (buf && self->hwb_allocator) {

    GstAhc2HwbMemory *hwb_mem = gst_ahc2_hwb_memory_new_from_aimage (
        self->hwb_allocator, qi->img,  1);
    if (hwb_mem) {

      qi->img = NULL;

      gst_buffer_append_memory (buf, GST_MEMORY_CAST (hwb_mem));

      gint n = g_atomic_int_add (&self->hwb_mem_count, 1);
      if ((n % 30) == 0)
        GST_INFO_OBJECT (self, "hwb_mem alloc count=%d (AImage 위임)", n);
    } else {
      GST_WARNING_OBJECT (self, "gst_ahc2_hwb_memory_new_from_aimage failed; "
          "fallback to immediate AImage_delete");
    }
  }

  ahc2_queue_item_destroy (qi);

  if (!buf) {
    GST_ERROR_OBJECT (self, "ahc2_aimage_to_buffer_m1 failed");
    return GST_FLOW_ERROR;
  }

  if ((self->frame_count % 30) == 0)
    GST_INFO_OBJECT (self, "ahc2src #%" G_GUINT64_FORMAT " (real frame)",
        self->frame_count);

  self->frame_count++;
  *outbuf = buf;
  return GST_FLOW_OK;
}

static gboolean
gst_ahc2_src_unlock (GstBaseSrc * bsrc)
{
  GstAhc2Src *self = GST_AHC2_SRC (bsrc);
  GST_DEBUG_OBJECT (self, "unlock — flushing queue");
  g_atomic_int_set (&self->flushing, 1);
  if (self->queue) {
    gst_data_queue_set_flushing (self->queue, TRUE);

  }
  return TRUE;
}

static gboolean
gst_ahc2_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstAhc2Src *self = GST_AHC2_SRC (bsrc);
  GST_DEBUG_OBJECT (self, "unlock_stop — resume queue");
  g_atomic_int_set (&self->flushing, 0);
  if (self->queue) {
    gst_data_queue_flush (self->queue);
    gst_data_queue_set_flushing (self->queue, FALSE);
  }
  return TRUE;
}

static gboolean
gst_ahc2_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  GstAhc2Src *self = GST_AHC2_SRC (bsrc);

  if (GST_QUERY_TYPE (query) == GST_QUERY_CONTEXT) {
    if (gst_gl_handle_context_query (GST_ELEMENT (bsrc), query,
            self->gl_display, self->gl_context, self->gl_other_context)) {
      GST_DEBUG_OBJECT (self, "GL context query handled (display=%p, ctx=%p)",
          self->gl_display, self->gl_context);
      return TRUE;
    }
  }
  return GST_BASE_SRC_CLASS (gst_ahc2_src_parent_class)->query (bsrc, query);
}

static void
gst_ahc2_src_set_context (GstElement * element, GstContext * context)
{
  GstAhc2Src *self = GST_AHC2_SRC (element);

  if (context && gst_context_has_context_type (context,
          GST_AHC2_CONTEXT_TYPE_SURFACE_BYPASS)) {
    const GstStructure *s = gst_context_get_structure (context);
    gpointer    win_ptr = NULL;
    gint        version = 0;
    const gchar *owner  = NULL;

    if (!gst_structure_get_int (s, GST_AHC2_CTX_KEY_VERSION, &version)
        || version != GST_AHC2_CTX_PROTOCOL_VERSION) {
      GST_WARNING_OBJECT (self,
          "ahc2.surface-bypass: unsupported version %d (expected %d) — ignoring",
          version, GST_AHC2_CTX_PROTOCOL_VERSION);
    } else if (!gst_structure_get (s,
                   GST_AHC2_CTX_KEY_WINDOW, G_TYPE_POINTER, &win_ptr, NULL)
               || win_ptr == NULL) {
      GST_WARNING_OBJECT (self,
          "ahc2.surface-bypass: missing/NULL anative-window — ignoring");
    } else {
      owner = gst_structure_get_string (s, GST_AHC2_CTX_KEY_OWNER);
      self->bypass_window      = (ANativeWindow *) win_ptr;
      self->use_surface_bypass = TRUE;

      if (self->bypass_meta_api_type == 0) {
        self->bypass_meta_api_type = g_type_from_name ("GstAhc2SurfaceBypassMetaAPI");
      }
      GST_INFO_OBJECT (self,
          "Mode A armed: bypass_window=%p (from owner='%s', v%d, meta_type=%lu)",
          self->bypass_window, owner ? owner : "?", version,
          (gulong) self->bypass_meta_api_type);
    }

  }

  if (gst_gl_handle_set_context (element, context,
          &self->gl_display, &self->gl_other_context)) {
    GST_INFO_OBJECT (self,
        "GstGL context received (display=%p, other_ctx=%p, type=%s)",
        self->gl_display, self->gl_other_context,
        gst_context_get_context_type (context));
  }

  GST_ELEMENT_CLASS (gst_ahc2_src_parent_class)->set_context (element, context);
}

static gboolean
gst_ahc2_src_decide_allocation (GstBaseSrc * bsrc, GstQuery * query)
{
  GstAhc2Src *self = GST_AHC2_SRC (bsrc);
  GstCaps *caps = NULL;
  gboolean need_pool = FALSE;
  guint size = 0;
  const guint min_buffers = 4;
  const guint max_buffers = 8;

  gst_query_parse_allocation (query, &caps, &need_pool);
  if (!caps) {
    GST_ERROR_OBJECT (self, "decide_allocation: no caps in query");
    return FALSE;
  }

  GstVideoInfo info;
  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "decide_allocation: invalid caps");
    return FALSE;
  }
  size = GST_VIDEO_INFO_SIZE (&info);

  if (self->pool) {
    gst_object_unref (self->pool);
    self->pool = NULL;
  }
  self->pool = gst_ahc2_hwb_pool_new ();

  GstStructure *cfg = gst_buffer_pool_get_config (self->pool);
  gst_buffer_pool_config_set_params (cfg, caps, size, min_buffers, max_buffers);
  gst_buffer_pool_config_add_option (cfg, GST_BUFFER_POOL_OPTION_VIDEO_META);
  if (!gst_buffer_pool_set_config (self->pool, cfg)) {
    GST_ERROR_OBJECT (self, "decide_allocation: pool set_config failed");
    gst_object_unref (self->pool);
    self->pool = NULL;
    return FALSE;
  }

  if (!gst_buffer_pool_set_active (self->pool, TRUE)) {
    GST_ERROR_OBJECT (self, "decide_allocation: pool set_active failed");
    gst_object_unref (self->pool);
    self->pool = NULL;
    return FALSE;
  }

  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_set_nth_allocation_pool (query, 0, self->pool, size,
        min_buffers, max_buffers);
  else
    gst_query_add_allocation_pool (query, self->pool, size,
        min_buffers, max_buffers);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  GST_INFO_OBJECT (self,
      "decide_allocation: pool=%p, caps=%" GST_PTR_FORMAT
      ", size=%u, min=%u, max=%u",
      self->pool, caps, size, min_buffers, max_buffers);

  return TRUE;
}

static void
gst_ahc2_src_finalize (GObject * obj)
{
  GstAhc2Src *self = GST_AHC2_SRC (obj);

  gst_ahc2_src_cleanup_pool (self);

  if (self->hwb_allocator) {
    gst_object_unref (self->hwb_allocator);
    self->hwb_allocator = NULL;
  }

  gst_clear_object (&self->gl_context);
  gst_clear_object (&self->gl_other_context);
  gst_clear_object (&self->gl_display);

  if (self->queue) {
    gst_data_queue_set_flushing (self->queue, TRUE);
    gst_data_queue_flush (self->queue);
    g_object_unref (self->queue);
    self->queue = NULL;
  }

  G_OBJECT_CLASS (gst_ahc2_src_parent_class)->finalize (obj);
}

static gboolean
ahc2_queue_check_full (GstDataQueue * q, guint visible, guint bytes,
    guint64 time, gpointer ctx)
{
  return visible >= 8;
}

static void
gst_ahc2_src_class_init (GstAhc2SrcClass * klass)
{
  GObjectClass    *gobj = G_OBJECT_CLASS (klass);
  GstElementClass *elem = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *base = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *push = GST_PUSH_SRC_CLASS (klass);

  gst_element_class_add_static_pad_template (elem, &src_template);

  gst_element_class_set_static_metadata (elem,
      "Android Camera2 NDK Source",
      "Source/Video",
      "Captures video frames from Android Camera2 NDK",
      "KIMRIHYEON <dlgus8648@naver.com>");

  gobj->finalize         = gst_ahc2_src_finalize;
  elem->set_context      = gst_ahc2_src_set_context;
  base->start            = gst_ahc2_src_start;
  base->stop             = gst_ahc2_src_stop;
  base->get_caps         = gst_ahc2_src_get_caps;
  base->set_caps         = gst_ahc2_src_set_caps;
  base->decide_allocation = gst_ahc2_src_decide_allocation;
  base->query            = gst_ahc2_src_query;
  base->unlock           = gst_ahc2_src_unlock;
  base->unlock_stop      = gst_ahc2_src_unlock_stop;
  push->create           = gst_ahc2_src_create;

  GST_DEBUG_CATEGORY_INIT (gst_ahc2_src_debug, "ahc2src", 0,
      "Android Camera2 NDK source element");
}

static void
gst_ahc2_src_init (GstAhc2Src * self)
{

  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);
  gst_base_src_set_do_timestamp (GST_BASE_SRC (self), TRUE);

  self->queue = gst_data_queue_new (ahc2_queue_check_full,
       NULL,  NULL,  self);
  g_atomic_int_set (&self->flushing, 0);

  self->hwb_allocator = gst_ahc2_hwb_allocator_new ();
  g_atomic_int_set (&self->hwb_mem_count, 0);

  gst_gl_memory_egl_init_once ();
}
