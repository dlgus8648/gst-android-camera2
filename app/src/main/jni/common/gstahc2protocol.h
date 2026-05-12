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

#ifndef __GST_AHC2_PROTOCOL_H__
#define __GST_AHC2_PROTOCOL_H__

#include <gst/gst.h>
#include <android/native_window.h>

G_BEGIN_DECLS

#define GST_AHC2_CAPS_FEATURE_SURFACE_BYPASS  "memory:GstAhc2SurfaceBypass"

#define GST_AHC2_CONTEXT_TYPE_SURFACE_BYPASS  "ahc2.surface-bypass"
#define GST_AHC2_CTX_PROTOCOL_VERSION         1

#define GST_AHC2_CTX_KEY_VERSION   "version"
#define GST_AHC2_CTX_KEY_WINDOW    "anative-window"
#define GST_AHC2_CTX_KEY_OWNER     "owner-element"

typedef struct _GstAhc2SurfaceBypassMeta GstAhc2SurfaceBypassMeta;

struct _GstAhc2SurfaceBypassMeta {
  GstMeta meta;

  GstClockTime arrival_time;
};

GType                gst_ahc2_surface_bypass_meta_api_get_type (void);
const GstMetaInfo *  gst_ahc2_surface_bypass_meta_get_info     (void);

#define GST_AHC2_SURFACE_BYPASS_META_API_TYPE \
    (gst_ahc2_surface_bypass_meta_api_get_type ())
#define GST_AHC2_SURFACE_BYPASS_META_INFO \
    (gst_ahc2_surface_bypass_meta_get_info ())

#define gst_buffer_get_ahc2_surface_bypass_meta(buf) \
    ((GstAhc2SurfaceBypassMeta *) gst_buffer_get_meta ((buf), \
        GST_AHC2_SURFACE_BYPASS_META_API_TYPE))

GstAhc2SurfaceBypassMeta *
gst_buffer_add_ahc2_surface_bypass_meta (GstBuffer *buffer,
    GstClockTime arrival_time);

GstContext *
gst_ahc2_surface_bypass_context_new (ANativeWindow *win,
    const gchar *owner_element);

gboolean
gst_ahc2_surface_bypass_context_parse (GstContext *context,
    ANativeWindow **out_win,
    const gchar  **out_owner,
    gint          *out_version);

G_END_DECLS

#endif
