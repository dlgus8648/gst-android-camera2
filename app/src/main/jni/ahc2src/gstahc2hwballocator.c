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

#include "gstahc2hwbmemory.h"

#include <media/NdkImage.h>

#ifndef EGL_EGLEXT_PROTOTYPES
#define EGL_EGLEXT_PROTOTYPES 1
#endif

#include <EGL/egl.h>

#include <EGL/eglext.h>

GST_DEBUG_CATEGORY_STATIC (gst_ahc2_hwb_alloc_debug);

#define GST_CAT_DEFAULT gst_ahc2_hwb_alloc_debug

G_DEFINE_TYPE (GstAhc2HwbAllocator, gst_ahc2_hwb_allocator, GST_TYPE_ALLOCATOR)

static volatile gint g_acq_count = 0;

static volatile gint g_rel_count = 0;

static gpointer
ahc2_hwb_mem_map (GstMemory * gmem, gsize maxsize, GstMapFlags flags)
{

  GstAhc2HwbMemory *m = (GstAhc2HwbMemory *) gmem;

  if (flags & GST_MAP_WRITE) {
    GST_WARNING ("ahc2hwbmem: WRITE map rejected (READONLY)");
    return NULL;
  }

  if (!m->aimg_backref) {
    GST_ERROR ("ahc2hwbmem: map without aimg_backref (A8 path?)");
    return NULL;
  }

  uint8_t *yp = NULL;
  int      ylen = 0;

  if (AImage_getPlaneData (m->aimg_backref, 0, &yp, &ylen) != AMEDIA_OK) {
    GST_ERROR ("ahc2hwbmem: AImage_getPlaneData(0) failed");
    return NULL;
  }
  return yp;
}

static void
ahc2_hwb_mem_unmap (GstMemory * gmem)
{

}

static GstMemory *
ahc2_hwb_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  GST_WARNING ("ahc2hwbAllocator->alloc(): not implemented in A7 option-A "
      "(자체 AHardwareBuffer_allocate 는 A8). size=%" G_GSIZE_FORMAT, size);
  return NULL;
}

static void
ahc2_hwb_allocator_free (GstAllocator * allocator, GstMemory * gmem)
{

  GstAhc2HwbMemory *m = (GstAhc2HwbMemory *) gmem;
  if (!m) return;

  if (m->egl_image && m->egl_display) {

    eglDestroyImageKHR ((EGLDisplay) m->egl_display, (EGLImageKHR) m->egl_image);
    m->egl_image = NULL;
    m->egl_display = NULL;
  }
  if (m->ahb) {

    AHardwareBuffer_release (m->ahb);

    g_atomic_int_inc (&g_rel_count);
    m->ahb = NULL;
  }
  if (m->owns_aimg && m->aimg_backref) {

    AImage_delete (m->aimg_backref);
    m->aimg_backref = NULL;
  }

  g_free (m);
}

static void
gst_ahc2_hwb_allocator_class_init (GstAhc2HwbAllocatorClass * klass)
{

  GstAllocatorClass *ac = GST_ALLOCATOR_CLASS (klass);

  ac->alloc = ahc2_hwb_allocator_alloc;

  ac->free  = ahc2_hwb_allocator_free;

  GST_DEBUG_CATEGORY_INIT (gst_ahc2_hwb_alloc_debug, "ahc2hwballoc", 0,
      "ahc2src AHardwareBuffer allocator");
}

static void
gst_ahc2_hwb_allocator_init (GstAhc2HwbAllocator * self)
{

  GstAllocator *a = GST_ALLOCATOR_CAST (self);

  a->mem_type   = GST_AHC2_HWB_ALLOCATOR_NAME;

  a->mem_map    = ahc2_hwb_mem_map;

  a->mem_unmap  = ahc2_hwb_mem_unmap;

  GST_OBJECT_FLAG_SET (a, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

GstAllocator *
gst_ahc2_hwb_allocator_new (void)
{

  return g_object_new (GST_TYPE_AHC2_HWB_ALLOCATOR, NULL);
}

GstAhc2HwbMemory *
gst_ahc2_hwb_memory_new_from_aimage (GstAllocator * allocator,
    AImage * aimg, gsize size)
{

  if (!aimg) return NULL;

  AHardwareBuffer *ahb = NULL;

  media_status_t ms = AImage_getHardwareBuffer (aimg, &ahb);
  if (ms != AMEDIA_OK || !ahb) {
    GST_ERROR ("AImage_getHardwareBuffer failed ms=%d", ms);
    return NULL;
  }

  AHardwareBuffer_acquire (ahb);

  g_atomic_int_inc (&g_acq_count);

  GstAhc2HwbMemory *m = g_new0 (GstAhc2HwbMemory, 1);

  m->ahb          = ahb;

  m->aimg_backref = aimg;

  m->owns_aimg    = TRUE;

  gst_memory_init (&m->parent,
      GST_MEMORY_FLAG_READONLY | GST_MEMORY_FLAG_NO_SHARE,
      allocator,
       NULL,
       size,
         0,
        0,
          size);

  return m;
}

G_GNUC_UNUSED gint
gst_ahc2_hwb_get_acq_count (void) { return g_atomic_int_get (&g_acq_count); }
G_GNUC_UNUSED gint
gst_ahc2_hwb_get_rel_count (void) { return g_atomic_int_get (&g_rel_count); }
