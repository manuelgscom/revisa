cmake_minimum_required(VERSION 3.28)
project(revisa VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Fake engine and download overrides driven by environment variables (REVISA_FAKE_*,
# REVISA_TEST_MODEL_*). Release builds set it to OFF.
option(REVISA_TEST_HOOKS "Fake engine and test overrides driven by environment variables" ON)

# Optimize for this CPU. OFF by default: development measures the same SIMD set as the release
# (AVX2 + FMA + F16C), not whatever the development machine has.
option(REVISA_NATIVE "Build ggml with -march=native" OFF)

# Qt 6 tray app (revisa-tray) and its tests. Needs Qt 6.8+ (Core, Gui, Widgets); OFF builds the CLI only.
option(REVISA_GUI "Build the Qt tray app revisa-tray" OFF)

# Download URL of the default model. Empty = the one in src/core/fetch/model_info.hpp.
set(REVISA_MODEL_URL "" CACHE STRING "Download URL of the default model (empty = model_info.hpp)")

# Strict warnings only on our own targets. Never via a global add_compile_options:
# llama.cpp does not build with -Werror.
# MSVC: /W4 /WX, sources are UTF-8 without BOM (/utf-8) and the CRT "secure" deprecations are off
# (portable code uses getenv/fopen on purpose). C4702 (unreachable code) is raised by the optimizer
# inside inlined Qt/STL headers, where /external:W0 does not reach: off.
function(revisa_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /permissive- /Zc:__cplusplus /bigobj /wd4702)
    target_compile_definitions(${target} PRIVATE
      _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wshadow -Wconversion)
  endif()
endfunction()

# Windows executables: the application manifest makes the process code page UTF-8 (argv, getenv,
# fopen and std::filesystem::path(std::string) then take UTF-8, as on Linux) and allows long paths.
function(revisa_win_manifest target)
  if(WIN32)
    target_sources(${target} PRIVATE "${CMAKE_SOURCE_DIR}/src/win/revisa.manifest")
  endif()
endfunction()

# --- llama.cpp: static, CPU only, no OpenMP. No common/tools/server => no networking ----------
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP      OFF CACHE BOOL "" FORCE)
set(LLAMA_OPENSSL        OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE ${REVISA_NATIVE} CACHE BOOL "" FORCE)
if(NOT REVISA_NATIVE)
  set(GGML_AVX  ON CACHE BOOL "" FORCE)
  set(GGML_AVX2 ON CACHE BOOL "" FORCE)
  set(GGML_FMA  ON CACHE BOOL "" FORCE)
  set(GGML_F16C ON CACHE BOOL "" FORCE)
endif()
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
set(GGML_CUDA   OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/llama.cpp EXCLUDE_FROM_ALL SYSTEM)
# Unoptimised llama.cpp is far too slow even in Debug. (MSVC: /O2 clashes with Debug's /RTC1;
# build Release or RelWithDebInfo there.)
if(MSVC)
  # Without /Zc:__cplusplus MSVC reports 199711L even in C++20, and llama.cpp's u8"" handling
  # (LU8 in llama-chat.cpp) picks the pre-C++20 branch: char8_t errors.
  foreach(t ggml-base ggml-cpu ggml llama)
    if(TARGET ${t})
      target_compile_options(${t} PRIVATE /Zc:__cplusplus)
    endif()
  endforeach()
else()
  foreach(t ggml-base ggml-cpu ggml llama)
    if(TARGET ${t})
      target_compile_options(${t} PRIVATE $<$<CONFIG:Debug>:-O2>)
    endif()
  endforeach()
endif()

# --- revisa_core: the engine library (no Qt, no HTTP) -----------------------------------------
# A new file in src/core/<module>/ is picked up automatically. Platform files: *_linux.cpp only
# outside Windows, *_win32.cpp only on Windows.
set(REVISA_CORE_MODULES config fetch infer text segment protect diff catalog classify guard check queue tokens)
set(REVISA_CORE_SOURCES)
foreach(module IN LISTS REVISA_CORE_MODULES)
  file(GLOB module_sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/core/${module}/*.cpp")
  list(APPEND REVISA_CORE_SOURCES ${module_sources})
endforeach()
if(WIN32)
  list(FILTER REVISA_CORE_SOURCES EXCLUDE REGEX "_linux\\.cpp$")
else()
  list(FILTER REVISA_CORE_SOURCES EXCLUDE REGEX "_win32\\.cpp$")
endif()

# Catalog data (src/core/catalog/**/*.tsv, *.txt and the Hunspell es_ES files), embedded in the
# binary: one generated .cpp per file (cmake/embed.cmake) plus a registry with their paths.
set(REVISA_CATALOG_DIR "${CMAKE_SOURCE_DIR}/src/core/catalog")
file(GLOB_RECURSE REVISA_CATALOG_DATA CONFIGURE_DEPENDS RELATIVE "${REVISA_CATALOG_DIR}"
     "${REVISA_CATALOG_DIR}/*.tsv" "${REVISA_CATALOG_DIR}/*.txt")
list(FILTER REVISA_CATALOG_DATA EXCLUDE REGEX "^hunspell/")
list(APPEND REVISA_CATALOG_DATA hunspell/es_ES.aff hunspell/es_ES.dic)
list(SORT REVISA_CATALOG_DATA)
set(REVISA_CATALOG_GEN "${CMAKE_BINARY_DIR}/generated/catalog")
set(catalog_decls "")
set(catalog_entries "")
set(catalog_index 0)
foreach(rel IN LISTS REVISA_CATALOG_DATA)
  set(sym "kData${catalog_index}")
  set(out "${REVISA_CATALOG_GEN}/data_${catalog_index}.cpp")
  add_custom_command(
    OUTPUT "${out}"
    COMMAND "${CMAKE_COMMAND}" -DIN=${REVISA_CATALOG_DIR}/${rel} -DOUT=${out} -DNAME=${sym}
            -P "${CMAKE_SOURCE_DIR}/cmake/embed.cmake"
    DEPENDS "${REVISA_CATALOG_DIR}/${rel}" "${CMAKE_SOURCE_DIR}/cmake/embed.cmake"
    COMMENT "Embedding catalog/${rel}"
    VERBATIM)
  list(APPEND REVISA_CORE_SOURCES "${out}")
  string(APPEND catalog_decls "extern const unsigned char ${sym}[];\nextern const std::size_t ${sym}Size;\n")
  string(APPEND catalog_entries "    {\"${rel}\", {reinterpret_cast<const char*>(${sym}), ${sym}Size}},\n")
  math(EXPR catalog_index "${catalog_index} + 1")
endforeach()
file(CONFIGURE OUTPUT "${REVISA_CATALOG_GEN}/registry.cpp" CONTENT
"// Generated by CMakeLists.txt: the embedded catalog data files. Do not edit.
#include <cstddef>
#include <string_view>
namespace revisa::catalog::detail {
struct EmbeddedFile {
    std::string_view path;
    std::string_view data;
};
@catalog_decls@extern const EmbeddedFile kEmbeddedFiles[] = {
@catalog_entries@};
extern const std::size_t kEmbeddedFileCount = @catalog_index@;
}  // namespace revisa::catalog::detail
" @ONLY)
list(APPEND REVISA_CORE_SOURCES "${REVISA_CATALOG_GEN}/registry.cpp")

find_package(Threads REQUIRED)

add_library(revisa_core STATIC ${REVISA_CORE_SOURCES})
target_include_directories(revisa_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(revisa_core PUBLIC llama vendor::nlohmann Threads::Threads)
target_compile_definitions(revisa_core PUBLIC
  REVISA_VERSION="${PROJECT_VERSION}"
  REVISA_TEST_HOOKS=$<BOOL:${REVISA_TEST_HOOKS}>)
if(NOT REVISA_MODEL_URL STREQUAL "")
  target_compile_definitions(revisa_core PUBLIC REVISA_MODEL_URL="${REVISA_MODEL_URL}")
endif()
revisa_warnings(revisa_core)

# --- revisa_server: local HTTP server (cpp-httplib), apart from the core (Android has no HTTP) ---
file(GLOB REVISA_SERVER_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/server/*.cpp")
add_library(revisa_server STATIC ${REVISA_SERVER_SOURCES})
target_include_directories(revisa_server SYSTEM PUBLIC "${CMAKE_SOURCE_DIR}/third_party/cpp-httplib")
target_link_libraries(revisa_server PUBLIC revisa_core)
revisa_warnings(revisa_server)

# --- revisa_lsp: language server over stdio ---------------------------------------------------
file(GLOB REVISA_LSP_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/lsp/*.cpp")
add_library(revisa_lsp STATIC ${REVISA_LSP_SOURCES})
target_link_libraries(revisa_lsp PUBLIC revisa_core)
revisa_warnings(revisa_lsp)

# --- CLI ---------------------------------------------------------------------------------------
file(GLOB REVISA_CLI_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/cli/*.cpp")
add_library(revisa_cli STATIC ${REVISA_CLI_SOURCES})
target_link_libraries(revisa_cli PUBLIC revisa_core revisa_server revisa_lsp)
revisa_warnings(revisa_cli)

add_executable(revisa src/main.cpp)
target_link_libraries(revisa PRIVATE revisa_cli)
revisa_warnings(revisa)
revisa_win_manifest(revisa)

# --- revisa-tray: Qt 6 Widgets tray app (REVISA_GUI=ON). Links only revisa_core: no HTTP server.
# Everything OS-specific lives in src/tray/platform/: on Linux x11.cpp, clipboard_only.cpp and
# *_linux.cpp; on Windows win32.cpp, clipboard_only.cpp and *_win32.cpp.
# Host: cmake -B build-gui -DREVISA_GUI=ON -DCMAKE_PREFIX_PATH=$HOME/Qt/6.8.3/gcc_64
if(REVISA_GUI)
  find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
  file(GLOB REVISA_TRAY_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/tray/*.cpp")
  list(FILTER REVISA_TRAY_SOURCES EXCLUDE REGEX "/main\\.cpp$")
  file(GLOB REVISA_TRAY_PLATFORM CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/tray/platform/*.cpp")
  if(WIN32)
    list(FILTER REVISA_TRAY_PLATFORM EXCLUDE REGEX "(/x11|_linux)\\.cpp$")
  else()
    list(FILTER REVISA_TRAY_PLATFORM EXCLUDE REGEX "(/win32|_win32)\\.cpp$")
  endif()
  # The app minus main(), so the headless tests link the same code.
  add_library(revisa_tray_lib STATIC
    ${REVISA_TRAY_SOURCES} ${REVISA_TRAY_PLATFORM}
    "${CMAKE_SOURCE_DIR}/src/tray/platform/platform.hpp"
    "${CMAKE_SOURCE_DIR}/src/tray/resources/tray.qrc")
  set_target_properties(revisa_tray_lib PROPERTIES AUTOMOC ON AUTORCC ON)
  target_link_libraries(revisa_tray_lib PUBLIC revisa_core Qt6::Core Qt6::Gui Qt6::Widgets)
  if(NOT WIN32)
    # Xlib for XGrabKey and the window properties; XTest is dlopen'ed (libXtst.so.6) at runtime.
    find_package(X11 REQUIRED)
    target_link_libraries(revisa_tray_lib PRIVATE X11::X11 ${CMAKE_DL_LIBS})
  endif()
  revisa_warnings(revisa_tray_lib)

  # WIN32_EXECUTABLE: no console window on Windows (logs: QT_FORCE_STDERR_LOGGING=1).
  add_executable(revisa-tray WIN32 src/tray/main.cpp)
  set_target_properties(revisa-tray PROPERTIES AUTOMOC ON)
  target_link_libraries(revisa-tray PRIVATE revisa_tray_lib)
  if(WIN32)
    target_sources(revisa-tray PRIVATE "${CMAKE_SOURCE_DIR}/src/win/revisa-tray.rc")
  endif()
  revisa_warnings(revisa-tray)
  revisa_win_manifest(revisa-tray)
endif()

# --- Tests -------------------------------------------------------------------------------------
# Suite (and ctest label) that needs something external:
#   model  a GGUF in REVISA_TEST_MODEL. Without it ctest reports the test as Skipped, with a
#          warning (tests/run_model_suite.sh).
add_library(doctest INTERFACE)
target_include_directories(doctest SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/third_party/doctest/doctest")
if(MSVC)
  # doctest's own forward declaration of std::basic_ostream breaks printing std::string_view with
  # MSVC's STL: use the real standard headers.
  target_compile_definitions(doctest INTERFACE DOCTEST_CONFIG_USE_STD_HEADERS)
endif()

enable_testing()

file(GLOB REVISA_UNIT_TESTS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/*_test.cpp")
# The test that compares the written model contract with the code only builds where that
# document is part of the tree.
set(REVISA_CONTRACT_DOC_DIR "${CMAKE_SOURCE_DIR}/docs")
if(NOT EXISTS "${REVISA_CONTRACT_DOC_DIR}/contrato-modelo.md")
  list(FILTER REVISA_UNIT_TESTS EXCLUDE REGEX "/(contrato|catalogo)_doc_test\\.cpp$")
endif()
add_executable(revisa_tests tests/test_main.cpp ${REVISA_UNIT_TESTS})
target_link_libraries(revisa_tests PRIVATE revisa_cli doctest)
target_compile_definitions(revisa_tests PRIVATE
  REVISA_TESTS_DIR="${CMAKE_SOURCE_DIR}/tests"
  REVISA_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden")
if(EXISTS "${REVISA_CONTRACT_DOC_DIR}/contrato-modelo.md")
  target_compile_definitions(revisa_tests PRIVATE REVISA_DOCS_DIR="${REVISA_CONTRACT_DOC_DIR}")
endif()
revisa_warnings(revisa_tests)
revisa_win_manifest(revisa_tests)
if(WIN32)
  # The download tests run a fake curl.exe (tests/win/fake_curl.cpp) found through
  # DownloadOptions::path_env, alone in its directory.
  add_executable(revisa_fake_curl tests/win/fake_curl.cpp)
  set_target_properties(revisa_fake_curl PROPERTIES
    OUTPUT_NAME curl RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fake_curl")
  revisa_warnings(revisa_fake_curl)
  revisa_win_manifest(revisa_fake_curl)
  add_dependencies(revisa_tests revisa_fake_curl)
  target_compile_definitions(revisa_tests PRIVATE REVISA_FAKE_CURL_DIR="${CMAKE_BINARY_DIR}/fake_curl")
endif()
add_test(NAME revisa_tests COMMAND revisa_tests -tse=model)
if(WIN32)
  add_test(NAME revisa_tests_model
           COMMAND "${CMAKE_COMMAND}" -DTESTS_BIN=$<TARGET_FILE:revisa_tests>
                   -P "${CMAKE_SOURCE_DIR}/tests/run_model_suite.cmake"
           WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
else()
  add_test(NAME revisa_tests_model
           COMMAND "${CMAKE_SOURCE_DIR}/tests/run_model_suite.sh" "$<TARGET_FILE:revisa_tests>"
           WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")  # REVISA_TEST_MODEL may be relative to the repo
endif()
set_tests_properties(revisa_tests_model PROPERTIES LABELS model SKIP_RETURN_CODE 77)

# Terminal e2e (pty, fork, POSIX sockets): Linux only.
if(NOT WIN32)
  file(GLOB REVISA_E2E_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/e2e/*.cpp")
  add_executable(revisa_e2e tests/test_main.cpp ${REVISA_E2E_SOURCES})
  target_link_libraries(revisa_e2e PRIVATE doctest util vendor::nlohmann Threads::Threads)
  # The server e2e talks HTTP with cpp-httplib's client (header only, no revisa code).
  target_include_directories(revisa_e2e SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/third_party/cpp-httplib")
  target_compile_definitions(revisa_e2e PRIVATE
    REVISA_BIN="$<TARGET_FILE:revisa>"
    REVISA_E2E_DIR="${CMAKE_SOURCE_DIR}/tests/e2e"
    REVISA_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden")
  add_dependencies(revisa_e2e revisa)
  revisa_warnings(revisa_e2e)
  add_test(NAME revisa_e2e COMMAND revisa_e2e)
endif()

# --- revisa-tray tests (REVISA_GUI=ON) ----------------------------------------------------------
# Headless unit tests (offscreen Qt platform) and the field app the X11 e2e drives
# (an X11 session with a window manager; it is not run by ctest).
if(REVISA_GUI)
  file(GLOB REVISA_TRAY_TESTS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/tray/*_test.cpp")
  add_executable(revisa_tray_tests tests/test_main.cpp ${REVISA_TRAY_TESTS})
  set_target_properties(revisa_tray_tests PROPERTIES AUTOMOC ON)
  target_link_libraries(revisa_tray_tests PRIVATE revisa_tray_lib doctest)
  target_compile_definitions(revisa_tray_tests PRIVATE REVISA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  revisa_warnings(revisa_tray_tests)
  revisa_win_manifest(revisa_tray_tests)
  add_test(NAME revisa_tray_tests COMMAND revisa_tray_tests)
  set_tests_properties(revisa_tray_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  if(WIN32)
    # The Qt DLLs are not next to the test binary: put the Qt bin directory on PATH.
    set_property(TEST revisa_tray_tests APPEND PROPERTY ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()

  if(NOT WIN32)
    add_executable(revisa-field-app tests/tray/e2e/field_app.cpp)
    set_target_properties(revisa-field-app PROPERTIES AUTOMOC ON)
    target_link_libraries(revisa-field-app PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
    revisa_warnings(revisa-field-app)
  endif()
endif()
