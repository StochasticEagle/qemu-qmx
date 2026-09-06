qmx_builder_qt = dependency(
  'qt6',
  modules: ['Core', 'Widgets'],
  version: '>=6.5',
  include_type: 'system',
  required: get_option('qmx_builder'),
)

if qmx_builder_qt.found()
  qmx_builder_enabled = true
  qt6 = import('qt6')

  qmx_builder_moc = qt6.preprocess(
    moc_headers: files('src/mainwindow.h'),
  )

  executable(
    'qmx-builder',
    files(
      'src/main.cpp',
      'src/mainwindow.cpp',
      'src/qemudiscovery.cpp',
      'src/qmxdocument.cpp',
    ),
    qmx_builder_moc,
    dependencies: qmx_builder_qt,
    install: true,
  )

  install_data(
    'resources/io.github.StochasticEagle.QmxBuilder.desktop',
    install_dir: qemu_desktopdir,
  )
  install_data(
    'resources/io.github.StochasticEagle.QmxBuilder.xml',
    install_dir: get_option('datadir') / 'mime/packages',
  )
  install_data(
    'README.md',
    'examples/basic.qmx',
    install_dir: qemu_docdir / 'qmx-builder',
  )

  qmx_builder_qt_test = dependency(
    'qt6',
    modules: ['Test'],
    version: '>=6.5',
    include_type: 'system',
    required: false,
  )
  if qmx_builder_qt_test.found()
    qmx_builder_test_moc = qt6.preprocess(
      moc_sources: files('tests/test_qmxdocument.cpp'),
    )
    qmx_builder_test = executable(
      'test-qmxdocument',
      files('tests/test_qmxdocument.cpp', 'src/qmxdocument.cpp'),
      qmx_builder_test_moc,
      include_directories: include_directories('src'),
      dependencies: [qmx_builder_qt, qmx_builder_qt_test],
    )
    test('qmxdocument', qmx_builder_test, suite: 'qmx-builder')
  endif
endif
