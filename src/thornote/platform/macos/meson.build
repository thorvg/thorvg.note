macos_appdir = '/Applications'

add_languages('objcpp', required: true, native: false)
thornote_sources += files(
  'display_text.cpp',
  'native_text_input.mm',
  'native_window.mm',
  'font_catalog.cpp',
  'system_fonts.cpp',
)
thornote_deps += dependency('appleframeworks', modules: ['CoreText', 'CoreFoundation', 'AppKit'])
