if target_platform == 'emscripten'
  cmake = import('cmake')
  sdl_options = cmake.subproject_options()
  sdl_options.add_cmake_defines({
    'SDL_SHARED': false,
    'SDL_STATIC': true,
    'SDL_INSTALL': false,
    'SDL_TEST_LIBRARY': false,
    'SDL_TESTS': false,
    'SDL_AUDIO': false,
    'SDL_VIDEO': true,
    'SDL_GPU': false,
    'SDL_RENDER': false,
    'SDL_CAMERA': false,
    'SDL_JOYSTICK': false,
    'SDL_HAPTIC': false,
    'SDL_HIDAPI': false,
    'SDL_SENSOR': false,
    'SDL_OPENGL': false,
    'SDL_OPENGLES': false,
    'SDL_PTHREADS': false,
    'CMAKE_C_FLAGS_RELEASE': '-Os -DNDEBUG',
  })
  sdl_options.set_install(false)
  sdl = cmake.subproject('sdl3', options: sdl_options)
  sdl_dep = sdl.dependency('SDL3-static')
else
  sdl_dep = dependency('sdl3', version: '>=3.2.0')
endif
app_id = 'org.thorvg.ThorNote'
app_icon = meson.project_source_root() / 'resource/icon.svg'
identity_data = configuration_data()
identity_data.set('THORNOTE_APP_ID', app_id)
identity_data.set('THORNOTE_APP_VERSION', meson.project_version())
identity_data.set('THORNOTE_ICON_SVG', import('fs').read(app_icon))
identity_data.set('THORVG_SYMBOL_SVG', import('fs').read(meson.project_source_root() / 'resource/brand/thorvg-symbol.svg'))

thornote_sources = files('path.h')
thornote_deps = [ui_dep, storage_dep, sdl_dep]
thornote_data = get_option('datadir') / 'thornote'
thornote_args = [
  '-DTHORNOTE_SOURCE_DATA="' + meson.current_source_dir() / 'data' + '"',
  '-DTHORNOTE_INSTALL_DATA="' + get_option('prefix') / thornote_data + '"',
]
thornote_link_args = []
thornote_link_depends = []
subdir('app')
subdir('documents')
subdir('preferences')
subdir('scripting')
subdir('extensions')
subdir('views')
subdir('platform')

thornote_app = static_library(
  'thornote-app',
  thornote_sources,
  include_directories: src_inc,
  dependencies: thornote_deps,
  cpp_args: thornote_args,
  objcpp_args: ['-std=c++20', '-fobjc-arc'],
)
thornote_app_dep = declare_dependency(
  link_with: thornote_app,
  include_directories: src_inc,
  dependencies: thornote_deps,
  compile_args: thornote_args,
)

thornote_runtime_paths = []
foreach runtime_dep: [thorvg_dep, sdl_dep, lua_dep]
  runtime_path = runtime_dep.get_variable(pkgconfig: 'libdir', default_value: '')
  if runtime_path != '' and runtime_path not in thornote_runtime_paths
    thornote_runtime_paths += runtime_path
  endif
endforeach

if target_platform == 'emscripten'
  thornote_link_args += [
    '-sALLOW_MEMORY_GROWTH=1',
    '-sSTACK_SIZE=1048576',
    '-sENVIRONMENT=web',
    '-sEXPORTED_RUNTIME_METHODS=FS,ENV',
    '--embed-file', meson.current_source_dir() / 'data/assets@/assets',
    '--embed-file', meson.current_source_dir() / 'data/layouts@/layouts',
  ]
  thornote_link_depends += files('data/layouts/titlebar.lua', 'data/layouts/workspace.lua')
  # The optimization level also selects the optimized runtime and the post-link passes.
  if get_option('optimization') in ['1', '2', '3', 's']
    thornote_link_args += '-O' + get_option('optimization')
  endif
  if get_option('optimization') == 's'
    thornote_link_args += '-sBINARYEN_EXTRA_PASSES=gufa-optimizing,merge-similar-functions,--converge'
  endif
endif

thornote_main = files('main.cpp')
thornote = executable(
  'ThorNote',
  thornote_main,
  dependencies: thornote_app_dep,
  link_args: thornote_link_args,
  link_depends: thornote_link_depends,
  install: true,
  install_rpath: ':'.join(thornote_runtime_paths),
)
thornote_fonts = files(
  'data/assets/Pretendard-Regular.ttf',
  'data/assets/Pretendard-Bold.ttf',
  'data/assets/Pretendard.LICENSE',
  'data/assets/DejaVuSansMono.ttf',
  'data/assets/DejaVuSans.LICENSE',
)
thornote_layouts = files('data/layouts/workspace.lua', 'data/layouts/titlebar.lua')
install_data(thornote_fonts, install_dir: thornote_data / 'assets', install_tag: 'runtime')
install_data(thornote_layouts, install_dir: thornote_data / 'layouts', install_tag: 'runtime')
install_symlink(
  'tnote',
  pointing_to: 'ThorNote',
  install_dir: get_option('bindir'),
)

if target_platform == 'macos'
  subdir('platform/macos/bundle')
endif
