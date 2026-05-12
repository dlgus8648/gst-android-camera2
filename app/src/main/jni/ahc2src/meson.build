
ahc2_sources = files(
  'gstahc2src.c',
  'gstahc2hwballocator.c',
  'gstahc2hwbpool.c',
  'plugin.c',
)

ahc2_headers = files(
  'gstahc2src.h',
  'gstahc2hwbpool.h',
  'gstahc2hwbmemory.h',
)

cc = meson.get_compiler('c')

ahc2_deps = [
  gst_dep,
  gst_base_dep,
  gst_video_dep,
  gst_gl_dep,
  gst_photography_dep,
  cc.find_library('camera2ndk'),
  cc.find_library('mediandk'),
  cc.find_library('android'),
  cc.find_library('log'),
  cc.find_library('EGL'),
  cc.find_library('GLESv2'),
]

ahc2_includes = include_directories(
  '.',
  '../common',
)

gstahc2 = shared_module('gstahc2',
  ahc2_sources,
  c_args : ['-DGST_USE_UNSTABLE_API', '-DHAVE_CONFIG_H'],
  include_directories : ahc2_includes,
  dependencies : ahc2_deps,
  install : true,
  install_dir : join_paths(get_option('libdir'), 'gstreamer-1.0'),
)
