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

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>
#include <gst/video/videooverlay.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>

#include <string.h>

#include "gstahc2sink.h"
#include "gstahc2protocol.h"
#include "gstahc2sinkbypass.h"

GST_DEBUG_CATEGORY (ahc2sink_debug);
#define GST_CAT_DEFAULT ahc2sink_debug

#define AHC2SINK_LOCK_CAPS                                  \
    "video/x-raw, "                                         \
    "format = (string) { RGBA, RGBx, RGB16, NV12, NV21 }, " \
    "width = (int) [ 16, 8192 ], "                          \
    "height = (int) [ 16, 8192 ], "                         \
    "framerate = (fraction) [ 0/1, 240/1 ]"

#define AHC2SINK_BYPASS_CAPS                                          \
    "video/x-raw(" GST_AHC2_CAPS_FEATURE_SURFACE_BYPASS "), "          \
    "format = (string) { NV12, NV21, YV12, I420, RGBA, RGBx }, "      \
    "width = (int) [ 16, 8192 ], "                                    \
    "height = (int) [ 16, 8192 ], "                                   \
    "framerate = (fraction) [ 0/1, 240/1 ]"

#define AHC2SINK_TEMPLATE_CAPS  \
    AHC2SINK_BYPASS_CAPS "; " AHC2SINK_LOCK_CAPS

#define DEFAULT_BYPASS_MODE       GST_AHC2_SINK_BYPASS_AUTO
#define DEFAULT_ORIENTATION_HINT  0
#define DEFAULT_TARGET_FPS        0.0f
#define DEFAULT_DATASPACE         0

typedef enum {
  GST_AHC2_SINK_BYPASS_AUTO         = 0,
  GST_AHC2_SINK_BYPASS_FORCE_BYPASS = 1,
  GST_AHC2_SINK_BYPASS_FORCE_LOCK   = 2,
} GstAhc2SinkBypassMode;

#define GST_TYPE_AHC2_SINK_BYPASS_MODE (gst_ahc2_sink_bypass_mode_get_type())
static GType
gst_ahc2_sink_bypass_mode_get_type (void)
{
  static gsize type = 0;
  static const GEnumValue values[] = {
    { GST_AHC2_SINK_BYPASS_AUTO,
      "Auto: Mode A if upstream supports surface-bypass, else Mode B",
      "auto" },
    { GST_AHC2_SINK_BYPASS_FORCE_BYPASS,
      "Force Mode A (zero-copy bypass) — fail if upstream can't",
      "force-bypass" },
    { GST_AHC2_SINK_BYPASS_FORCE_LOCK,
      "Force Mode B (lock + memcpy + unlockAndPost)",
      "force-lock" },
    { 0, NULL, NULL }
  };
  if (g_once_init_enter (&type)) {
    GType t = g_enum_register_static ("GstAhc2SinkBypassMode", values);
    g_once_init_leave (&type, t);
  }
  return (GType) type;
}

enum {
  PROP_0,
  PROP_BYPASS_MODE,
  PROP_ORIENTATION_HINT,
  PROP_TARGET_FPS,
  PROP_DATASPACE,
  PROP_POSTED_FRAMES,
  PROP_DROPPED_FRAMES,
  PROP_LOCK_FAILURES,
  PROP_LAST
};

typedef enum {
  GST_AHC2_SINK_MODE_NONE   = 0,
  GST_AHC2_SINK_MODE_LOCK   = 1,
  GST_AHC2_SINK_MODE_BYPASS = 2,
} GstAhc2SinkActiveMode;

struct _GstAhc2Sink {
  GstVideoSink parent;

  ANativeWindow *win;
  GMutex         win_lock;

  GstAhc2SinkBypassMode bypass_mode;
  gint                  orientation_hint;
  gfloat                target_fps;
  gint                  dataspace;

  GstVideoInfo          info;
  gint                  format_window;
  gboolean              caps_set;
  GstAhc2SinkActiveMode active_mode;

  gboolean              bypass_ctx_pushed;

