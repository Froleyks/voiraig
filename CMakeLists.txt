cmake_minimum_required(VERSION 3.22.1)
project(voiraig)
find_package(Git QUIET)

file(GLOB headers CONFIGURE_DEPENDS *.hpp)
file(GLOB sources CONFIGURE_DEPENDS *.cpp)

# Options:
option(GIT_SUBMODULE "Automatically download dependencies" ON)
option(LTO "Enable Link-Time Optimization" ON)
option(LOG "Enable Logging" OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(LOG
      ON
      CACHE BOOL "Enable Logging" FORCE)
endif()

# Compile flags:
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(LTO)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif(LTO)
if(LOG)
  add_definitions("-DLOG")
endif(LOG)

# Dependencies:
if(GIT_SUBMODULE)
  message(STATUS "Submodule update")
  execute_process(
    COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE GIT_SUBMOD_RESULT)
  if(NOT GIT_SUBMOD_RESULT EQUAL "0")
    message(
      FATAL_ERROR
        "git submodule update --init --recursive failed with ${GIT_SUBMOD_RESULT}, please checkout submodules"
    )
  endif()
endif()

# Aiger:
set(AIGER_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/aiger
    CACHE PATH "Path to Aiger")
find_file(
  AIGER_SRC
  NAMES aiger.c
  PATHS ${AIGER_PATH})
if(NOT AIGER_SRC)
  message(FATAL_ERROR "Aiger library not found in ${AIGER_PATH}.")
endif()
add_library(aiger STATIC ${AIGER_PATH}/aiger.c)
target_include_directories(aiger PUBLIC ${AIGER_PATH}/)

# Format:
add_custom_target(
  format
  COMMAND clang-format -i ${sources} ${headers} || echo
          "clang-format not installed"
  COMMAND cmake-format -i ${CMAKE_CURRENT_LIST_FILE} || echo
          "cmake-format not installed"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Formatting files")

# Voircheck:
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/voircheck.in
               ${CMAKE_CURRENT_BINARY_DIR}/voircheck @ONLY)

# Voiraig:
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VERSION)
string(STRIP ${VERSION} VERSION)
execute_process(
  COMMAND bash "-c" "git show|head -1|awk '{print $2}'"
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE GIT_ID)
add_definitions("-DVERSION=\"${VERSION}\"")
add_definitions("-DGITID=\"${GIT_ID}\"")

add_executable(voiraig ${sources})
target_link_libraries(voiraig aiger)
