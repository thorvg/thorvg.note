project('stb', 'cpp', version: '1.26', meson_version: '>=1.1')

# Only the UI font source compiles the stb_truetype implementation.
stb_dep = declare_dependency(
  include_directories: include_directories('.', is_system: true),
)
meson.override_dependency('stb', stb_dep)
