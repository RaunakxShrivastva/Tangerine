project('tangerine', 'c',
  version: '0.1.0',
  default_options: [
    'warning_level=2',
    'werror=true',
    'c_std=c11',
  ])

# Get C compiler
c_compiler = meson.get_compiler('c')

# Dependencies
gtk4_dep = dependency('gtk4', version: '>=4.12')
libadwaita_dep = dependency('libadwaita-1', version: '>=1.4')
gstreamer_dep = dependency('gstreamer-1.0', version: '>=1.22')
gst_video_dep = dependency('gstreamer-video-1.0')
ffmpeg_dep = dependency('libavformat', version: '>=58')
libvips_dep = dependency('vips', version: '>=8.14')
poppler_dep = dependency('poppler-glib', version: '>=23.0')
libarchive_dep = dependency('libarchive', version: '>=3.6')
yaml_dep = dependency('yaml-0.1')

# Flatpak portal (optional)
portal_dep = dependency('xdg-desktop-portal-1', required: false)

deps = [
  gtk4_dep, libadwaita_dep, gstreamer_dep, gst_video_dep,
  ffmpeg_dep, libvips_dep, poppler_dep, libarchive_dep,
  yaml_dep
]
if portal_dep.found()
  deps += portal_dep
endif

# Include directories
inc = include_directories('src')

# Math library for sqrt
m_lib = c_compiler.find_library('m', required: false)
if not m_lib.found()
  m_lib = c_compiler.find_library('m', dirs: ['/usr/lib', '/usr/lib64'], required: false)
endif
if not m_lib.found()
  m_lib = c_compiler.find_library('m', dirs: ['/lib', '/lib64'], required: false)
endif

# Executable
executable('tangerine',
  sources: [
    'src/main.c',
    'src/radial-menu.c',
    'src/drag-monitor.c',
    'src/conversion-engine.c',
  ],
  dependencies: deps + [m_lib],
  include_directories: inc,
  install: true,
  c_args: ['-Wno-deprecated-declarations']
)

# Tests (disabled for now - need library linking)
# test('radial-menu', executable('test-radial-menu', 'src/tests/test-radial-menu.c', dependencies: deps, include_directories: inc, c_args: ['-Wno-deprecated-declarations'], link_with: tangerine_lib))

# Install data
install_data('data/com.github.yourname.Tangerine.gschema.xml',
  install_dir: join_paths(get_option('datadir'), 'glib-2.0', 'schemas'))