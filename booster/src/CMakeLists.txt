# Set minimum required version of CMake
cmake_minimum_required(VERSION 3.26)

find_package(Perl)
if(NOT PERL_FOUND)
    message(FATAL_ERROR "Perl is needed for generating the fsdata.c file")
endif()

# if PICOTOOL_OVERRIDE_DIR system environment variable is set,
# then use that as the folder for the picotool executable
if (DEFINED ENV{PICOTOOL_OVERRIDE_DIR})
    message(STATUS "PICOTOOL_OVERRIDE_DIR env var is set to '$ENV{PICOTOOL_OVERRIDE_DIR}'")
    add_executable(picotool IMPORTED GLOBAL)
    set_property(TARGET picotool PROPERTY IMPORTED_LOCATION $ENV{PICOTOOL_OVERRIDE_DIR}/picotool)
    # check the picotool version:
    execute_process(COMMAND $ENV{PICOTOOL_OVERRIDE_DIR}/picotool version
                    OUTPUT_VARIABLE PICOTOOL_VERSION
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "^picotool v${picotool_VERSION_REQUIRED}" PICOTOOL_VERSION_MATCH ${PICOTOOL_VERSION})
    if (NOT PICOTOOL_VERSION_MATCH)
        message(STATUS "picotool version response was: ${PICOTOOL_VERSION}")
        message(FATAL_ERROR "PICOTOOL_OVERRIDE_DIR is set to '$ENV{PICOTOOL_OVERRIDE_DIR}', but the version of picotool found is not ${picotool_VERSION_REQUIRED}")
    else()
        message(STATUS "picotool version response was: ${PICOTOOL_VERSION}")
    endif()
endif ()

# If not specified, use pico_w as default board type
set(BOARD_TYPE $ENV{BOARD_TYPE})
if(NOT DEFINED BOARD_TYPE)
    message(STATUS "BOARD_TYPE not set, using default pico_w")
    set(BOARD_TYPE "pico_w")
endif()
set(PICO_BOARD ${BOARD_TYPE})

# Determine board type macros
if("${BOARD_TYPE}" STREQUAL "pico_w")
    add_compile_definitions(BOARD_TYPE_PICO_W=1 BOARD_TYPE_PICO=0 BOARD_TYPE_CUSTOM16MB=0)
elseif("${BOARD_TYPE}" STREQUAL "pico")
    add_compile_definitions(BOARD_TYPE_PICO_W=0 BOARD_TYPE_PICO=1 BOARD_TYPE_CUSTOM16MB=0)
elseif("${BOARD_TYPE}" STREQUAL "sidecartos_16mb")
    add_compile_definitions(BOARD_TYPE_PICO_W=0 BOARD_TYPE_PICO=1 BOARD_TYPE_CUSTOM16MB=0)
else()
    message(FATAL_ERROR "Unknown BOARD_TYPE: ${BOARD_TYPE}")
endif()

# Default WiFi STA test parameters (override via environment variables)
if(DEFINED ENV{STA_TEST_MODE})
    set(MNGR_STA_TEST_MODE $ENV{STA_TEST_MODE})
else()
    set(MNGR_STA_TEST_MODE 0)
endif()

if(DEFINED ENV{STA_TEST_SSID})
    set(MNGR_STA_TEST_SSID "$ENV{STA_TEST_SSID}")
else()
    set(MNGR_STA_TEST_SSID "")
endif()

if(DEFINED ENV{STA_TEST_PASSWORD})
    set(MNGR_STA_TEST_PASSWORD "$ENV{STA_TEST_PASSWORD}")
else()
    set(MNGR_STA_TEST_PASSWORD "")
endif()

# Computer target mask (1=Native, 2=USB, 4=Bluetooth)
if(DEFINED ENV{COMPUTER_TARGET})
    set(COMPUTER_TARGET $ENV{COMPUTER_TARGET})
else()
    set(COMPUTER_TARGET 5)  # Default: Native + Bluetooth
endif()
message(STATUS "COMPUTER_TARGET: ${COMPUTER_TARGET}")

set(MNGR_STA_TEST_AUTH 4)

add_compile_definitions(
    MNGR_STA_TEST_MODE=${MNGR_STA_TEST_MODE}
    MNGR_STA_TEST_SSID="${MNGR_STA_TEST_SSID}"
    MNGR_STA_TEST_PASSWORD="${MNGR_STA_TEST_PASSWORD}"
    MNGR_STA_TEST_AUTH=${MNGR_STA_TEST_AUTH}
    COMPUTER_TARGET=${COMPUTER_TARGET}
)

# Add the env variable PICO_SDK_PATH if it does not exist
if(NOT DEFINED ENV{PICO_SDK_PATH})
    set(ENV{PICO_SDK_PATH} ../../pico-sdk)
endif()
# Show the value
message(STATUS "PICO_SDK_PATH: $ENV{PICO_SDK_PATH}")

# Add the env variable PICO_EXTRAS_PATH if it does not exist
if(NOT DEFINED ENV{PICO_EXTRAS_PATH})
    set(ENV{PICO_EXTRAS_PATH} ../../pico-extras)
