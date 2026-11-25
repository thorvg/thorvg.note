project(
  'ThorNote',
  'cpp',
  version: '0',
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
      'buildtype=' + get_option('buildtype'),
      'optimization=' + get_option('optimization'),
      'b_ndebug=' + get_option('b_ndebug'),
      'engines=gl',
      'loaders=svg,lottie,ttf,png,jpg,webp,gif',
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

clang_tidy = find_program('clang-tidy', native: true, version: '>=19', required: false)
run_clang_tidy = find_program('run-clang-tidy', 'run-clang-tidy.py', native: true, required: false)
if clang_tidy.found() and run_clang_tidy.found()
  tidy_command = [run_clang_tidy]
  if target_platform == 'macos' and not meson.is_cross_build()
    tidy_command = [find_program('xcrun', native: true), run_clang_tidy.full_path()]
  endif

  run_target('clang-tidy', command: [
    find_program('python3', native: true),
    files('tools/run_clang_tidy.py'),
    meson.project_source_root(),
  ] + tidy_command + [
    '-clang-tidy-binary', clang_tidy.full_path(),
    '-p', meson.project_build_root(),
    '-quiet',
  ])
endif
