cmake_minimum_required(VERSION 3.10)
project(cadical)
file(GLOB sources CONFIGURE_DEPENDS src/*.cpp)
add_library(cadical STATIC ${sources})
target_include_directories(cadical PUBLIC src)
set_target_properties(cadical PROPERTIES PUBLIC_HEADER cadical.hpp)

file(READ "VERSION" NEWLINE_CONSUME CADICAL_VERSION)
execute_process(
  COMMAND sh "-c" "git rev-parse --revs-only HEAD --"
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE CADICAL_GIT_ID
  ERROR_QUIET)
target_compile_definitions(cadical PRIVATE
    VERSION="${CADICAL_VERSION}"
    IDENTIFIER="${CADICAL_GIT_ID}"
    COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
)
