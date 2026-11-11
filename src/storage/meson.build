storage_deps = [task_dep]
storage_sources = files(
  'storage.h',
  'internal.h',
  'codec.cpp',
  'directory.cpp',
  'file.cpp',
  'recovery.cpp',
  'service.cpp',
  'sha256.cpp',
)

subdir('platform')

storage_library = library(
  'storage',
  storage_sources,
  include_directories: src_inc,
  dependencies: storage_deps,
  override_options: ['cpp_std=c++20'],
)
storage_dep = declare_dependency(
  link_with: storage_library,
  include_directories: src_inc,
  dependencies: storage_deps,
)
