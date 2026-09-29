# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/__w/dmsdio/dmsdio/build_ci_port/_deps/dmod-src")
  file(MAKE_DIRECTORY "/__w/dmsdio/dmsdio/build_ci_port/_deps/dmod-src")
endif()
file(MAKE_DIRECTORY
  "/__w/dmsdio/dmsdio/build_ci_port/_deps/dmod-build"
  "/__w/dmsdio/dmsdio/build_ci_port/_deps/dmod-subbuild/dmod-populate-prefix"
  "/__w/dmsdio/dmsdio/build_ci_port/_deps/dmod-subbuild/dmod-populate-prefix/tmp"
  "/__w/dmsdio/dmsdio/build_ci_port/_deps/dmod-subbuild/dmod-populate-prefix/src/dmod-populate-stamp"
  "/__w/dmsdio/dmsdio/build_ci_port/_deps/dmod-subbuild/dmod-populate-prefix/src"
  "/__w/dmsdio/dmsdio/build_ci_port/_deps/dmod-subbuild/dmod-populate-prefix/src/dmod-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/__w/dmsdio/dmsdio/build_ci_port/_deps/dmod-subbuild/dmod-populate-prefix/src/dmod-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/__w/dmsdio/dmsdio/build_ci_port/_deps/dmod-subbuild/dmod-populate-prefix/src/dmod-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
