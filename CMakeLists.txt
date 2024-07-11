cmake_minimum_required(VERSION 3.16.3)
project(voiraig)

file(GLOB headers CONFIGURE_DEPENDS src/*.hpp)
file(GLOB sources CONFIGURE_DEPENDS src/*.cpp)

# Options:
option(LOG "Enable Logging" OFF)
option(LTO "Enable Link-Time Optimization" ON)
option(GIT "Automatically download dependencies" ON)
option(CHECK "Setup checking and fuzzing" ON)

set(AIGER_DIR ${CMAKE_CURRENT_LIST_DIR}/aiger CACHE PATH "aiger directory")
set(CADICAL_DIR ${CMAKE_CURRENT_LIST_DIR}/cadical CACHE PATH "CaDiCaL directory")
set(CERTIFAIGER_DIR ${CMAKE_CURRENT_LIST_DIR}/certifaiger CACHE PATH
                                                                "certifaiger directory")

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
  add_compile_definitions("LOG")
endif(LOG)

# Dependencies:
function(git_submodule)
  if(GIT)
    find_package(Git QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive ${ARGV0}
                    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR} RESULT_VARIABLE GIT_RES)
    if(NOT GIT_RES EQUAL "0")
      message(
        FATAL_ERROR
          "git submodule update --init --recursive failed with ${GIT_RES}, please checkout submodules"
      )
    endif()
  endif()
endfunction()

git_submodule(cadical)
include(ExternalProject)
ExternalProject_Add(
  CaDiCaL
  PREFIX cadical
  SOURCE_DIR ${CADICAL_DIR}
  BUILD_IN_SOURCE 1
  CONFIGURE_COMMAND ./configure
  BUILD_COMMAND make -j
  INSTALL_COMMAND cp build/libcadical.a <INSTALL_DIR> && make clean)
add_library (cadical STATIC IMPORTED)
set_target_properties(cadical PROPERTIES IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/cadical/libcadical.a)
target_include_directories(cadical INTERFACE ${CADICAL_DIR}/src)

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
add_compile_definitions("VERSION=\"${VERSION}\"")
add_compile_definitions("GITID=\"${GIT_ID}\"")

add_executable(voiraig ${sources})
target_link_libraries(voiraig aiger)
target_link_libraries(voiraig cadical)
install(TARGETS voiraig)
