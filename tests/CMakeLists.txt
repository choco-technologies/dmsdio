set(DMOD_MODULE_NAME        test_dmsdio)
set(DMOD_MODULE_VERSION     ${PROJECT_VERSION})
set(DMOD_AUTHOR_NAME        "Patryk Kubiak")
set(DMOD_STACK_SIZE         1024)
set(DMOD_PRIORITY           0)

# Runs against the x86_64 mock in src/port/x86_64/port.c (see
# mock_control.h) - only meaningful in a build configured with
# -DDMOD_CPU_FAMILY=x86_64; a stm32f4/stm32f7-configured build still
# compiles this (dmod_add_test builds for whatever DMOD_TOOLS_NAME is
# active) but nothing here ever runs it through dmod_loader on real
# hardware, since ctest never targets the flashed board.
dmod_add_test(${DMOD_MODULE_NAME} ${DMOD_MODULE_VERSION}
    dmsdio_test.c
    fake_mal.c
)

target_include_directories(${DMOD_MODULE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/port/x86_64
)

target_link_libraries(${DMOD_MODULE_NAME} dmsdio_if dmsdio_port_if)

dmod_link_modules(${DMOD_MODULE_NAME}
    dmsdio
    dmdrvi
    dmini
)

# Register the test module with ctest - this replays the same two steps CI
# runs (see .github/templates/ci.yml.template): install the test module's
# dependencies with dmf-get, then execute it through dmod_loader. The test's
# exit code equals its number of failed steps (see dmod_add_test).
find_program(DMF_GET_EXECUTABLE dmf-get HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)
find_program(DMOD_LOADER_EXECUTABLE dmod_loader HINTS ${DMOD_TOOLS_BIN_DIR} PATHS ENV PATH)

if(DMF_GET_EXECUTABLE AND DMOD_LOADER_EXECUTABLE)
    # test_dmsdio only ever looks "dmsdio" up by name at runtime
    # (Dmod_GetModuleContext("dmsdio") - see dmsdio_test.c), so nothing in
    # its compiled binary references dmsdio's own signature the way it does
    # dmsdio_port's/dmini's Module API calls - todmd's dependency scan
    # can't see a DIF-by-name lookup like that, so "dmsdio" never makes it
    # into test_dmsdio(-local).dmd on its own (confirmed: dmod_link_modules()
    # above only *constrains the version* of dependencies the scan already
    # found, it can't add ones the scan missed - see todmd --help). Install
    # it explicitly, from the same local manifest every other local
    # dependency here already resolves through.
    add_test(
        NAME ${DMOD_MODULE_NAME}
        COMMAND ${CMAKE_COMMAND} -E env DMOD_DMF_DIR=${DMOD_DMF_DIR}
            bash -c "${DMF_GET_EXECUTABLE} install -d ${DMOD_DMF_DIR}/${DMOD_MODULE_NAME}-local.dmd -y && ${DMF_GET_EXECUTABLE} install dmsdio -m ${CMAKE_BINARY_DIR}/packages/manifest-local.dmm -y && ${DMOD_LOADER_EXECUTABLE} ${DMOD_DMF_DIR}/${DMOD_MODULE_NAME}.dmf"
    )
else()
    message(WARNING "dmf-get/dmod_loader not found - 'ctest' will not run ${DMOD_MODULE_NAME}. Build the tools first with -DDMOD_BUILD_TOOLS=ON")
endif()
