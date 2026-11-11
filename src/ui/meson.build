ui_sources = files('ui.h')
ui_deps = [thorvg_dep, text_dep]

subdir('core')
subdir('components')
subdir('widgets')
subdir('bindings')

lua_dep = dependency(
  'lua',
  version: '>=5.4',
  required: true,
  fallback: ['lua', 'lua_dep'],
  default_options: [
    'interpreter=false',
    'loadlib=disabled',
    'line_editing=disabled',
  ],
)
ui_deps += lua_dep

ui_library = library(
  'ui',
  ui_sources,
  include_directories: src_inc,
  dependencies: ui_deps,
)
ui_dep = declare_dependency(
  link_with: ui_library,
  include_directories: src_inc,
  dependencies: ui_deps,
)
