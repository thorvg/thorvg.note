test(
  'app',
  import('python').find_installation(),
  args: [files('smoke.py'), thornote.full_path()],
  depends: thornote,
  timeout: 120,
)

app_tests = executable(
  'app-tests',
  '../common/main.cpp',
  'testSearch.cpp',
  'testMarkdownTheme.cpp',
  'testNoteBrowser.cpp',
  'testWorkspace.cpp',
  dependencies: [test_dep, thornote_app_dep],
)
test('search', app_tests, timeout: 120)
