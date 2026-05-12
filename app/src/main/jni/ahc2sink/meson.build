
ahc2sink_sources = files(
  'gstahc2sink.c',
  'gstahc2sinkbypass.c',
  'plugin.c',
)

ahc2sink_headers = files(
  'gstahc2sink.h',
  'gstahc2sinkbypass.h',
)

ahc2sink_includes = include_directories(
  '.',
  '../common',
)

cc = meson.get_compiler('c')

ahc2sink_deps = [
  gst_dep,
  gst_base_dep,
  gst_video_dep,
  cc.find_library('android'),
  cc.find_library('log'),
]

gstahc2sink = shared_module('gstahc2sink',
  ahc2sink_sources,
  c_args : ['-DGST_USE_UNSTABLE_API'],
  include_directories : ahc2sink_includes,
  dependencies : ahc2sink_deps,
  install : true,
  install_dir : join_paths(get_option('libdir'), 'gstreamer-1.0'),
)
