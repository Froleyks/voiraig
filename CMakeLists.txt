cmake_minimum_required(VERSION 3.22.1)
project(voiraig)

file(GLOB headers CONFIGURE_DEPENDS *.hpp)
file(GLOB sources CONFIGURE_DEPENDS *.cpp)

# Options:
option(LTO "Enable Link-Time Optimization" ON)
option(LOG "Enable Logging" OFF)

# Format:
add_custom_target(
  format
  COMMAND clang-format -i ${sources} ${headers}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Running clang-format")

# Library:
set(AIGER_PATH
    "$ENV{AIGER}"
    CACHE PATH "Path to the aiger directory")
if(NOT AIGER_PATH)
  set(AIGER_PATH "../aiger")
endif()
find_file(
  AIGER_SRC
  NAMES aiger.c
  PATHS ${AIGER_PATH})
if(NOT AIGER_SRC)
  message(
    FATAL_ERROR
      "Aiger library not found in ${AIGER_PATH}.
    Please set with -DAIGER_PATH=/path/to/aiger or build_dependencies.sh to download to the default location."
  )
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

# Executable Targets:
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VERSION)
string(STRIP ${VERSION} VERSION)
execute_process(
  COMMAND bash "-c" "git show|head -1|awk '{print $2}'"
  OUTPUT_STRIP_TRAILING_WHITESPACE
  OUTPUT_VARIABLE GIT_ID)
add_definitions("-DVERSION=\"${VERSION}\"")
add_definitions("-DGITID=\"${GIT_ID}\"")

add_executable(voiraig ${sources})
add_library(aiger STATIC ${AIGER_PATH}/aiger.c)
target_include_directories(aiger PUBLIC ${AIGER_PATH}/)
target_link_libraries(voiraig aiger)
