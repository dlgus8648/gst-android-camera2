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

#include "gstahc2src.h"

static gboolean
plugin_init (GstPlugin * plugin)
{

  return gst_element_register (plugin, "ahc2src",
      GST_RANK_NONE, GST_TYPE_AHC2_SRC);
}

#ifndef PACKAGE
#define PACKAGE "ahc2src"
#endif

GST_PLUGIN_DEFINE (

    GST_VERSION_MAJOR,

    GST_VERSION_MINOR,

    ahc2src,

    "Android Camera2 NDK GStreamer source",

    plugin_init,

    "0.1",

    "LGPL",

    "ahc2src",

    "https://github.com/dlgus8648/gst-ahc2")
