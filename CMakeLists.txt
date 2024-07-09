cmake_minimum_required(VERSION 3.22.1)
project(voiraig)

file(GLOB headers CONFIGURE_DEPENDS src/*.hpp)
file(GLOB sources CONFIGURE_DEPENDS src/*.cpp)

# Options:
option(LOG "Enable Logging" OFF)
option(LTO "Enable Link-Time Optimization" ON)
option(GIT "Automatically download dependencies" ON)
option(CHECK "Setup checking and fuzzing" ON)

set(AIGER_DIR ${CMAKE_CURRENT_LIST_DIR}/aiger CACHE PATH "aiger directory")
set(CERTIFAIGER_DIR ${CMAKE_CURRENT_LIST_DIR}/certifaiger
    CACHE PATH "certifaiger directory")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(LOG ON CACHE BOOL "Enable Logging" FORCE)
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
function(git_submodule)
  if(GIT)
    find_package(Git QUIET)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive ${ARGV0}
      WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR} RESULT_VARIABLE GIT_RES)
    if(NOT GIT_RES EQUAL "0")
      message(
        FATAL_ERROR
          "git submodule update --init --recursive failed with ${GIT_RES}, please checkout submodules"
      )
    endif()
  endif()
endfunction()

if(NOT TARGET aiger)
  git_submodule(aiger)
  find_file(AIGER_SRC NAMES aiger.c PATHS ${AIGER_DIR})
  if(NOT AIGER_SRC)
    message(FATAL_ERROR "Aiger library not found in ${AIGER_DIR}.")
  endif()
  add_library(aiger STATIC ${AIGER_DIR}/aiger.c)
  target_include_directories(aiger PUBLIC ${AIGER_DIR}/)
endif()

if(CHECK)
  git_submodule(certifaiger)
  add_subdirectory(${CERTIFAIGER_DIR})
endif()

# Targets
# # Format:
add_custom_target(
  format_${PROJECT_NAME}
  COMMAND clang-format -i ${sources} ${headers} || echo "clang-format not installed"
  COMMAND cmake-format -i ${CMAKE_CURRENT_LIST_FILE} || echo "cmake-format not installed"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "Formatting files")
set_target_properties(format_${PROJECT_NAME} PROPERTIES EXCLUDE_FROM_ALL TRUE)

# # Voiraig:
file(READ "${CMAKE_CURRENT_LIST_DIR}/VERSION" VERSION)
string(STRIP ${VERSION} VERSION)
execute_process(COMMAND bash "-c" "git show|head -1|awk '{print $2}'"
                OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE GIT_ID)
add_definitions("-DVERSION=\"${VERSION}\"")
add_definitions("-DGITID=\"${GIT_ID}\"")

add_executable(voiraig ${sources})
target_link_libraries(voiraig aiger)
install(TARGETS voiraig)
