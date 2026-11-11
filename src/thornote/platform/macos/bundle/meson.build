macos_bundle_dir = get_option('prefix') / macos_appdir / 'ThorNote.app'
macos_bundle_resources = macos_bundle_dir / 'Contents/Resources'

executable(
  'ThorNote',
  thornote_main,
  dependencies: thornote_app_dep,
  install: true,
  install_dir: macos_bundle_dir / 'Contents/MacOS',
  install_rpath: ':'.join(thornote_runtime_paths),
)
configure_file(
  input: 'Info.plist.in',
  output: 'Info.plist',
  configuration: identity_data,
  install_dir: macos_bundle_dir / 'Contents',
  install_tag: 'runtime',
)
install_data(
  meson.project_source_root() / 'resource/ThorNote.icns',
  install_dir: macos_bundle_resources,
  install_tag: 'runtime',
)
install_data(thornote_fonts, install_dir: macos_bundle_resources / 'assets', install_tag: 'runtime')
install_data(thornote_layouts, install_dir: macos_bundle_resources / 'layouts', install_tag: 'runtime')
meson.add_install_script(
  import('python').find_installation(),
  files('sign.py'),
  macos_bundle_dir,
  find_program('codesign'),
  install_tag: 'runtime',
)