endif()
# Show the value
message(STATUS "PICO_EXTRAS_PATH: $ENV{PICO_EXTRAS_PATH}")

# Add / normalize BLUEPAD32_ROOT
if(NOT DEFINED ENV{BLUEPAD32_ROOT})
    set(ENV{BLUEPAD32_ROOT} "${CMAKE_CURRENT_LIST_DIR}/../../bluepad32")
endif()
set(BLUEPAD32_ROOT "$ENV{BLUEPAD32_ROOT}")
message(STATUS "BLUEPAD32_ROOT: ${BLUEPAD32_ROOT}")

# To use BTstack from Pico SDK do
set(BTSTACK_ROOT "${PICO_SDK_PATH}/lib/btstack")

# If you want to use latest BTstack (included in Bluepad32 repo),
# uncomment the following lines:
# -- uncomment from here:
#set(BTSTACK_ROOT ${BLUEPAD32_ROOT}/external/btstack)
#set(PICO_BTSTACK_PATH ${BTSTACK_ROOT})
# Add missing files here. See:
# https://github.com/bluekitchen/btstack/issues/649
# https://github.com/raspberrypi/pico-sdk/issues/2142
#set(BTSTACK_MISSING_SOURCES
#    ${BTSTACK_ROOT}/src/hci_event_builder.c
#)
# -- uncomment until here
# And you need to apply this patch in Pico SDK
# https://github.com/raspberrypi/pico-sdk/pull/2157

# Include build functions from Pico SDK
include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)
include($ENV{PICO_EXTRAS_PATH}/external/pico_extras_import.cmake)

# Set name of project (as PROJECT_NAME) and C/C   standards
project(booster C CXX)
set(CMAKE_C_STANDARD 11)
# Creates a pico-sdk subdirectory in our project for the libraries
pico_sdk_init()

# Create the fsdata.c file (regenerate when fs contents change)
set(MAKE_FS_DATA_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/external/makefsdata)

# WARNING: I use a modified version of the makefsdata script to add text/css headers, not supported in the original tool
if (NOT EXISTS ${MAKE_FS_DATA_SCRIPT})
        file(DOWNLOAD
                https://github.com/sidecartridge/lwip/blob/c476c83fd7fda8befd4fb3f99bac99cb2f31e9fd/src/apps/http/makefsdata/makefsdata
                ${MAKE_FS_DATA_SCRIPT}
                )
endif()

