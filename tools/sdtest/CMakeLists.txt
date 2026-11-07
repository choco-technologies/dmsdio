# sdtest - a manual, read-only test of a dmsdio card; added from the top-level CMakeLists.txt (shares its dmod setup)
set(DMOD_DMR_PATH ${CMAKE_CURRENT_SOURCE_DIR}/sdtest.dmr)

dmod_add_executable(sdtest ${DMOD_MODULE_VERSION}
    main.c
)

# dmdrvi: headers only, for the block commands of the node (DMDRVI_IOCTL_BLOCK_*, the monitor refresh)
dmod_link_modules(sdtest
    dmdrvi
)

target_include_directories(sdtest PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
