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

# Shared application library
tangerine_lib = static_library('tangerine-lib',
  sources: [
    'src/radial-menu.c',
    'src/drag-monitor.c',
    'src/conversion-engine.c',
    'src/file-operations.c',
    'src/preferences.c',
  ],
  dependencies: deps + [m_lib],
  include_directories: inc,
  c_args: ['-Wno-deprecated-declarations']
)

# Executable
executable('tangerine',
  sources: ['src/main.c'],
  dependencies: deps + [m_lib],
  include_directories: inc,
  link_with: tangerine_lib,
  install: true,
  c_args: ['-Wno-deprecated-declarations']
)

# Tests
gdk_pixbuf_dep = dependency('gdk-pixbuf-2.0')
conversion_test = executable('test-conversion',
  sources: ['src/tests/test-conversion.c'],
  dependencies: deps + [m_lib, gdk_pixbuf_dep],
  include_directories: inc,
  link_with: tangerine_lib,
  c_args: ['-Wno-deprecated-declarations']
)
test('conversion', conversion_test,
  timeout: 60,
  env: ['GSETTINGS_SCHEMA_DIR=' + meson.current_build_dir()])

# Install data
datadir = get_option('datadir')

install_data('data/com.github.yourname.Tangerine.gschema.xml',
  install_dir: join_paths(datadir, 'glib-2.0', 'schemas'))

install_data('src/formats.yaml',
  install_dir: join_paths(datadir, 'tangerine'))

install_data('data/com.github.yourname.Tangerine.desktop',
  install_dir: join_paths(datadir, 'applications'))

# File manager integrations
install_data('data/nautilus/tangerine-convert',
  install_dir: join_paths(datadir, 'nautilus', 'scripts'),
  install_mode: 'rwxr-xr-x')

install_data('data/kio/tangerine-servicemenu.desktop',
  install_dir: join_paths(datadir, 'kio', 'servicemenus'))

# Compile GSettings schema into builddir so `meson test` and dev runs work
# with GSETTINGS_SCHEMA_DIR=builddir.
glib_compile_schemas = find_program('glib-compile-schemas', required: false)
if glib_compile_schemas.found()
  custom_target('gschemas-compiled',
    output: 'gschemas.compiled',
    input: 'data/com.github.yourname.Tangerine.gschema.xml',
    command: [glib_compile_schemas, '--targetdir', meson.current_build_dir(),
      meson.current_source_dir() / 'data'],
    build_by_default: true)
endif

