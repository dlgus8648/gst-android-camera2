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

#ifndef __GST_AHC2_HWB_MEMORY_H__
#define __GST_AHC2_HWB_MEMORY_H__

#include <gst/gst.h>

#include <gst/allocators/allocators.h>

#include <android/hardware_buffer.h>

#include <media/NdkImage.h>

G_BEGIN_DECLS

#define GST_TYPE_AHC2_HWB_ALLOCATOR        (gst_ahc2_hwb_allocator_get_type ())

#define GST_AHC2_HWB_ALLOCATOR(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AHC2_HWB_ALLOCATOR, GstAhc2HwbAllocator))

#define GST_IS_AHC2_HWB_ALLOCATOR(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_AHC2_HWB_ALLOCATOR))

#define GST_AHC2_HWB_ALLOCATOR_NAME "AHC2HwbAllocator"

typedef struct _GstAhc2HwbAllocator      GstAhc2HwbAllocator;

typedef struct _GstAhc2HwbAllocatorClass GstAhc2HwbAllocatorClass;

typedef struct _GstAhc2HwbMemory         GstAhc2HwbMemory;

struct _GstAhc2HwbAllocator
{

  GstAllocator parent;
};

struct _GstAhc2HwbAllocatorClass
{

  GstAllocatorClass parent_class;
};

struct _GstAhc2HwbMemory
{

  GstMemory        parent;
  AHardwareBuffer *ahb;
  AImage          *aimg_backref;
  gboolean         owns_aimg;

  gpointer         egl_image;
  gpointer         egl_display;
};

GType                 gst_ahc2_hwb_allocator_get_type (void);

GstAllocator        * gst_ahc2_hwb_allocator_new (void);

GstAhc2HwbMemory   * gst_ahc2_hwb_memory_new_from_aimage (
    GstAllocator * allocator, AImage * aimg, gsize size);

G_END_DECLS

#endif