  guint64       posted_frames;
  guint64       dropped_frames;
  guint64       lock_failures;
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE (
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,

    GST_STATIC_CAPS (AHC2SINK_TEMPLATE_CAPS)
);

static void gst_ahc2_sink_finalize     (GObject *o);
static void gst_ahc2_sink_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec);
static void gst_ahc2_sink_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec);

static gboolean      gst_ahc2_sink_start              (GstBaseSink *bsink);
static gboolean      gst_ahc2_sink_stop               (GstBaseSink *bsink);
static gboolean      gst_ahc2_sink_set_caps           (GstBaseSink *bsink, GstCaps *caps);
static gboolean      gst_ahc2_sink_propose_allocation (GstBaseSink *bsink, GstQuery *query);
static GstFlowReturn gst_ahc2_sink_show_frame         (GstVideoSink *vsink, GstBuffer *buf);
static GstStateChangeReturn gst_ahc2_sink_change_state (GstElement *element,
                                                         GstStateChange transition);

static void gst_ahc2_sink_video_overlay_init (GstVideoOverlayInterface *iface);
static void gst_ahc2_sink_set_window_handle  (GstVideoOverlay *overlay, guintptr handle);
static void gst_ahc2_sink_expose             (GstVideoOverlay *overlay);

G_DEFINE_TYPE_WITH_CODE (GstAhc2Sink, gst_ahc2_sink, GST_TYPE_VIDEO_SINK,
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
        gst_ahc2_sink_video_overlay_init))

static gboolean
caps_format_to_anative_format (GstVideoFormat fmt, gint *out_format)
{
  switch (fmt) {
    case GST_VIDEO_FORMAT_RGBA:
      *out_format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
      return TRUE;
    case GST_VIDEO_FORMAT_RGBx:
      *out_format = AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM;
      return TRUE;
    case GST_VIDEO_FORMAT_RGB16:
      *out_format = AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM;
      return TRUE;
    default:
      return FALSE;
  }
}

typedef int32_t (*pfn_setBuffersTransform) (ANativeWindow *, int32_t);
typedef int32_t (*pfn_setBuffersDataSpace) (ANativeWindow *, int32_t);
typedef int32_t (*pfn_setFrameRate)        (ANativeWindow *, float, int8_t);
typedef void    (*pfn_tryAllocateBuffers)  (ANativeWindow *);

static gpointer
anw_sym (const char *name)
{

  return dlsym (RTLD_DEFAULT, name);
}

static pfn_setBuffersTransform
anw_setBuffersTransform_fn (void)
{
  static gsize once = 0;
  static pfn_setBuffersTransform fn = NULL;
  if (g_once_init_enter (&once)) {
    fn = (pfn_setBuffersTransform) anw_sym ("ANativeWindow_setBuffersTransform");
    g_once_init_leave (&once, 1);
  }
  return fn;
}

static pfn_setBuffersDataSpace
anw_setBuffersDataSpace_fn (void)
{
  static gsize once = 0;
  static pfn_setBuffersDataSpace fn = NULL;
  if (g_once_init_enter (&once)) {
    fn = (pfn_setBuffersDataSpace) anw_sym ("ANativeWindow_setBuffersDataSpace");
    g_once_init_leave (&once, 1);
  }
  return fn;
}

static pfn_setFrameRate
anw_setFrameRate_fn (void)
{
  static gsize once = 0;
  static pfn_setFrameRate fn = NULL;
  if (g_once_init_enter (&once)) {
    fn = (pfn_setFrameRate) anw_sym ("ANativeWindow_setFrameRate");
    g_once_init_leave (&once, 1);
  }
  return fn;
}

static pfn_tryAllocateBuffers
anw_tryAllocateBuffers_fn (void)
{
  static gsize once = 0;
  static pfn_tryAllocateBuffers fn = NULL;
  if (g_once_init_enter (&once)) {
    fn = (pfn_tryAllocateBuffers) anw_sym ("ANativeWindow_tryAllocateBuffers");
    g_once_init_leave (&once, 1);
  }
  return fn;
}

