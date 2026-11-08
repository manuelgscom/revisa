# Windows counterpart of run_model_suite.sh: runs the `model` doctest suite. Without
# REVISA_TEST_MODEL it warns and exits 77, which ctest reports as Skipped (SKIP_RETURN_CODE), so a
# missing model is never a silent pass.
#   usage: cmake -DTESTS_BIN=<path to revisa_tests> -P run_model_suite.cmake
# cmake_language(EXIT) needs CMake 3.29+.
if("$ENV{REVISA_TEST_MODEL}" STREQUAL "")
  message("AVISO: REVISA_TEST_MODEL no está definido: se salta la suite model.")
  message("       $env:REVISA_TEST_MODEL='model\\models\\publicar\\revisa-0.8b-Q4_K_M.gguf'; ctest --test-dir <build> -L model")
  cmake_language(EXIT 77)
endif()
if(NOT EXISTS "$ENV{REVISA_TEST_MODEL}")
  message("ERROR: REVISA_TEST_MODEL=$ENV{REVISA_TEST_MODEL} no existe.")
  cmake_language(EXIT 1)
endif()
execute_process(COMMAND "${TESTS_BIN}" -ts=model RESULT_VARIABLE rc)
if(NOT rc STREQUAL "0")
  cmake_language(EXIT 1)
endif()
