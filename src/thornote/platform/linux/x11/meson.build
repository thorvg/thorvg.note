thornote_x11_dep = meson.get_compiler('cpp').find_library(':libX11.so.6', required: false)
if thornote_x11_dep.found()
  thornote_sources += files('title_bar.cpp', 'window_shadow.cpp')
  thornote_deps += thornote_x11_dep
else
  thornote_sources += files('unavailable.cpp')
endif
