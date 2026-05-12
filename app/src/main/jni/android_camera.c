/*
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Youness Alaoui
 *
 * Copyright (C) 2016-2017, Collabora Ltd.
 *   Author: Justin Kim <justin.kim@collabora.com>
 *
 * Copyright (C) 2026, KIMRIHYEON <dlgus8648@naver.com>
 *   - Camera2 NDK 마이그레이션 및 ahc2src 플러그인 통합
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
 *
 */

#include <stdlib.h>

#include <string.h>

#include <jni.h>

#include <android/native_window.h>

#include <android/native_window_jni.h>

#include <gst/gst.h>

#include <pthread.h>

#include <gst/video/videooverlay.h>

#include <gst/interfaces/photography.h>

#include <gst/gl/gl.h>

#include <gst/gl/egl/gstgldisplay_egl.h>

#include <gst/video/video.h>

#include "ahc2src/gstahc2src.h"

GST_PLUGIN_STATIC_DECLARE (ahc2src);

GST_DEBUG_CATEGORY_STATIC (debug_category);

#define GST_CAT_DEFAULT debug_category

#if GLIB_SIZEOF_VOID_P == 8

# define GET_CUSTOM_DATA(env, thiz, fieldID) (GstAhc *)(*env)->GetLongField (env, thiz, fieldID)
# define SET_CUSTOM_DATA(env, thiz, fieldID, data) (*env)->SetLongField (env, thiz, fieldID, (jlong)data)
#else

# define GET_CUSTOM_DATA(env, thiz, fieldID) (GstAhc *)(jint)(*env)->GetLongField (env, thiz, fieldID)
# define SET_CUSTOM_DATA(env, thiz, fieldID, data) (*env)->SetLongField (env, thiz, fieldID, (jlong)(jint)data)
#endif

typedef struct _GstAhc
{

  jobject app;

  GstElement *pipeline;

  GMainLoop *main_loop;

  ANativeWindow *native_window;

  gboolean state;

  GstElement *ahcsrc;

  GstElement *filter;

  GstElement *vsink;

  gboolean initialized;

  GstGLDisplay *gl_display;

  GstGLContext *gl_context;
} GstAhc;

static pthread_t gst_app_thread;

static pthread_key_t current_jni_env;

static JavaVM *java_vm;

static jfieldID native_android_camera_field_id;

static jmethodID on_error_method_id;

static jmethodID on_state_changed_method_id;

static jmethodID on_gstreamer_initialized_method_id;

static JNIEnv *
attach_current_thread (void)
{

  JNIEnv *env;

  JavaVMAttachArgs args;

  GST_DEBUG ("Attaching thread %p", g_thread_self ());

  args.version = JNI_VERSION_1_4;

  args.name = NULL;

  args.group = NULL;

  if ((*java_vm)->AttachCurrentThread (java_vm, &env, &args) < 0) {
    GST_ERROR ("Failed to attach current thread");
    return NULL;
  }

  return env;
}

static void
detach_current_thread (void *env)
{
  GST_DEBUG ("Detaching thread %p", g_thread_self ());
  (*java_vm)->DetachCurrentThread (java_vm);
}

static JNIEnv *
get_jni_env (void)
{
  JNIEnv *env;

  if ((env = pthread_getspecific (current_jni_env)) == NULL) {

    env = attach_current_thread ();

    pthread_setspecific (current_jni_env, env);
  }

  return env;
}

static void
on_error (GstBus * bus, GstMessage * message, GstAhc * ahc)
{

  gchar *message_string;

  GError *err;

  gchar *debug_info;

  jstring jmessage;

  JNIEnv *env = get_jni_env ();

  gst_message_parse_error (message, &err, &debug_info);

  message_string =
      g_strdup_printf ("Error received from element %s: %s",
      GST_OBJECT_NAME (message->src), err->message);

  g_clear_error (&err);

  g_free (debug_info);

  jmessage = (*env)->NewStringUTF (env, message_string);

  (*env)->CallVoidMethod (env, ahc->app, on_error_method_id, jmessage);

  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");

    (*env)->ExceptionClear (env);
  }

  (*env)->DeleteLocalRef (env, jmessage);

  g_free (message_string);

  gst_element_set_state (ahc->pipeline, GST_STATE_NULL);
}

