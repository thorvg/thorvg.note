thornote_sources += files('clipboard.cpp', 'window_actions.cpp')

if target_platform == 'macos'
  subdir('macos')
else
  subdir('sdl')
  if target_platform == 'linux'
    subdir('linux')
  else
    subdir('posix')
  endif
  if target_platform == 'emscripten'
    subdir('emscripten')
  endif
endif
