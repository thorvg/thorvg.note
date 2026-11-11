thornote_sources += files(
  '../posix/font_directories.cpp',
  'native_window.cpp',
  'window_shadow.cpp',
  'system_fonts.cpp',
)
thornote_deps += declare_dependency(
  include_directories: include_directories('include', is_system: true),
  dependencies: meson.get_compiler('cpp').find_library(':libfontconfig.so.1'),
)
subdir('x11')
subdir('wayland')

configure_file(
  input: 'ThorNote.desktop.in',
  output: app_id + '.desktop',
  configuration: identity_data,
  install_dir: get_option('datadir') / 'applications',
)
install_data(
  app_icon,
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps',
  rename: app_id + '.svg',
)