static void
eos_cb (GstBus * bus, GstMessage * msg, GstAhc * data)
{
  gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
}

static void
state_changed_cb (GstBus * bus, GstMessage * msg, GstAhc * ahc)
{
  JNIEnv *env = get_jni_env ();

  GstState old_state, new_state, pending_state;

  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);

  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (ahc->pipeline)) {

    ahc->state = new_state;
    GST_DEBUG ("State changed to %s, notifying application",
        gst_element_state_get_name (new_state));

    (*env)->CallVoidMethod (env, ahc->app, on_state_changed_method_id,
        new_state);
    if ((*env)->ExceptionCheck (env)) {

      (*env)->ExceptionDescribe (env);
      GST_ERROR ("Failed to call Java method");
      (*env)->ExceptionClear (env);
    }
  }
}

static void
check_initialization_complete (GstAhc * data)
{
  JNIEnv *env = get_jni_env ();

  if (!data->initialized && data->native_window && data->main_loop) {
    GST_DEBUG
        ("Initialization complete, notifying application. native_window:%p main_loop:%p",
        data->native_window, data->main_loop);

    data->initialized = TRUE;

    (*env)->CallVoidMethod (env, data->app, on_gstreamer_initialized_method_id);
    if ((*env)->ExceptionCheck (env)) {
      GST_ERROR ("Failed to call Java method");
      (*env)->ExceptionClear (env);
    }
  }
}

static void *
app_function (void *userdata)
{

  JavaVMAttachArgs args;

  GstBus *bus;

  GstMessage *msg;

  GstAhc *ahc = (GstAhc *) userdata;

  GSource *bus_source;

  GMainContext *context;

  GST_DEBUG ("Creating pipeline in GstAhc at %p", ahc);

  context = g_main_context_new ();

  GST_PLUGIN_STATIC_REGISTER (ahc2src);

  ahc->ahcsrc = gst_element_factory_make ("ahc2src", "ahcsrc");

  ahc->vsink  = gst_element_factory_make ("glimagesink", "vsink");

  ahc->filter = gst_element_factory_make ("capsfilter", NULL);

  if (!ahc->ahcsrc || !ahc->vsink || !ahc->filter) {
    GST_ERROR ("A5: factory_make failed (ahc2src=%p, glimagesink=%p, capsfilter=%p)",
        ahc->ahcsrc, ahc->vsink, ahc->filter);
    return NULL;
  }

  ahc->pipeline = gst_pipeline_new ("camera-pipeline");

  gst_bin_add_many (GST_BIN (ahc->pipeline),
      ahc->ahcsrc, ahc->filter, ahc->vsink, NULL);

  if (!gst_element_link_many (ahc->ahcsrc, ahc->filter, ahc->vsink, NULL)) {
    GST_ERROR ("A5: link_many failed (caps negotiation problem?)");
    return NULL;
  }

  ahc->gl_display = (GstGLDisplay *) gst_gl_display_egl_new ();
  if (ahc->gl_display) {

    ahc->gl_context = gst_gl_context_new (ahc->gl_display);
    GError *err = NULL;

    if (!gst_gl_context_create (ahc->gl_context, NULL, &err)) {
      GST_WARNING ("gst_gl_context_create failed: %s — GL path 비활성",
          err ? err->message : "unknown");
      g_clear_error (&err);

      gst_clear_object (&ahc->gl_context);
      gst_clear_object (&ahc->gl_display);
    } else {

      GstContext *display_ctx = gst_context_new (
          GST_GL_DISPLAY_CONTEXT_TYPE, TRUE);
      gst_context_set_gl_display (display_ctx, ahc->gl_display);

      gst_element_set_context (ahc->pipeline, display_ctx);
      gst_context_unref (display_ctx);

      GstContext *app_ctx = gst_context_new ("gst.gl.app_context", TRUE);
      GstStructure *cs = gst_context_writable_structure (app_ctx);
      gst_structure_set (cs, "context", GST_TYPE_GL_CONTEXT,
          ahc->gl_context, NULL);
      gst_element_set_context (ahc->pipeline, app_ctx);
      gst_context_unref (app_ctx);

      GST_INFO ("옵션 B: GstGLDisplay/Context push to pipeline (display=%p, ctx=%p)",
          ahc->gl_display, ahc->gl_context);
    }
  } else {
    GST_WARNING ("gst_gl_display_egl_new failed — GL path 비활성, raw fallback");
  }

  if (ahc->native_window) {
    GST_DEBUG ("Native window already received, notifying the vsink about it.");
    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (ahc->vsink),
        (guintptr) ahc->native_window);
  }

  bus = gst_element_get_bus (ahc->pipeline);

  bus_source = gst_bus_create_watch (bus);

  g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func,
      NULL, NULL);

  g_source_attach (bus_source, context);

  g_source_unref (bus_source);

  g_signal_connect (G_OBJECT (bus), "message::error", G_CALLBACK (on_error),
      ahc);

  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback) eos_cb, ahc);

  g_signal_connect (G_OBJECT (bus), "message::state-changed",
      (GCallback) state_changed_cb, ahc);

  gst_object_unref (bus);

  GST_DEBUG ("Entering main loop... (GstAhc:%p)", ahc);

  ahc->main_loop = g_main_loop_new (context, FALSE);

  check_initialization_complete (ahc);

  g_main_loop_run (ahc->main_loop);
  GST_DEBUG ("Exited main loop");

  g_main_loop_unref (ahc->main_loop);

  ahc->main_loop = NULL;

  g_main_context_unref (context);

  gst_element_set_state (ahc->pipeline, GST_STATE_NULL);

  if (ahc->vsink)  gst_object_unref (ahc->vsink);
  if (ahc->filter) gst_object_unref (ahc->filter);
  if (ahc->ahcsrc) gst_object_unref (ahc->ahcsrc);

  gst_object_unref (ahc->pipeline);

  gst_clear_object (&ahc->gl_context);
  gst_clear_object (&ahc->gl_display);

  return NULL;
}

