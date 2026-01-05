include(FetchContent)

set(_certifaiger_fetch_args
  GIT_REPOSITORY https://github.com/Froleyks/certifaiger
  GIT_TAG main
)

set(CERTIFAIGER_DIR "${CMAKE_SOURCE_DIR}/../certifaiger" CACHE PATH "Path to a local certifaiger checkout")
cmake_path(NORMAL_PATH CERTIFAIGER_DIR)
if (EXISTS "${CERTIFAIGER_DIR}/CMakeLists.txt")
  message(STATUS "Using local certifaiger at: ${CERTIFAIGER_DIR}")
  set(_certifaiger_fetch_args SOURCE_DIR "${CERTIFAIGER_DIR}")
else()
  message(STATUS "Fetching certifaiger from Git")
endif()

FetchContent_Declare(certifaiger ${_certifaiger_fetch_args})
FetchContent_MakeAvailable(certifaiger)
