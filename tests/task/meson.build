task_tests = executable(
  'task-tests',
  '../common/doctest/doctest.h',
  '../common/main.cpp',
  'testTask.cpp',
  include_directories: include_directories('..'),
  dependencies: task_dep,
  cpp_args: task_args,
)
test('task', task_tests, timeout: 30)
