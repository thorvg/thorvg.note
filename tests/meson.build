test_sources = []
test_deps = []
subdir('text')

test_runner = executable(
  'ThorNote-tests',
  'common/doctest/doctest.h',
  'common/main.cpp',
  test_sources,
  dependencies: test_deps,
  include_directories: include_directories('.'),
)
test('text', test_runner,
  args: ['--test-suite=document,session'],
  timeout: 120,
)

subdir('task')
subdir('storage')
subdir('common')
subdir('ui')
