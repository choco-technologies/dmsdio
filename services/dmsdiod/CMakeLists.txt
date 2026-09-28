# =====================================================================
#               dmsdiod - SD card presence service
# =====================================================================
set(DMOD_MODULE_NAME        dmsdiod)
set(DMOD_STACK_SIZE         1024)
set(DMOD_DMR_PATH           ${CMAKE_CURRENT_SOURCE_DIR}/dmsdiod.dmr)

dmod_add_executable(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    main.c
)

# Only the public ioctl interface of dmsdio (dmsdio_types.h) - the driver is
# reached through its host node, never linked.
target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

dmod_link_modules(${DMOD_MODULE_NAME}
    dmosi
    dmhaman
)
