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

#ifndef __GST_AHC2_HWB_POOL_H__
#define __GST_AHC2_HWB_POOL_H__

#include <gst/gst.h>

#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_AHC2_HWB_POOL  (gst_ahc2_hwb_pool_get_type ())

G_DECLARE_FINAL_TYPE (GstAhc2HwbPool, gst_ahc2_hwb_pool,
    GST, AHC2_HWB_POOL, GstBufferPool)

GstBufferPool * gst_ahc2_hwb_pool_new (void);

G_END_DECLS

#endif
