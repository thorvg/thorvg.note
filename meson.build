project(
  'ThorNote',
  'cpp',
  version: '0.1.0',
  meson_version: '>=1.2',
  default_options: [
    'buildtype=debugoptimized',
    'b_sanitize=none',
    'werror=false',
    'optimization=3',
    'cpp_std=c++20',
    'warning_level=2',
    'default_library=static',
  ],
)

target_platform = host_machine.system()
if target_platform == 'darwin'
  target_platform = 'macos'
endif

src_inc = include_directories('src')
if target_platform == 'emscripten'
  # WebAssembly has no system packages; ThorVG comes from its wrap.
  thorvg = subproject(
    'thorvg',
    default_options: [
      'engines=cpu',
      'loaders=svg,lottie,ttf,png,jpg',
      'threads=false',
      'extra=',
      'static=true',
    ],
  )
  # The module headers are installed with ThorVG but missing from its subproject dependency.
  thorvg_dep = declare_dependency(
    dependencies: thorvg.get_variable('thorvg_dep'),
    include_directories: thorvg.get_variable('headers'),
  )
else
  thorvg_dep = dependency('thorvg-1', version: '>=1.2.0')
endif
subdir('src')
if target_platform != 'emscripten'
  subdir('tests')
endif
