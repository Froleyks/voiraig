# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/runner/work/voiraig/voiraig/_codeql_build_dir/_deps/cadical-src")
  file(MAKE_DIRECTORY "/home/runner/work/voiraig/voiraig/_codeql_build_dir/_deps/cadical-src")
endif()
file(MAKE_DIRECTORY
  "/home/runner/work/voiraig/voiraig/_codeql_build_dir/_deps/cadical-build"
  "/home/runner/work/voiraig/voiraig/_codeql_build_dir/_deps/cadical-subbuild/cadical-populate-prefix"
  "/home/runner/work/voiraig/voiraig/_codeql_build_dir/_deps/cadical-subbuild/cadical-populate-prefix/tmp"
  "/home/runner/work/voiraig/voiraig/_codeql_build_dir/_deps/cadical-subbuild/cadical-populate-prefix/src/cadical-populate-stamp"
  "/home/runner/work/voiraig/voiraig/_codeql_build_dir/_deps/cadical-subbuild/cadical-populate-prefix/src"
  "/home/runner/work/voiraig/voiraig/_codeql_build_dir/_deps/cadical-subbuild/cadical-populate-prefix/src/cadical-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/runner/work/voiraig/voiraig/_codeql_build_dir/_deps/cadical-subbuild/cadical-populate-prefix/src/cadical-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/runner/work/voiraig/voiraig/_codeql_build_dir/_deps/cadical-subbuild/cadical-populate-prefix/src/cadical-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
