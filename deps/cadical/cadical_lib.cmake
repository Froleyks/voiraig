Include(FetchContent)
set(CADICAL_GIT_TAG "master" CACHE STRING "CaDiCaL git commit hash or tag to checkout")

set(_cadical_fetch_args
  GIT_REPOSITORY https://github.com/arminbiere/cadical
  GIT_TAG        ${CADICAL_GIT_TAG}
)

set(CADICAL_DIR "${CMAKE_SOURCE_DIR}/../cadical" CACHE PATH "Path to a local cadical checkout")
cmake_path(NORMAL_PATH CADICAL_DIR)
if (EXISTS "${CADICAL_DIR}/configure")
  message(STATUS "Using local cadical at: ${CADICAL_DIR}")
  set(_cadical_fetch_args SOURCE_DIR "${CADICAL_DIR}")
else()
  message(STATUS "Fetching cadical from Git")
endif()

FetchContent_Declare(cadical
  ${_cadical_fetch_args}
  PATCH_COMMAND  cp ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt .
)
FetchContent_MakeAvailable(cadical)
