cmake_minimum_required(VERSION 3.21)

project(qmx-builder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

qt_add_executable(qmx-builder
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/qmxdocument.cpp
    src/qmxdocument.h
    src/qemudiscovery.cpp
    src/qemudiscovery.h
)

target_link_libraries(qmx-builder PRIVATE Qt6::Core Qt6::Widgets)

include(GNUInstallDirs)
install(TARGETS qmx-builder RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES resources/io.github.StochasticEagle.QmxBuilder.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES resources/io.github.StochasticEagle.QmxBuilder.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)

option(QMX_BUILDER_TESTS "Build QMX Builder tests" ON)
if(QMX_BUILDER_TESTS)
    enable_testing()
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    qt_add_executable(test-qmxdocument
        tests/test_qmxdocument.cpp
        src/qmxdocument.cpp
        src/qmxdocument.h
    )
    target_include_directories(test-qmxdocument PRIVATE src)
    target_link_libraries(test-qmxdocument PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME qmxdocument COMMAND test-qmxdocument)
endif()
