list(APPEND OVERLAY_CONFIG "coap-server.conf")
list(APPEND OVERLAY_CONFIG "secret.conf")
list(APPEND OVERLAY_CONFIG "thread-ftd.conf")

cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

execute_process(COMMAND git describe
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                OUTPUT_VARIABLE GIT_DESCRIBE
                ERROR_QUIET)
string(STRIP "${GIT_DESCRIBE}" GIT_DESCRIBE)
STRING(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+"
       VERSION "${GIT_DESCRIBE}")

project(app LANGUAGES C VERSION ${VERSION})


configure_file(app_version.h.in ${CMAKE_BINARY_DIR}/app/include/app_version.h)
zephyr_include_directories(
        ${CMAKE_BINARY_DIR}/app/include
)
target_include_directories(app PRIVATE ${CMAKE_BINARY_DIR}/app/include src)

zephyr_include_directories(
       configuration/${BOARD})

target_sources(app PRIVATE
        src/door.c
        src/main.c)

zephyr_linker_sources(DATA_SECTIONS sections-ram.ld)

zephyr_iterable_section(
        NAME coap_resource_coap_server
        GROUP DATA_REGION ${XIP_ALIGN_WITH_INPUT}
        SUBALIGN CONFIG_LINKER_ITERABLE_SUBALIGN)
