cmake_minimum_required(VERSION 3.18)

set(DMOD_CPU_FAMILY "stm32f7" CACHE STRING "Target CPU family")
include(${CMAKE_CURRENT_SOURCE_DIR}/src/port/${DMOD_CPU_FAMILY}/config.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT DEFINED DMOD_MODULE_VERSION)
    set(DMOD_MODULE_VERSION "0.1" CACHE STRING "DMOD module version")
endif()

# If a local dmod checkout is provided, use it instead of fetching from git
if(DEFINED DMOD_DIR AND NOT "${DMOD_DIR}" STREQUAL "")
    set(FETCHCONTENT_SOURCE_DIR_DMOD "${DMOD_DIR}" CACHE PATH "" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
    dmod
    GIT_REPOSITORY https://github.com/choco-technologies/dmod.git
    GIT_TAG        develop
)

set(DMOD_MODE "DMOD_MODULE" CACHE STRING "DMOD build mode")
set(DMOD_BUILD_TESTS OFF CACHE BOOL "Build tests")
set(DMOD_BUILD_EXAMPLES OFF CACHE BOOL "Build examples")
set(DMOD_BUILD_TOOLS OFF CACHE BOOL "Build tools")
set(DMOD_BUILD_TEMPLATES OFF CACHE BOOL "Build templates")

FetchContent_MakeAvailable(dmod)

project(dmsdio VERSION ${DMOD_MODULE_VERSION}
    DESCRIPTION "SD memory card driver (SDSC/SDHC/SDXC) for DMOD"
    LANGUAGES C CXX)

set(DMOD_DIR ${dmod_SOURCE_DIR} CACHE PATH "DMOD source directory")
set(DMOD_SCRIPTS_DIR ${DMOD_DIR}/scripts CACHE PATH "DMOD scripts directory")
include(${DMOD_DIR}/paths.cmake)
dmod_setup_external_module()

# Name of the module
set(DMOD_MODULE_NAME        dmsdio)

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         1024)

# Path to the .dmr file - enables release package creation in build/packages/
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/dmsdio.dmr)

# List of MAL interfaces implemented by this module (leave empty if none)
set(DMOD_MAL_IMPLS          )

# List of DIF interfaces implemented by this module (leave empty if none)
set(DMOD_DIF_IMPLS          )

#
#   dmod_add_library - create a library module
#   it has the same signature as add_library
#   and can be used in the same way after the creation
#   (for example, to link libraries)
#
dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    # List of source files - can include C and C++ files
    src/dmsdio.c
    src/dmsdio_card.c
    src/dmsdio_cmd.c
    src/dmsdio_config.c
    src/dmsdio_decode.c
    src/dmsdio_detect.c
    src/dmsdio_ident.c
    src/dmsdio_io.c
    src/dmsdio_xfer.c
)

# dmdrvi 2.1: the host node implements the monitor ioctls (presence is
# monitored by dmdevfs' dmdevmon service, see docs/api-reference.md).
# dmgpio 1.1: its ioctls start at DMDRVI_IOCTL_CUSTOM_BASE - the card detect
# read (dmgpio_ioctl_cmd_get_high/low_pins_state) has other values in 1.0.
dmod_link_modules(${DMOD_MODULE_NAME}
    dmdrvi@>=2.1
    dmini
    dmosi
    dmgpio@>=1.1
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(${DMOD_MODULE_NAME}_if INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_subdirectory(src/port)
target_link_libraries(${DMOD_MODULE_NAME} ${DMOD_MODULE_NAME}_port_if)
enable_testing()
add_subdirectory(tests)
