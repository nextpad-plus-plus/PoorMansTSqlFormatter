cmake_minimum_required(VERSION 3.20)
project(PoorMansTSqlFormatter LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)

# Universal binary (Apple Silicon + Intel), matching the host.
if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# The faithful C++ engine (validated byte-for-byte against the Windows corpus).
set(ENGINE_SOURCES
    src/engine/Node.cpp
    src/engine/Tokenizer.cpp
    src/engine/ParseTree.cpp
    src/engine/Parser.cpp
    src/engine/Formatter.cpp)

add_library(PoorMansTSqlFormatter SHARED
    src/PoorMansTSqlFormatter.mm
    ${ENGINE_SOURCES})

# Vendored headers (deps/: NppPluginInterfaceMac.h, Scintilla.h, Sci_Position.h)
# plus src/ for the embedded AboutLogoData.h / LicenseData.h.
target_include_directories(PoorMansTSqlFormatter PRIVATE
    ${CMAKE_SOURCE_DIR}/deps
    ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(PoorMansTSqlFormatter PRIVATE "-framework Cocoa")

set_target_properties(PoorMansTSqlFormatter PROPERTIES
    PREFIX ""
    SUFFIX ".dylib"
    OUTPUT_NAME "PoorMansTSqlFormatter")

set(PLUGIN_INSTALL_DIR "$ENV{HOME}/.nextpad++/plugins/PoorMansTSqlFormatter")
install(TARGETS PoorMansTSqlFormatter DESTINATION "${PLUGIN_INSTALL_DIR}")

# ── standalone corpus-validation harness (not installed) ─────────────────────
add_executable(run_corpus EXCLUDE_FROM_ALL
    tests/run_corpus.cpp ${ENGINE_SOURCES})
target_include_directories(run_corpus PRIVATE ${CMAKE_SOURCE_DIR}/src)