void
gst_native_init (JNIEnv * env, jobject thiz)
{

  GstAhc *data = (GstAhc *) g_malloc0 (sizeof (GstAhc));

  SET_CUSTOM_DATA (env, thiz, native_android_camera_field_id, data);
  GST_DEBUG ("Created GstAhc at %p", data);

  data->app = (*env)->NewGlobalRef (env, thiz);
  GST_DEBUG ("Created GlobalRef for app object at %p", data->app);

  pthread_create (&gst_app_thread, NULL, &app_function, data);
}

void
gst_native_finalize (JNIEnv * env, jobject thiz)
{
  GstAhc *data = GET_CUSTOM_DATA (env, thiz, native_android_camera_field_id);

  if (!data)
    return;
  GST_DEBUG ("Quitting main loop...");

  g_main_loop_quit (data->main_loop);
  GST_DEBUG ("Waiting for thread to finish...");

  pthread_join (gst_app_thread, NULL);
  GST_DEBUG ("Deleting GlobalRef at %p", data->app);

  (*env)->DeleteGlobalRef (env, data->app);
  GST_DEBUG ("Freeing GstAhc at %p", data);

  g_free (data);

  SET_CUSTOM_DATA (env, thiz, native_android_camera_field_id, NULL);
  GST_DEBUG ("Done finalizing");
}

void
gst_native_play (JNIEnv * env, jobject thiz)
{
  GstAhc *data = GET_CUSTOM_DATA (env, thiz, native_android_camera_field_id);

  if (!data)
    return;
  GST_DEBUG ("Setting state to PLAYING");
  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
}

void
gst_native_pause (JNIEnv * env, jobject thiz)
{
  GstAhc *data = GET_CUSTOM_DATA (env, thiz, native_android_camera_field_id);

  if (!data)
    return;
  GST_DEBUG ("Setting state to PAUSED");
  gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
}

