Include(FetchContent)
set(CADICAL_GIT_TAG "master" CACHE STRING "CaDiCaL git commit hash or tag to checkout")

FetchContent_Declare(
  cadical
  GIT_REPOSITORY https://github.com/arminbiere/cadical
  GIT_TAG        ${CADICAL_GIT_TAG}
  PATCH_COMMAND  cp ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt .
)
FetchContent_MakeAvailable(cadical)
