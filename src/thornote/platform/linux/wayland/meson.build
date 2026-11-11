thornote_wayland_dep = meson.get_compiler('cpp').find_library(':libwayland-client.so.0', required: false)
if thornote_wayland_dep.found()
  thornote_sources += files('title_bar.cpp', 'window_shadow.cpp')
  thornote_deps += thornote_wayland_dep
else
  thornote_sources += files('unavailable.cpp')
endif
