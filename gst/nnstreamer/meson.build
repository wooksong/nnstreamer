nnstreamer_inc = include_directories('.', './include/')
nnstreamer_sources = []
nnstreamer_headers = []

# Dependencies
nnstreamer_base_deps = [
  glib_dep,
  gmodule_dep,
  gst_dep,
  gio_dep,
  gst_base_dep,
  gst_controller_dep,
  gst_video_dep,
  gst_audio_dep,
  libm_dep,
  thread_dep
]

if orcc_support_is_available
  nnstreamer_base_deps += orc_dep
endif

if build_platform == 'tizen'
  nnstreamer_base_deps += dependency('dlog')
endif

# Internal dependencies
nnstreamer_internal_deps = []

# Add nnstreamer registerer and common sources
nnst_common_sources = [
  'registerer/nnstreamer.c',
  'hw_accel.c',
  'nnstreamer_conf.c',
  'nnstreamer_subplugin.c',
  'tensor_common.c',
  'tensor_common_pipeline.c',
  'tensor_data.c',
  'tensor_allocator.c'
]

foreach s : nnst_common_sources
  nnstreamer_sources += join_paths(meson.current_source_dir(), s)
endforeach

# Add plugins
nnst_plugins = [
  'tensor_aggregator',
  'tensor_converter',
  'tensor_crop',
  'tensor_decoder',
  'tensor_demux',
  'tensor_merge',
  'tensor_mux',
  'tensor_sink',
  'tensor_source',
  'tensor_split',
  'tensor_transform',
  'tensor_filter',
  'tensor_repo',
  'tensor_if',
  'tensor_rate',
  'tensor_query'
]

foreach p : nnst_plugins
  subdir(p)
endforeach

subdir('include')

# Private header for sub-plugins and native APIs
nnstreamer_headers += join_paths(meson.current_source_dir(), 'nnstreamer_internal.h')
nnstreamer_headers += join_paths(meson.current_source_dir(), 'nnstreamer_log.h')

# Build libraries ("both_libraries" are supported from 0.46.)
nnstreamer_shared = shared_library('nnstreamer',
  nnstreamer_sources,
  dependencies: nnstreamer_base_deps,
  include_directories: nnstreamer_inc,
  install: true,
  install_dir: plugins_install_dir
)

## @todo target name using method name() (since 0.54.0)
# target_name = nnstreamer_shared.name()
libpath_split = nnstreamer_shared.full_path().split('/')
target_name = libpath_split[libpath_split.length() - 1]

meson.add_install_script(
  'sh', '-c',
  'ln -fs @0@ @1@@2@'.format(
    join_paths(plugins_install_dir, target_name),
    '${DESTDIR}',
    join_paths(nnstreamer_libdir, target_name)))

nnstreamer_static = static_library('nnstreamer',
  nnstreamer_sources,
  dependencies: nnstreamer_base_deps,
  include_directories: nnstreamer_inc,
  install: true,
  install_dir: nnstreamer_libdir
)

nnstreamer_lib = nnstreamer_shared
if get_option('default_library') == 'static'
  nnstreamer_lib = nnstreamer_static
endif

nnstreamer_dep = declare_dependency(link_with: nnstreamer_lib,
  dependencies: nnstreamer_base_deps,
  include_directories: nnstreamer_inc)

# Install headers into /{includedir}/nnstreamer
install_headers(nnstreamer_headers,
  subdir: 'nnstreamer'
)
