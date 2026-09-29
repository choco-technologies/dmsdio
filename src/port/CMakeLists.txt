# ======================================================================
#               dmsdio_port Module Configuration
# ======================================================================

# Name of the module
set(DMOD_MODULE_NAME        dmsdio_port)

# Version is already set above and used in project()

# Author (should be string)
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")

# Stack size for the module (should be integer)
set(DMOD_STACK_SIZE         1024)

# Path to the DMR file for this module
set(DMOD_DMR_PATH           ${CMAKE_SOURCE_DIR}/dmsdio_port.dmr)

# Extra sources of a CPU family. Every STM32 family shares the same
# SDIO/SDMMC host IP (see src/port/stm32_common), the x86_64 family is the
# simulated card used by the tests.
set(FAMILY_SOURCES "")
if(DMOD_CPU_FAMILY MATCHES "^stm32")
    set(FAMILY_SOURCES
        stm32_common/stm32_common.c
        stm32_common/stm32_data.c
    )
elseif(DMOD_CPU_FAMILY STREQUAL "x86_64")
    set(FAMILY_SOURCES
        x86_64/mock_card.c
        x86_64/mock_registers.c
    )
endif()

#
#   dmod_add_library - create a library module
#   it has the same signature as add_library
#   and can be used in the same way after the creation
#   (for example, to link libraries)
#
dmod_add_library(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    # List of source files - can include C and C++ files
    ${DMOD_CPU_FAMILY}/port.c
    ${FAMILY_SOURCES}
)

# One dmod_link_modules() call: each call rewrites the target's version
# requirements file.
# dmdrvi: headers only - dmsdio_types.h numbers the dmsdio ioctls from
# DMDRVI_IOCTL_CUSTOM_BASE.
# dmdma 0.4: dmdma_lease_start_ex() (FIFO/burst/flow-controller options)
set(DMSDIO_PORT_MODULES dmdrvi@>=2.0)
if(DMOD_CPU_FAMILY MATCHES "^stm32")
    list(APPEND DMSDIO_PORT_MODULES
        dmclk_port
        dmosi
        dmdma@>=0.4
    )
endif()
dmod_link_modules(${DMOD_MODULE_NAME} ${DMSDIO_PORT_MODULES})

target_include_directories(${DMOD_MODULE_NAME} BEFORE PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/${DMOD_CPU_FAMILY}
)

target_include_directories(${DMOD_MODULE_NAME}_if INTERFACE
    ${CMAKE_SOURCE_DIR}/include
)
