icon_assets = [
  'folder', 'file', 'star', 'search', 'check', 'chevron-right', 'grid',
  'command', 'close', 'new', 'save', 'settings', 'menu', 'sidebar',
  'properties', 'chevron-left', 'plus', 'minus', 'dirty',
  'window-minimize', 'window-maximize', 'window-restore', 'window-fullscreen',
]
icon_rows = []
foreach name : icon_assets
  # Strip formatting around JSON lines, preserving whitespace inside strings.
  content = []
  foreach line : import('fs').read('assets' / (name + '.json')).splitlines()
    content += line.strip()
  endforeach
  icon_rows += '    {"@0@", R"LOTTIE(@1@)LOTTIE", true},'.format(name, ''.join(content))
endforeach
icon_data = configuration_data()
icon_data.set('UI_ICON_DATA', '\n'.join(icon_rows))
icon_header = configure_file(
  input: 'builtin_data.h.in',
  output: 'builtin_data.h',
  configuration: icon_data,
)

ui_sources += files('icons.cpp')
ui_sources += icon_header
