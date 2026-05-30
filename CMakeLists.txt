cmake_minimum_required(VERSION 3.20)
project(PoorMansTSqlFormatter LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

set(NPP_MACOS_DIR "${CMAKE_SOURCE_DIR}/../../notepad-plus-plus-macos")
set(NPP_WIN_DIR "${CMAKE_SOURCE_DIR}/../../notepad-plus-plus")
set(SCINTILLA_INCLUDE "${NPP_WIN_DIR}/scintilla/include")
set(NPP_PLUGIN_INCLUDE "${NPP_MACOS_DIR}/src")

add_library(PoorMansTSqlFormatter SHARED
    src/PoorMansTSqlFormatter.mm
    src/SqlTokenizer.cpp
    src/SqlFormatter.cpp)

target_include_directories(PoorMansTSqlFormatter PRIVATE
    ${NPP_PLUGIN_INCLUDE}
    ${SCINTILLA_INCLUDE}
    src)

target_link_libraries(PoorMansTSqlFormatter PRIVATE "-framework Cocoa")

set_target_properties(PoorMansTSqlFormatter PROPERTIES
    PREFIX ""
    SUFFIX ".dylib"
    OUTPUT_NAME "PoorMansTSqlFormatter")

set(PLUGIN_INSTALL_DIR "$ENV{HOME}/.nextpad++/plugins/PoorMansTSqlFormatter")
install(TARGETS PoorMansTSqlFormatter DESTINATION "${PLUGIN_INSTALL_DIR}")
