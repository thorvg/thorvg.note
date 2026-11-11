task_threads_dep = declare_dependency()
task_args = []
if not get_option('task_threads')
  task_args += ['-DTN_TASK_SINGLE_THREAD']
elif host_machine.system() != 'emscripten'
  task_threads_dep = dependency('threads')
endif
task_library = library(
  'task',
  files('task.h', 'task.cpp'),
  include_directories: src_inc,
  dependencies: task_threads_dep,
  cpp_args: task_args,
)
task_dep = declare_dependency(
  link_with: task_library,
  include_directories: src_inc,
  dependencies: task_threads_dep,
)
