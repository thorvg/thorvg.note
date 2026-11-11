ui_tests = executable(
  'ui-tests',
  '../common/doctest/doctest.h',
  '../common/main.cpp',
  'common/fonts.h',
  'common/frame.h',
  'testComposition.cpp',
  'testContext.cpp',
  'testFont.cpp',
  'testLayout.cpp',
  'testLua.cpp',
  'testTextEditor.cpp',
  'testTextView.cpp',
  'testWidgets.cpp',
  dependencies: [test_dep, ui_dep],
  cpp_args: ['-DTEST_RESOURCE_DIR="' + meson.project_source_root() / 'tests/resource' + '"'],
)
test('ui', ui_tests, timeout: 120)
