cmake_minimum_required(VERSION 3.10)
project(cadical)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/build.hpp
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/make-build-header.sh > build.hpp
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
  COMMENT "Generating build.hpp"
  VERBATIM
)
add_custom_target(cadical_build_header
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/build.hpp
)

file(GLOB sources CONFIGURE_DEPENDS src/*.cpp src/*.c)
add_library(cadical_lib STATIC ${sources})
add_dependencies(cadical_lib cadical_build_header)
target_include_directories(cadical_lib PUBLIC src)
set_target_properties(cadical_lib PROPERTIES PUBLIC_HEADER cadical.hpp)
