thornote_sources += files('native_text_input.cpp', 'browser_files.cpp')
thornote_link_args += [
  '--js-library', meson.current_source_dir() / 'text_input.js',
  '--js-library', meson.current_source_dir() / 'recovery.js',
]
thornote_link_depends += files('text_input.js', 'recovery.js')

if get_option('optimization') == 's'
  thornote_link_args += [
    '--closure', '1',
    '--closure-args=--externs="' + meson.current_source_dir() / 'closure-externs.js' + '"',
  ]
  thornote_link_depends += files('closure-externs.js')
endif
