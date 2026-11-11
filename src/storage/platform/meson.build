storage_platform = host_machine.system()
if storage_platform == 'windows'
  error('Storage is not supported on Windows.')
endif

storage_sources += files('native.h')
subdir('posix')
if storage_platform == 'darwin'
  storage_sources += files('mac/metadata.cpp')
elif storage_platform == 'linux'
  storage_sources += files('linux/metadata.cpp')
elif storage_platform == 'emscripten'
  storage_sources += files('emscripten/metadata.cpp')
else
  storage_sources += files('generic/metadata.cpp')
endif
