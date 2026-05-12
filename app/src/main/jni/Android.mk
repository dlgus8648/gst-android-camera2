#
# Copyright (C) 2012, Collabora Ltd.
#   Author: Youness Alaoui
#
# Copyright (C) 2016-2017, Collabora Ltd.
#   Author: Justin Kim <justin.kim@collabora.com>
#
# Copyright (C) 2026, KIMRIHYEON <dlgus8648@naver.com>
#   - Camera2 NDK 마이그레이션 및 ahc2src 플러그인 통합
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

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_CFLAGS    := -DGST_USE_UNSTABLE_API
LOCAL_MODULE    := android_camera
# A1: ahc2src 플러그인 소스 파일 추가
# A6: gstahc2hwbpool.c 추가 (GstBufferPool)
# A7: gstahc2hwballocator.c 추가 (AHardwareBuffer 기반 GstAllocator, AImage path)
LOCAL_SRC_FILES := android_camera.c dummy.cpp \
                   ahc2src/gstahc2src.c \
                   ahc2src/gstahc2hwbpool.c \
                   ahc2src/gstahc2hwballocator.c \
                   ahc2src/plugin.c
# common/gstahc2protocol.h 헤더 사용을 위한 include path
LOCAL_C_INCLUDES := $(LOCAL_PATH)/common
LOCAL_SHARED_LIBRARIES := gstreamer_android
# A2: Camera2 NDK + Media NDK 링크
# A8.1: EGL/GLESv2 명시 링크 (eglGetNativeClientBufferANDROID, eglCreateImageKHR)
LOCAL_LDLIBS    := -llog -landroid -lcamera2ndk -lmediandk -lEGL -lGLESv2

include $(BUILD_SHARED_LIBRARY)

ifeq ($(TARGET_ARCH_ABI),armeabi)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/arm
else ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/armv7
else ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/arm64
else ifeq ($(TARGET_ARCH_ABI),x86)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/x86
else ifeq ($(TARGET_ARCH_ABI),x86_64)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/x86_64
else
$(error Target arch ABI not supported: $(TARGET_ARCH_ABI))
endif

GSTREAMER_NDK_BUILD_PATH  := $(GSTREAMER_ROOT)/share/gst-android/ndk-build/

include $(GSTREAMER_NDK_BUILD_PATH)/plugins.mk
GSTREAMER_PLUGINS         := $(GSTREAMER_PLUGINS_CORE) \
                             $(GSTREAMER_PLUGINS_PLAYBACK) \
                             $(GSTREAMER_PLUGINS_CODECS) \
                             opengl

# Needed for new versions of gstreamer
# A1: ahc2src 가 GstPushSrc 사용하므로 gstreamer-base-1.0 명시 추가
# A8.1: gstreamer-gl-1.0 추가 (egl 모듈은 gstreamer-gl-1.0.pc 안에 포함됨 —
#       gstreamer-gl-egl-1.0 이라는 별도 패키지는 존재하지 않음)
GSTREAMER_EXTRA_DEPS      := gstreamer-video-1.0 gstreamer-photography-1.0 \
                             gstreamer-base-1.0 \
                             gstreamer-gl-1.0

include $(GSTREAMER_NDK_BUILD_PATH)/gstreamer-1.0.mk