static gint
orientation_to_anative_transform (gint deg)
{
  switch (deg) {
    case 90:  return ANATIVEWINDOW_TRANSFORM_ROTATE_90;
    case 180: return ANATIVEWINDOW_TRANSFORM_ROTATE_180;
    case 270: return ANATIVEWINDOW_TRANSFORM_ROTATE_270;
    case 0:
    default:  return ANATIVEWINDOW_TRANSFORM_IDENTITY;
  }
}

static void
gst_ahc2_sink_class_init (GstAhc2SinkClass *klass)
{
  GObjectClass     *gobject_class   = G_OBJECT_CLASS (klass);
  GstElementClass  *element_class   = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *basesink_class  = GST_BASE_SINK_CLASS (klass);
  GstVideoSinkClass *videosink_class = GST_VIDEO_SINK_CLASS (klass);

  gobject_class->finalize     = gst_ahc2_sink_finalize;
  gobject_class->set_property = gst_ahc2_sink_set_property;
  gobject_class->get_property = gst_ahc2_sink_get_property;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_ahc2_sink_change_state);

  basesink_class->start              = GST_DEBUG_FUNCPTR (gst_ahc2_sink_start);
  basesink_class->stop               = GST_DEBUG_FUNCPTR (gst_ahc2_sink_stop);
  basesink_class->set_caps           = GST_DEBUG_FUNCPTR (gst_ahc2_sink_set_caps);
  basesink_class->propose_allocation = GST_DEBUG_FUNCPTR (gst_ahc2_sink_propose_allocation);

  videosink_class->show_frame = GST_DEBUG_FUNCPTR (gst_ahc2_sink_show_frame);

  g_object_class_install_property (gobject_class, PROP_BYPASS_MODE,
      g_param_spec_enum ("bypass-mode", "Bypass mode",
          "Mode selection: auto / force-bypass / force-lock (P0: bypass not yet implemented)",
          GST_TYPE_AHC2_SINK_BYPASS_MODE, DEFAULT_BYPASS_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ORIENTATION_HINT,
      g_param_spec_int ("orientation-hint", "Orientation hint",
          "Display rotation in degrees: 0/90/180/270 → setBuffersTransform",
          0, 270, DEFAULT_ORIENTATION_HINT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TARGET_FPS,
      g_param_spec_float ("target-fps", "Target frame rate",
          "Hint for ANativeWindow_setFrameRate (0 = system default, API 30+)",
          0.0f, 240.0f, DEFAULT_TARGET_FPS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DATASPACE,
      g_param_spec_int ("dataspace", "Buffer dataspace",
          "ADataSpace value for setBuffersDataSpace (0 = ADATASPACE_UNKNOWN, API 28+)",
          0, G_MAXINT, DEFAULT_DATASPACE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_POSTED_FRAMES,
      g_param_spec_uint64 ("posted-frames", "Posted frames",
          "Number of frames successfully posted to the surface (lifetime)",
          0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DROPPED_FRAMES,
      g_param_spec_uint64 ("dropped-frames", "Dropped frames",
          "Number of frames dropped (no window, etc.)",
          0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LOCK_FAILURES,
      g_param_spec_uint64 ("lock-failures", "ANativeWindow_lock failures",
          "Number of times ANativeWindow_lock returned non-zero",
          0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class, &sink_template);

  gst_element_class_set_static_metadata (element_class,
      "Android Camera2 NDK ANativeWindow Sink",
      "Sink/Video/Android",
      "Renders raw video to an Android Surface via ANativeWindow (GL-free)",
      "KIMRIHYEON <dlgus8648@naver.com>");

  GST_DEBUG_CATEGORY_INIT (ahc2sink_debug, "ahc2sink", 0,
      "Android NDK ANativeWindow video sink");

  (void) gst_ahc2_surface_bypass_meta_get_info ();
}

static void
gst_ahc2_sink_init (GstAhc2Sink *self)
{
  g_mutex_init (&self->win_lock);
  self->win               = NULL;
  self->bypass_mode       = DEFAULT_BYPASS_MODE;
  self->orientation_hint  = DEFAULT_ORIENTATION_HINT;
  self->target_fps        = DEFAULT_TARGET_FPS;
  self->dataspace         = DEFAULT_DATASPACE;
  self->format_window     = 0;
  self->caps_set          = FALSE;
  self->active_mode       = GST_AHC2_SINK_MODE_NONE;
  self->bypass_ctx_pushed = FALSE;
  self->posted_frames     = 0;
  self->dropped_frames    = 0;
  self->lock_failures     = 0;
  gst_video_info_init (&self->info);
}

static void
gst_ahc2_sink_finalize (GObject *o)
{
  GstAhc2Sink *self = GST_AHC2_SINK (o);

  g_mutex_lock (&self->win_lock);
  if (self->win) {
    ANativeWindow_release (self->win);
    self->win = NULL;
  }
  g_mutex_unlock (&self->win_lock);
  g_mutex_clear (&self->win_lock);

  G_OBJECT_CLASS (gst_ahc2_sink_parent_class)->finalize (o);
}

static void
gst_ahc2_sink_set_property (GObject *o, guint id, const GValue *v, GParamSpec *pspec)
{
  GstAhc2Sink *self = GST_AHC2_SINK (o);
  switch (id) {
    case PROP_BYPASS_MODE:      self->bypass_mode      = g_value_get_enum  (v); break;
    case PROP_ORIENTATION_HINT: self->orientation_hint = g_value_get_int   (v); break;
    case PROP_TARGET_FPS:       self->target_fps       = g_value_get_float (v); break;
    case PROP_DATASPACE:        self->dataspace        = g_value_get_int   (v); break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
      break;
  }
}

static void
gst_ahc2_sink_get_property (GObject *o, guint id, GValue *v, GParamSpec *pspec)
{
  GstAhc2Sink *self = GST_AHC2_SINK (o);
  switch (id) {
    case PROP_BYPASS_MODE:      g_value_set_enum   (v, self->bypass_mode);       break;
    case PROP_ORIENTATION_HINT: g_value_set_int    (v, self->orientation_hint);  break;
    case PROP_TARGET_FPS:       g_value_set_float  (v, self->target_fps);        break;
    case PROP_DATASPACE:        g_value_set_int    (v, self->dataspace);         break;
    case PROP_POSTED_FRAMES:    g_value_set_uint64 (v, self->posted_frames);     break;
    case PROP_DROPPED_FRAMES:   g_value_set_uint64 (v, self->dropped_frames);    break;
    case PROP_LOCK_FAILURES:    g_value_set_uint64 (v, self->lock_failures);     break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, pspec);
      break;
  }
}

static gboolean
gst_ahc2_sink_start (GstBaseSink *bsink)
{
  GstAhc2Sink *self = GST_AHC2_SINK (bsink);

  self->posted_frames  = 0;
  self->dropped_frames = 0;
  self->lock_failures  = 0;

  GST_INFO_OBJECT (self, "start: win=%p, bypass-mode=%d", self->win, self->bypass_mode);
  return TRUE;
}

static gboolean
gst_ahc2_sink_stop (GstBaseSink *bsink)
{
  GstAhc2Sink *self = GST_AHC2_SINK (bsink);

  GST_INFO_OBJECT (self, "stop: posted=%" G_GUINT64_FORMAT
      ", dropped=%" G_GUINT64_FORMAT
      ", lock_fail=%" G_GUINT64_FORMAT,
      self->posted_frames, self->dropped_frames, self->lock_failures);

  self->caps_set = FALSE;
  return TRUE;
}

static gboolean
gst_ahc2_sink_set_caps (GstBaseSink *bsink, GstCaps *caps)
{
  GstAhc2Sink *self = GST_AHC2_SINK (bsink);
  GstVideoInfo info;
  gint format_w = 0;
  ANativeWindow *win;
  gint rc;
  GstCapsFeatures *feat;
  gboolean has_bypass_feature;

  feat = gst_caps_get_features (caps, 0);
  has_bypass_feature = (feat != NULL &&
      gst_caps_features_contains (feat, GST_AHC2_CAPS_FEATURE_SURFACE_BYPASS));

  if (self->bypass_mode == GST_AHC2_SINK_BYPASS_FORCE_LOCK && has_bypass_feature) {
    GST_ERROR_OBJECT (self,
        "set_caps: caps has bypass feature but bypass-mode=force-lock");
    return FALSE;
  }
  if (self->bypass_mode == GST_AHC2_SINK_BYPASS_FORCE_BYPASS && !has_bypass_feature) {
    GST_ERROR_OBJECT (self,
        "set_caps: bypass-mode=force-bypass but caps has no bypass feature "
        "(upstream is not bypass-aware — needs ahc2src + §6 patch)");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "set_caps: gst_video_info_from_caps failed");
    return FALSE;
  }

  if (!has_bypass_feature) {
    if (!caps_format_to_anative_format (GST_VIDEO_INFO_FORMAT (&info), &format_w)) {
      GST_ERROR_OBJECT (self, "set_caps: unsupported format %s",
          gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&info)));
      return FALSE;
    }
  }

  self->info          = info;
  self->format_window = format_w;
  self->caps_set      = TRUE;
  self->active_mode   = has_bypass_feature ? GST_AHC2_SINK_MODE_BYPASS
                                           : GST_AHC2_SINK_MODE_LOCK;

  GST_INFO_OBJECT (self, "set_caps: %dx%d %s @ %d/%d, mode=%s%s",
      info.width, info.height,
      gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&info)),
      info.fps_n, info.fps_d,
      has_bypass_feature ? "BYPASS (Mode A)" : "LOCK (Mode B)",
      has_bypass_feature ? " — frame data bypasses GStreamer" : "");

  if (has_bypass_feature) {
    g_object_set (G_OBJECT (self), "sync", FALSE, NULL);

    return TRUE;
  }

  g_mutex_lock (&self->win_lock);
  win = self->win;
  if (win) ANativeWindow_acquire (win);
  g_mutex_unlock (&self->win_lock);

  if (win) {

    rc = ANativeWindow_setBuffersGeometry (win, info.width, info.height, format_w);
    if (rc != 0) {
      GST_ERROR_OBJECT (self,
          "ANativeWindow_setBuffersGeometry(%dx%d, 0x%x) failed rc=%d",
          info.width, info.height, format_w, rc);
      ANativeWindow_release (win);
      return FALSE;
    }

    if (self->orientation_hint != 0) {

      pfn_setBuffersTransform fn = anw_setBuffersTransform_fn ();
      if (fn) {
        gint t = orientation_to_anative_transform (self->orientation_hint);
        rc = fn (win, t);
        if (rc != 0) {
          GST_WARNING_OBJECT (self,
              "setBuffersTransform(%d°) rc=%d (non-fatal)",
              self->orientation_hint, rc);
        }
      } else {
        GST_WARNING_OBJECT (self,
            "ANativeWindow_setBuffersTransform unavailable (device API < 26?)");
      }
    }

    if (self->dataspace != 0) {
      pfn_setBuffersDataSpace fn = anw_setBuffersDataSpace_fn ();
      if (fn) {
        rc = fn (win, self->dataspace);
        if (rc != 0) {
          GST_WARNING_OBJECT (self,
              "setBuffersDataSpace(%d) rc=%d (non-fatal)", self->dataspace, rc);
        }
      } else {
        GST_WARNING_OBJECT (self,
            "ANativeWindow_setBuffersDataSpace unavailable (device API < 28)");
      }
    }

    if (self->target_fps > 0.0f) {
      pfn_setFrameRate fn = anw_setFrameRate_fn ();
      if (fn) {
        rc = fn (win, self->target_fps,
            ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
        if (rc != 0) {
          GST_WARNING_OBJECT (self,
              "setFrameRate(%.1f) rc=%d (non-fatal)", self->target_fps, rc);
        }
      } else {
        GST_WARNING_OBJECT (self,
            "ANativeWindow_setFrameRate unavailable (device API < 30)");
      }
    }

    {
      pfn_tryAllocateBuffers fn = anw_tryAllocateBuffers_fn ();
      if (fn) fn (win);

    }

    ANativeWindow_release (win);
  } else {
    GST_INFO_OBJECT (self, "set_caps: window not yet attached, will configure on set_window_handle");
  }

  return TRUE;
}

static GstFlowReturn
gst_ahc2_sink_show_frame (GstVideoSink *vsink, GstBuffer *buf)
{
  GstAhc2Sink *self = GST_AHC2_SINK (vsink);
  ANativeWindow *win;
  ANativeWindow_Buffer outBuf;
  GstMapInfo map;
  gint rc;
  gint i, copy_height, src_stride, dst_stride_bytes, copy_bytes;
  gint bpp;
  guint8 *src, *dst;

  if (G_UNLIKELY (!self->caps_set)) {
    GST_WARNING_OBJECT (self, "show_frame: caps not set yet — drop");
    self->dropped_frames++;
    return GST_FLOW_OK;
  }

  if (self->active_mode == GST_AHC2_SINK_MODE_BYPASS) {

    GstAhc2SurfaceBypassMeta *m = gst_buffer_get_ahc2_surface_bypass_meta (buf);
    self->posted_frames++;
    if ((self->posted_frames % 60) == 0) {
      GST_INFO_OBJECT (self,
          "Mode A bypass frame #%" G_GUINT64_FORMAT
          " (size=%" G_GSIZE_FORMAT ", meta=%s) — zero-copy",
          self->posted_frames,
          gst_buffer_get_size (buf),
          m ? "yes" : "no");
    }
    return GST_FLOW_OK;
  }

  g_mutex_lock (&self->win_lock);
  win = self->win;
  if (win) ANativeWindow_acquire (win);
  g_mutex_unlock (&self->win_lock);

  if (G_UNLIKELY (!win)) {

    self->dropped_frames++;
    GST_LOG_OBJECT (self, "show_frame: no window — drop");
    return GST_FLOW_OK;
  }

  rc = ANativeWindow_lock (win, &outBuf, NULL);
  if (G_UNLIKELY (rc != 0)) {
    self->lock_failures++;
    GST_WARNING_OBJECT (self, "ANativeWindow_lock rc=%d (drop)", rc);
    ANativeWindow_release (win);
    return GST_FLOW_OK;
  }

  if (G_UNLIKELY (!gst_buffer_map (buf, &map, GST_MAP_READ))) {
    GST_ERROR_OBJECT (self, "gst_buffer_map failed");
    ANativeWindow_unlockAndPost (win);
    ANativeWindow_release (win);
    self->dropped_frames++;
    return GST_FLOW_ERROR;
  }

  bpp              = GST_VIDEO_INFO_COMP_PSTRIDE (&self->info, 0);
  src              = map.data;
  dst              = (guint8 *) outBuf.bits;
  src_stride       = GST_VIDEO_INFO_PLANE_STRIDE (&self->info, 0);
  dst_stride_bytes = outBuf.stride * bpp;
  copy_height      = MIN (self->info.height, outBuf.height);
  copy_bytes       = MIN (src_stride, dst_stride_bytes);

  for (i = 0; i < copy_height; i++) {
    memcpy (dst + i * dst_stride_bytes,
            src + i * src_stride,
            copy_bytes);
  }

  gst_buffer_unmap (buf, &map);

  rc = ANativeWindow_unlockAndPost (win);
  if (G_UNLIKELY (rc != 0)) {
    GST_WARNING_OBJECT (self, "ANativeWindow_unlockAndPost rc=%d", rc);
  } else {
    self->posted_frames++;
  }

  ANativeWindow_release (win);
  return GST_FLOW_OK;
}

static void
gst_ahc2_sink_set_window_handle (GstVideoOverlay *overlay, guintptr handle)
{
  GstAhc2Sink *self = GST_AHC2_SINK (overlay);
  ANativeWindow *new_win = (ANativeWindow *) handle;
  ANativeWindow *old_win;
  gboolean reconfigure = FALSE;

  GST_INFO_OBJECT (self, "set_window_handle: new=%p (was %p)", new_win, self->win);

  g_mutex_lock (&self->win_lock);
  old_win = self->win;
  if (new_win) {
    ANativeWindow_acquire (new_win);
  }
  self->win = new_win;

  reconfigure = (new_win && self->caps_set);
  g_mutex_unlock (&self->win_lock);

  if (old_win) {
    ANativeWindow_release (old_win);
  }

  if (reconfigure && self->active_mode == GST_AHC2_SINK_MODE_LOCK) {
    gint rc = ANativeWindow_setBuffersGeometry (new_win,
        self->info.width, self->info.height, self->format_window);
    if (rc != 0) {
      GST_WARNING_OBJECT (self,
          "set_window_handle: setBuffersGeometry rc=%d", rc);
    }
    if (self->orientation_hint != 0) {
      pfn_setBuffersTransform fn = anw_setBuffersTransform_fn ();
      if (fn) fn (new_win, orientation_to_anative_transform (self->orientation_hint));
    }
    if (self->dataspace != 0) {
      pfn_setBuffersDataSpace fn = anw_setBuffersDataSpace_fn ();
      if (fn) fn (new_win, self->dataspace);
    }
    {
      pfn_tryAllocateBuffers fn = anw_tryAllocateBuffers_fn ();
      if (fn) fn (new_win);
    }
  }

  if (new_win && !self->bypass_ctx_pushed
      && self->bypass_mode != GST_AHC2_SINK_BYPASS_FORCE_LOCK) {
    GstState state, pending;
    gst_element_get_state (GST_ELEMENT (self), &state, &pending, 0);
    if (state >= GST_STATE_PAUSED || pending >= GST_STATE_PAUSED) {
      if (gst_ahc2_sink_push_bypass_context (GST_ELEMENT (self), new_win)) {
        self->bypass_ctx_pushed = TRUE;
        GST_INFO_OBJECT (self,
            "set_window_handle: late win attach — bypass context pushed");
      }
    }
  }
}

static void
gst_ahc2_sink_expose (GstVideoOverlay *overlay)
{

  GstAhc2Sink *self = GST_AHC2_SINK (overlay);
  GST_LOG_OBJECT (self, "expose (no-op in P0)");
}

static void
gst_ahc2_sink_video_overlay_init (GstVideoOverlayInterface *iface)
{
  iface->set_window_handle = gst_ahc2_sink_set_window_handle;
  iface->expose            = gst_ahc2_sink_expose;

}

static GstStateChangeReturn
gst_ahc2_sink_change_state (GstElement *element, GstStateChange transition)
{
  GstAhc2Sink *self = GST_AHC2_SINK (element);
  GstStateChangeReturn ret;
  ANativeWindow *win_snapshot = NULL;
  gboolean want_push;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:

      g_mutex_lock (&self->win_lock);
      win_snapshot = self->win;
      if (win_snapshot) ANativeWindow_acquire (win_snapshot);
      g_mutex_unlock (&self->win_lock);

      want_push = (self->bypass_mode != GST_AHC2_SINK_BYPASS_FORCE_LOCK)
                  && win_snapshot != NULL
                  && !self->bypass_ctx_pushed;

      if (want_push) {
        if (gst_ahc2_sink_push_bypass_context (element, win_snapshot)) {
          self->bypass_ctx_pushed = TRUE;
          GST_INFO_OBJECT (self,
              "READY→PAUSED: bypass context pushed (win=%p)", win_snapshot);
        }
      } else {
        GST_DEBUG_OBJECT (self,
            "READY→PAUSED: skip ctx push (win=%p, mode=%d, already=%s)",
            win_snapshot, self->bypass_mode,
            self->bypass_ctx_pushed ? "yes" : "no");
      }

      if (win_snapshot) ANativeWindow_release (win_snapshot);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_ahc2_sink_parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:

      self->bypass_ctx_pushed = FALSE;
      self->active_mode = GST_AHC2_SINK_MODE_NONE;
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_ahc2_sink_propose_allocation (GstBaseSink *bsink, GstQuery *query)
{
  GstAhc2Sink *self = GST_AHC2_SINK (bsink);

  gst_query_add_allocation_meta (query, GST_AHC2_SURFACE_BYPASS_META_API_TYPE, NULL);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  GST_DEBUG_OBJECT (self,
      "propose_allocation: advertised GstAhc2SurfaceBypassMetaAPI + GstVideoMeta");

  return TRUE;
}
