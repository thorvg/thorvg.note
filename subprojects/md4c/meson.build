# Vendored from https://github.com/mity/md4c/tree/release-0.5.3 (MIT).
project('md4c', 'c', version: '0.5.3', default_options: ['warning_level=0'])
md4c_library = static_library('md4c', 'md4c.c')
md4c_dep = declare_dependency(link_with: md4c_library, include_directories: include_directories('.'))