file(GLOB_RECURSE FS_CONTENT ${CMAKE_CURRENT_LIST_DIR}/fs/*)

add_custom_command(
        OUTPUT ${CMAKE_CURRENT_LIST_DIR}/fsdata_srv.c
        COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_CURRENT_LIST_DIR}/fsdata.c ${CMAKE_CURRENT_LIST_DIR}/fsdata_srv.c ${CMAKE_CURRENT_LIST_DIR}/fsdata_raw.c
        COMMAND perl ${MAKE_FS_DATA_SCRIPT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        COMMAND ${CMAKE_COMMAND} -E rename ${CMAKE_CURRENT_LIST_DIR}/fsdata.c ${CMAKE_CURRENT_LIST_DIR}/fsdata_raw.c
        COMMAND ${CMAKE_COMMAND} -E env /bin/sh -c "printf '#include <stddef.h>\\n#include \"lwip/apps/fs.h\"\\n\\n' > ${CMAKE_CURRENT_LIST_DIR}/fsdata_srv.c && cat ${CMAKE_CURRENT_LIST_DIR}/fsdata_raw.c >> ${CMAKE_CURRENT_LIST_DIR}/fsdata_srv.c"
        COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_CURRENT_LIST_DIR}/fsdata_raw.c
        DEPENDS ${FS_CONTENT} ${MAKE_FS_DATA_SCRIPT}
        COMMENT "Generating fsdata from fs/ directory"
        VERBATIM
)

add_custom_target(fsdata_srv_target DEPENDS ${CMAKE_CURRENT_LIST_DIR}/fsdata_srv.c)

# Add the settings library
add_subdirectory(settings)

# Add cJSON library
add_subdirectory(cjson)

# Tell CMake where to find the executable source file
add_executable(${PROJECT_NAME} 
        main.c
)

target_sources(${PROJECT_NAME} PRIVATE
        btloop.c
        gconfig.c
        lwipopts.h
        mngr.c
        mngr_httpd.c
        network.c
        version.c
        settings/settings.c
        cjson/cJSON.c
        dhcpserver/dhcpserver.c
        dnsserver/dnsserver.c
        btstack_config.h
        sdkconfig.h
        ${BTSTACK_MISSING_SOURCES}        )

add_dependencies(${PROJECT_NAME} fsdata_srv_target)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(${PROJECT_NAME})

# Link libraries required for the project
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${LINK_LIBRARIES}        # External or additional libraries passed as variables
    hardware_flash           # Flash memory access
    pico_lwip_http           # lwIP httpd library
    pico_lwip_mdns           # lwIP mDNS library
    pico_mbedtls             # mbedTLS library
    pico_stdlib              # Core functionality
    pico_cyw43_arch_lwip_threadsafe_background  # CYW43 WiFi driver with lwIP and threadsafe background
    pico_btstack_cyw43
    pico_btstack_classic
    settings                 # Custom settings library
    bluepad32)

# Link custom memmap with reserved memory for ROMs
set_target_properties(${PROJECT_NAME} PROPERTIES
        PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap_booster.ld
)

# Needed to include lwipopts.h properly
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}/dhcpserver
    ${CMAKE_CURRENT_LIST_DIR}/dnsserver)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    ${BLUEPAD32_ROOT}/src/components/bluepad32/include)

# Needed when using BTstack from our branch
include_directories(${BTSTACK_ROOT}/3rd-party/bluedroid/encoder/include)
include_directories(${BTSTACK_ROOT}/3rd-party/bluedroid/decoder/include)
# Need for certain IDEs, like CLion. Otherwise it won't find btstack
include_directories(${BTSTACK_ROOT}/src)

# Make local btstack_config.h and sdkconfig.h visible to libbluepad32
include_directories(${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(${BLUEPAD32_ROOT}/src/components/bluepad32 libbluepad32)


# Use reduced space for ROMs
pico_set_binary_type(${PROJECT_NAME} booster)

# Fetch the values from the environment variables
set(RELEASE_TYPE $ENV{RELEASE_TYPE})
set(RELEASE_VERSION $ENV{RELEASE_VERSION})
set(RELEASE_DATE $ENV{RELEASE_DATE})
set(_DEBUG $ENV{DEBUG_MODE})

# If the environment variables are not set, use default values
if(NOT DEFINED RELEASE_VERSION)

    # Check if RELEASE_TYPE is set and not empty, otherwise use default "FINAL"
    if(NOT DEFINED RELEASE_TYPE OR RELEASE_TYPE STREQUAL "")
        set(RELEASE_TYPE "final")
    endif()

    # Determine the file name based on RELEASE_TYPE
    if(RELEASE_TYPE STREQUAL "final")
        set(VERSION_FILE_NAME "version.txt")
    else()
        set(VERSION_FILE_NAME "version-${RELEASE_TYPE}.txt")
    endif()
    
    # Remove newline character if present
    string(STRIP "${RELEASE_VERSION}" RELEASE_VERSION)

    # In case version.txt is empty or does not exist, use a default version
    if("${RELEASE_VERSION}" STREQUAL "")
        set(RELEASE_VERSION "v0.0.1dev")
    endif()
endif()

if(NOT DEFINED RELEASE_DATE)
        string(TIMESTAMP CURRENT_DATE_TIME "%Y-%m-%d %H:%M:%S")
        set(RELEASE_DATE ${CURRENT_DATE_TIME})
endif()

if (NOT DEFINED _DEBUG)
        set(_DEBUG 0)
endif()

# Debug outputs
pico_enable_stdio_usb(${PROJECT_NAME} 0)
# Workaround to disable USB output in release builds
if(${_DEBUG} STREQUAL "0")
    pico_enable_stdio_uart(${PROJECT_NAME} 0)
    target_link_options(${PROJECT_NAME} PRIVATE
    "-Wl,--gc-sections"
    "-Wl,--strip-all"
    )

else()
    pico_enable_stdio_uart(${PROJECT_NAME} 1)
endif()

# Pass these values to the C compiler
add_definitions(-DRELEASE_TYPE="${RELEASE_TYPE}")
add_definitions(-DRELEASE_VERSION="${RELEASE_VERSION}")
add_definitions(-DRELEASE_DATE="${RELEASE_DATE}")
add_definitions(-DLATEST_RELEASE_URL="${LATEST_RELEASE_URL}")

# Pass the _DEBUG flag to the C compiler
add_definitions(-D_DEBUG=${_DEBUG})

# Print the variables on screen
message(STATUS "RELEASE_TYPE: " ${RELEASE_TYPE})
message(STATUS "RELEASE_VERSION: " ${RELEASE_VERSION})
message(STATUS "RELEASE_DATE: " ${RELEASE_DATE})
message(STATUS "DEBUG_MODE: " ${_DEBUG})

# Handle custom build type by checking if it's set
if(CMAKE_BUILD_TYPE STREQUAL "CustomBuild")
    # Define custom build type flags
    set(CMAKE_C_FLAGS_CUSTOMBUILD "-ffunction-sections -fdata-sections -O0 -mcpu=cortex-m0 -mthumb")
    set(CMAKE_CXX_FLAGS_CUSTOMBUILD "-ffunction-sections -fdata-sections -O0 -mcpu=cortex-m0 -mthumb")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS_CUSTOMBUILD}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_CUSTOMBUILD}")
endif()

target_compile_definitions(${PROJECT_NAME}  PRIVATE
 HCI_CONTROLLER_CHIPSET_PICOW
)
