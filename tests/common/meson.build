test_support_sources = files(
  'engine.h',
  'engine.cpp',
)
test_support_deps = [thorvg_dep]
test_support = static_library(
  'test-support',
  test_support_sources,
  dependencies: test_support_deps,
  include_directories: include_directories('..'),
)
test_dep = declare_dependency(
  link_with: test_support,
  dependencies: test_support_deps,
  include_directories: include_directories('..'),
)
