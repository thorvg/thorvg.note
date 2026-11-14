thornote_sources += files(
  'note_browser.cpp',
  'recent.cpp',
  'session.cpp',
  'workspace.cpp',
  'widgets.cpp',
  'emoji.cpp',
  'images.cpp',
)

if target_platform == 'emscripten'
  thornote_link_args += '-sFETCH=1'
else
  thornote_deps += dependency('libcurl')
endif
