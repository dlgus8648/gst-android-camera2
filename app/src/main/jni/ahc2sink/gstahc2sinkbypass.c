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
#include <android/native_window.h>

#include "gstahc2protocol.h"
#include "gstahc2sinkbypass.h"

GST_DEBUG_CATEGORY_EXTERN (ahc2sink_debug);
#define GST_CAT_DEFAULT ahc2sink_debug

static gboolean
gst_ahc2_surface_bypass_meta_init (GstMeta *meta, gpointer params, GstBuffer *buffer)
{
  GstAhc2SurfaceBypassMeta *m = (GstAhc2SurfaceBypassMeta *) meta;
  m->arrival_time = 0;
  return TRUE;
}

static void
gst_ahc2_surface_bypass_meta_free (GstMeta *meta, GstBuffer *buffer)
{

}

static gboolean
gst_ahc2_surface_bypass_meta_transform (GstBuffer *dest, GstMeta *meta,
    GstBuffer *buffer, GQuark type, gpointer data)
{
  GstAhc2SurfaceBypassMeta *src_meta = (GstAhc2SurfaceBypassMeta *) meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    GstAhc2SurfaceBypassMeta *new_meta = gst_buffer_add_ahc2_surface_bypass_meta (
        dest, src_meta->arrival_time);
    return new_meta != NULL;
  }

  return FALSE;
}

GType
gst_ahc2_surface_bypass_meta_api_get_type (void)
{
  static gsize type = 0;
  static const gchar *tags[] = { "memory", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstAhc2SurfaceBypassMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return (GType) type;
}

const GstMetaInfo *
gst_ahc2_surface_bypass_meta_get_info (void)
{
  static const GstMetaInfo *info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) &info)) {
    const GstMetaInfo *_info = gst_meta_register (
        gst_ahc2_surface_bypass_meta_api_get_type (),
        "GstAhc2SurfaceBypassMeta",
        sizeof (GstAhc2SurfaceBypassMeta),
        gst_ahc2_surface_bypass_meta_init,
        gst_ahc2_surface_bypass_meta_free,
        gst_ahc2_surface_bypass_meta_transform);
    g_once_init_leave ((GstMetaInfo **) &info, (GstMetaInfo *) _info);
  }
  return info;
}

GstAhc2SurfaceBypassMeta *
gst_buffer_add_ahc2_surface_bypass_meta (GstBuffer *buffer,
    GstClockTime arrival_time)
{
  GstAhc2SurfaceBypassMeta *m;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  m = (GstAhc2SurfaceBypassMeta *) gst_buffer_add_meta (buffer,
      GST_AHC2_SURFACE_BYPASS_META_INFO, NULL);
  if (m) {
    m->arrival_time = arrival_time;
  }
  return m;
}

GstContext *
gst_ahc2_surface_bypass_context_new (ANativeWindow *win,
    const gchar *owner_element)
{
  GstContext *ctx;
  GstStructure *s;

  ctx = gst_context_new (GST_AHC2_CONTEXT_TYPE_SURFACE_BYPASS, TRUE);
  s = gst_context_writable_structure (ctx);
  gst_structure_set (s,
      GST_AHC2_CTX_KEY_VERSION, G_TYPE_INT,     GST_AHC2_CTX_PROTOCOL_VERSION,
      GST_AHC2_CTX_KEY_WINDOW,  G_TYPE_POINTER, win,
      GST_AHC2_CTX_KEY_OWNER,   G_TYPE_STRING,  owner_element ? owner_element : "ahc2sink",
      NULL);
  return ctx;
}

gboolean
gst_ahc2_surface_bypass_context_parse (GstContext *context,
    ANativeWindow **out_win,
    const gchar  **out_owner,
    gint          *out_version)
{
  const GstStructure *s;
  gpointer win_ptr = NULL;
  const gchar *owner = NULL;
  gint version = 0;

  g_return_val_if_fail (context != NULL, FALSE);

  if (!gst_context_has_context_type (context,
          GST_AHC2_CONTEXT_TYPE_SURFACE_BYPASS)) {
    return FALSE;
  }

  s = gst_context_get_structure (context);

  if (!gst_structure_get_int (s, GST_AHC2_CTX_KEY_VERSION, &version)) {
    GST_WARNING ("ahc2.surface-bypass: missing 'version' field");
    return FALSE;
  }
  if (version != GST_AHC2_CTX_PROTOCOL_VERSION) {
    GST_WARNING ("ahc2.surface-bypass: unsupported version %d (expected %d)",
        version, GST_AHC2_CTX_PROTOCOL_VERSION);
    return FALSE;
  }

  if (!gst_structure_get (s,
          GST_AHC2_CTX_KEY_WINDOW, G_TYPE_POINTER, &win_ptr, NULL)
      || win_ptr == NULL) {
    GST_WARNING ("ahc2.surface-bypass: missing or NULL 'anative-window'");
    return FALSE;
  }

  owner = gst_structure_get_string (s, GST_AHC2_CTX_KEY_OWNER);

  if (out_win)     *out_win     = (ANativeWindow *) win_ptr;
  if (out_owner)   *out_owner   = owner;
  if (out_version) *out_version = version;
  return TRUE;
}

gboolean
gst_ahc2_sink_push_bypass_context (GstElement *element, ANativeWindow *win)
{
  GstContext *ctx;
  GstMessage *msg;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);

  if (!win) {
    GST_DEBUG_OBJECT (element,
        "push_bypass_context: win is NULL — skipping");
    return FALSE;
  }

  ctx = gst_ahc2_surface_bypass_context_new (win, GST_OBJECT_NAME (element));

  GST_INFO_OBJECT (element,
      "pushing GstContext type=%s (win=%p, version=%d) — ahc2src 가 catch 해야 Mode A 활성화",
      GST_AHC2_CONTEXT_TYPE_SURFACE_BYPASS, win, GST_AHC2_CTX_PROTOCOL_VERSION);

  {
    GstElement *toplevel = element;
    while (GST_ELEMENT_PARENT (toplevel) != NULL) {
      toplevel = GST_ELEMENT_PARENT (toplevel);
    }
    gst_element_set_context (toplevel, ctx);
    GST_INFO_OBJECT (element,
        "set_context on toplevel %s (= %s) — GstBin propagates to children",
        GST_OBJECT_NAME (toplevel),
        GST_IS_PIPELINE (toplevel) ? "pipeline" : "non-pipeline bin");
  }

  msg = gst_message_new_have_context (GST_OBJECT (element), gst_context_ref (ctx));
  if (gst_element_post_message (element, msg)) {
    GST_DEBUG_OBJECT (element, "posted HAVE_CONTEXT message to bus");
  }

  gst_context_unref (ctx);

  return TRUE;
}
