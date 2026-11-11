storage_tests = executable(
  'storage-tests',
  '../common/doctest/doctest.h',
  '../common/main.cpp',
  'common/files.h',
  'testCodec.cpp',
  'testFile.cpp',
  'testDirectory.cpp',
  'testRecovery.cpp',
  'testService.cpp',
  include_directories: include_directories('..'),
  dependencies: storage_dep,
  override_options: ['cpp_std=c++20'],
)
test('storage', storage_tests, timeout: 120)
