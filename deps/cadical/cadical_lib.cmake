Include(FetchContent)
FetchContent_Declare(cadical GIT_REPOSITORY https://github.com/arminbiere/cadical
                     PATCH_COMMAND cp ${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt .)
FetchContent_MakeAvailable(cadical)
