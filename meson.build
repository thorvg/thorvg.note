project(
  'ThorNote',
  'cpp',
  version: '0.1.0',
  meson_version: '>=1.1',
  default_options: [
    'buildtype=debugoptimized',
    'b_sanitize=none',
    'werror=false',
    'optimization=3',
    'cpp_std=c++17',
    'warning_level=2',
    'default_library=static',
  ],
)

src_inc = include_directories('src')
subdir('src')
subdir('tests')