jboolean
gst_class_init (JNIEnv * env, jclass klass)
{

  native_android_camera_field_id =
      (*env)->GetFieldID (env, klass, "native_custom_data", "J");
  GST_DEBUG ("The FieldID for the native_custom_data field is %p",
      native_android_camera_field_id);

  on_error_method_id =
      (*env)->GetMethodID (env, klass, "onError", "(Ljava/lang/String;)V");
  GST_DEBUG ("The MethodID for the onError method is %p", on_error_method_id);

  on_gstreamer_initialized_method_id =
      (*env)->GetMethodID (env, klass, "onGStreamerInitialized", "()V");
  GST_DEBUG ("The MethodID for the onGStreamerInitialized method is %p",
      on_gstreamer_initialized_method_id);

  on_state_changed_method_id =
      (*env)->GetMethodID (env, klass, "onStateChanged", "(I)V");
  GST_DEBUG ("The MethodID for the onStateChanged method is %p",
      on_state_changed_method_id);

  if (!native_android_camera_field_id || !on_error_method_id ||
      !on_gstreamer_initialized_method_id || !on_state_changed_method_id) {
    GST_ERROR
        ("The calling class does not implement all necessary interface methods");
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

void
gst_native_surface_init (JNIEnv * env, jobject thiz, jobject surface)
{
  GstAhc *ahc = GET_CUSTOM_DATA (env, thiz, native_android_camera_field_id);

  if (!ahc)
    return;

  GST_DEBUG ("Received surface %p", surface);

  if (ahc->native_window) {
    GST_DEBUG ("Releasing previous native window %p", ahc->native_window);
    ANativeWindow_release (ahc->native_window);
  }

  ahc->native_window = ANativeWindow_fromSurface (env, surface);
  GST_DEBUG ("Got Native Window %p", ahc->native_window);

  if (ahc->vsink) {
    GST_DEBUG
        ("Pipeline already created, notifying the vsink about the native window.");
    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (ahc->vsink),
        (guintptr) ahc->native_window);
  } else {
    GST_DEBUG
        ("Pipeline not created yet, vsink will later be notified about the native window.");
  }

  check_initialization_complete (ahc);
}

void
gst_native_surface_finalize (JNIEnv * env, jobject thiz)
{
  GstAhc *data = GET_CUSTOM_DATA (env, thiz, native_android_camera_field_id);

  if (!data) {
    GST_WARNING ("Received surface finalize but there is no GstAhc. Ignoring.");
    return;
  }
  GST_DEBUG ("Releasing Native Window %p", data->native_window);

  ANativeWindow_release (data->native_window);
  data->native_window = NULL;

  if (data->vsink)

    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (data->vsink),
        (guintptr) NULL);
}

void
gst_native_change_resolution (JNIEnv * env, jobject thiz, jint width, jint height)
{
  GstCaps *new_caps;
  GstAhc *ahc = GET_CUSTOM_DATA (env, thiz, native_android_camera_field_id);

  if (!ahc)
    return;

  if (!ahc->filter) {
    GST_INFO ("change_resolution(%d,%d) ignored — capsfilter inactive at A1",
        width, height);
    return;
  }

  // GL zero-copy 경로 유지: format=RGBA + memory:GLMemory feature 명시.
  // 이를 빼면 capsfilter가 SystemMemory(NV12 software) 경로로 fallback되어
  // 카메라 ISP 변환 대신 glimagesink가 자체 색공간 변환을 하게 되고,
  // 이때 color matrix(BT.601 vs BT.709) 차이로 색감이 달라짐.
  new_caps = gst_caps_new_simple ("video/x-raw",
      "format",         G_TYPE_STRING,    "RGBA",
      "width",          G_TYPE_INT,       width,
      "height",         G_TYPE_INT,       height,
      "framerate",      GST_TYPE_FRACTION, 30, 1,
      "texture-target", G_TYPE_STRING,    "external-oes",
      NULL);
  GstCapsFeatures *feat = gst_caps_features_new_static_str (
      GST_CAPS_FEATURE_MEMORY_GL_MEMORY, NULL);
  gst_caps_set_features (new_caps, 0, feat);

  // capsfilter의 caps 속성 업데이트만으로 reconfigure 이벤트가 upstream으로
  // 전파됨. State 전환(특히 READY)은 GL context를 일시적으로 무너뜨려
  // 재협상이 not-negotiated로 실패하므로 여기선 하지 않는다.
  // 호출 측(Java GstAhc.changeResolutionTo)에서 nativePause/nativePlay로
  // 파이프라인 상태를 감싸므로, ahc2src.set_caps는 PAUSED 상태에서
  // 안전하게 capture session을 재구성한다.
  g_object_set (ahc->filter,
      "caps", new_caps,
      NULL);

  gst_caps_unref (new_caps);
}

