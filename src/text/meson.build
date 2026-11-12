md4c_dep = subproject('md4c').get_variable('md4c_dep')
text_library = library(
  'text',
  files('text.cpp', 'markdown.cpp'),
  include_directories: src_inc,
  dependencies: md4c_dep,
)
text_dep = declare_dependency(
  link_with: text_library,
  include_directories: src_inc,
)
