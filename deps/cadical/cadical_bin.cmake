include(ExternalProject)
ExternalProject_Add(
  cadical
  GIT_REPOSITORY https://github.com/arminbiere/cadical.git
  GIT_TAG master
  BUILD_IN_SOURCE 1
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND ./configure
  BUILD_COMMAND make -j
  INSTALL_COMMAND cp build/cadical ${CMAKE_CURRENT_BINARY_DIR})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/cadical TYPE BIN)