void
gst_native_set_white_balance (JNIEnv * env, jobject thiz, jint wb_mode)
{
  GstAhc *ahc = GET_CUSTOM_DATA (env, thiz, native_android_camera_field_id);

  if (!ahc)
    return;

  if (!ahc->ahcsrc || !GST_IS_PHOTOGRAPHY (ahc->ahcsrc)) {
    GST_INFO ("set_white_balance(%d) ignored — photography iface inactive (Step P 에서 활성화)",
        wb_mode);
    return;
  }

  GST_DEBUG ("Setting WB_MODE (%d)", wb_mode);

  g_object_set (ahc->ahcsrc, GST_PHOTOGRAPHY_PROP_WB_MODE, wb_mode, NULL);
}

void
gst_native_set_auto_focus (JNIEnv * env, jobject thiz, jboolean enabled)
{
  GstAhc *ahc = GET_CUSTOM_DATA (env, thiz, native_android_camera_field_id);

  if (!ahc)
    return;

  if (!ahc->ahcsrc || !GST_IS_PHOTOGRAPHY (ahc->ahcsrc)) {
    GST_INFO ("set_auto_focus(%d) ignored — photography iface inactive (Step P 에서 활성화)",
        enabled);
    return;
  }

  GST_DEBUG ("Setting Autofocus (%d)", enabled);

  gst_photography_set_autofocus (GST_PHOTOGRAPHY (ahc->ahcsrc), enabled);
}

void
gst_native_set_rotate_method (JNIEnv * env, jobject thiz, jint method)
{
  GstAhc *ahc = GET_CUSTOM_DATA (env, thiz, native_android_camera_field_id);

  if (!ahc)
    return;

  if (!ahc->vsink) {
    GST_INFO ("set_rotate_method(%d) ignored — vsink inactive at A1", method);
    return;
  }

  g_object_set (ahc->vsink, "rotate-method", GST_VIDEO_ORIENTATION_90R, NULL);
}

static JNINativeMethod native_methods[] = {

  {"nativeInit", "()V", (void *) gst_native_init},

  {"nativeFinalize", "()V", (void *) gst_native_finalize},

  {"nativePlay", "()V", (void *) gst_native_play},

  {"nativePause", "()V", (void *) gst_native_pause},

  {"nativeClassInit", "()Z", (void *) gst_class_init},

  {"nativeSurfaceInit", "(Ljava/lang/Object;)V",
      (void *) gst_native_surface_init},

  {"nativeSurfaceFinalize", "()V",
      (void *) gst_native_surface_finalize},

  {"nativeChangeResolution", "(II)V",
      (void *) gst_native_change_resolution},

  {"nativeSetRotateMethod", "(I)V",
      (void *) gst_native_set_rotate_method},

  {"nativeSetWhiteBalance", "(I)V",
      (void *) gst_native_set_white_balance},

  {"nativeSetAutoFocus", "(Z)V",
      (void *) gst_native_set_auto_focus}
};

jint
JNI_OnLoad (JavaVM * vm, void *reserved)
{
  JNIEnv *env = NULL;

  setenv ("GST_DEBUG", "*:4,ahc:5,camera-test:5,ahcsrc:5,ahc2src:5", 1);

  GST_DEBUG_CATEGORY_INIT (debug_category, "camera-test", 0,
      "Android Gstreamer Camera test");

  java_vm = vm;

  if ((*vm)->GetEnv (vm, (void **) &env, JNI_VERSION_1_4) != JNI_OK) {
    GST_ERROR ("Could not retrieve JNIEnv");

    return 0;
  }

  jclass klass =
      (*env)->FindClass (env,
      "org/freedesktop/gstreamer/camera/GstAhc");

  (*env)->RegisterNatives (env, klass, native_methods,
      G_N_ELEMENTS (native_methods));

  pthread_key_create (&current_jni_env, detach_current_thread);

  return JNI_VERSION_1_4;
}
