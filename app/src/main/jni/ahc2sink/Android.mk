#
# Copyright (C) 2026, KIMRIHYEON <dlgus8648@naver.com>
#
# Inspired by gst-android-camera by Justin Kim, Collabora Ltd.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation
# version 2.1 of the License.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
#

AHC2SINK_DIR := $(call my-dir)

AHC2SINK_SRC_FILES := \
    $(AHC2SINK_DIR)/gstahc2sink.c \
    $(AHC2SINK_DIR)/gstahc2sinkbypass.c \
    $(AHC2SINK_DIR)/plugin.c

AHC2SINK_HEADERS := \
    $(AHC2SINK_DIR)/gstahc2sink.h \
    $(AHC2SINK_DIR)/gstahc2sinkbypass.h

AHC2SINK_C_INCLUDES := \
    $(AHC2SINK_DIR)/../common

AHC2SINK_LDLIBS := -landroid -llog

AHC2SINK_GSTREAMER_EXTRA_DEPS := \
    gstreamer-video-1.0 \
    gstreamer-base-1.0
