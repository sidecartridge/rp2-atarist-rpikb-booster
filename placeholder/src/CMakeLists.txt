# Set minimum required version of CMake
cmake_minimum_required(VERSION 3.26)

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

# Board / platform selection:
# Use env var PICO_PLATFORM as selector: pico_w | pico2_w (also accepts rp2040 | rp2350).
# If PICO_PLATFORM is not set, fallback to BOARD_TYPE.
# Default is pico_w -> rp2040 for placeholder compatibility.
set(PICO_PLATFORM_SELECTOR "$ENV{PICO_PLATFORM}")
string(STRIP "${PICO_PLATFORM_SELECTOR}" PICO_PLATFORM_SELECTOR)

if("${PICO_PLATFORM_SELECTOR}" STREQUAL "")
    set(PICO_PLATFORM_SELECTOR "$ENV{BOARD_TYPE}")
    string(STRIP "${PICO_PLATFORM_SELECTOR}" PICO_PLATFORM_SELECTOR)
endif()

if("${PICO_PLATFORM_SELECTOR}" STREQUAL "")
    set(PICO_BOARD "pico_w")
    set(PICO_PLATFORM "rp2040")
    message(STATUS "PICO_PLATFORM not set, defaulting to PICO_BOARD=${PICO_BOARD}, PICO_PLATFORM=${PICO_PLATFORM}")
elseif("${PICO_PLATFORM_SELECTOR}" STREQUAL "pico2_w" OR "${PICO_PLATFORM_SELECTOR}" STREQUAL "rp2350")
    set(PICO_BOARD "pico2_w")
    set(PICO_PLATFORM "rp2350")
elseif("${PICO_PLATFORM_SELECTOR}" STREQUAL "pico_w" OR "${PICO_PLATFORM_SELECTOR}" STREQUAL "rp2040")
    set(PICO_BOARD "pico_w")
    set(PICO_PLATFORM "rp2040")
elseif("${PICO_PLATFORM_SELECTOR}" STREQUAL "pico")
    set(PICO_BOARD "pico")
    set(PICO_PLATFORM "rp2040")
else()
    message(FATAL_ERROR "Unsupported PICO_PLATFORM='${PICO_PLATFORM_SELECTOR}'. Supported values: pico2_w, pico_w, pico (also rp2350, rp2040).")
endif()

# Keep BOARD_TYPE aligned with resolved PICO_BOARD for downstream compile definitions.
set(BOARD_TYPE ${PICO_BOARD})

# Determine board type macros
if("${BOARD_TYPE}" STREQUAL "pico_w")
    add_compile_definitions(BOARD_TYPE_PICO_W=1 BOARD_TYPE_PICO=0 BOARD_TYPE_CUSTOM16MB=0)
elseif("${BOARD_TYPE}" STREQUAL "pico2_w")
    add_compile_definitions(BOARD_TYPE_PICO_W=1 BOARD_TYPE_PICO=0 BOARD_TYPE_CUSTOM16MB=0)
elseif("${BOARD_TYPE}" STREQUAL "pico")
    add_compile_definitions(BOARD_TYPE_PICO_W=0 BOARD_TYPE_PICO=1 BOARD_TYPE_CUSTOM16MB=0)
elseif("${BOARD_TYPE}" STREQUAL "sidecartos_16mb")
    add_compile_definitions(BOARD_TYPE_PICO_W=0 BOARD_TYPE_PICO=1 BOARD_TYPE_CUSTOM16MB=0)
else()
    message(FATAL_ERROR "Unknown BOARD_TYPE: ${BOARD_TYPE}")
endif()

# Add the env variable PICO_SDK_PATH if it does not exist
if(NOT DEFINED ENV{PICO_SDK_PATH})
    set(ENV{PICO_SDK_PATH} ../../pico-sdk)
endif()
# Show the value
message(STATUS "PICO_SDK_PATH: $ENV{PICO_SDK_PATH}")


# Include build functions from Pico SDK
include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)

# Set name of project (as PROJECT_NAME) and C/C   standards
project(placeholder C CXX)
set(CMAKE_C_STANDARD 11)
# Creates a pico-sdk subdirectory in our project for the libraries
pico_sdk_init()

# Tell CMake where to find the executable source file
add_executable(${PROJECT_NAME} 
        main.c
)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(${PROJECT_NAME})

# Link to pico_stdlib (gpio, time, etc. functions)
# Base libraries
set(LINK_LIBRARIES
        pico_stdlib              # for core functionality
)


target_link_libraries(${PROJECT_NAME} PRIVATE ${LINK_LIBRARIES}
    hardware_flash # Specific hardware flash library
)

# Use reduced space for ROMs
pico_set_binary_type(${PROJECT_NAME} placeholder)

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
        set(_DEBUG 1)
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
# PICO_DEOPTIMIZE_DEBUG is used to disable optimizations in debug mode
if (NOT DEFINED ENV{PICO_DEOPTIMIZE_DEBUG})
    set(ENV{PICO_DEOPTIMIZE_DEBUG} "1")
endif()

# PICO_USE_FASTEST_SUPPORTED_CLOCK to the maximum frequency
if (NOT DEFINED ENV{PICO_USE_FASTEST_SUPPORTED_CLOCK})
    add_definitions(-DPICO_USE_FASTEST_SUPPORTED_CLOCK=1)
endif()


# Handle custom build type by checking if it's set
if(CMAKE_BUILD_TYPE STREQUAL "CustomBuild")
    # Define custom build type flags
    set(CMAKE_C_FLAGS_CUSTOMBUILD "-ffunction-sections -fdata-sections -O0 -mcpu=cortex-m0 -mthumb")
    set(CMAKE_CXX_FLAGS_CUSTOMBUILD "-ffunction-sections -fdata-sections -O0 -mcpu=cortex-m0 -mthumb")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS_CUSTOMBUILD}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_CUSTOMBUILD}")
endif()

# Conditionally set linker script
if(PICO_PLATFORM STREQUAL "rp2350" OR PICO_PLATFORM STREQUAL "rp2350-arm-s" OR PICO_PLATFORM STREQUAL "rp2350-riscv")
    set_target_properties(${PROJECT_NAME} PROPERTIES
        PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap_rp2-ikbd_rp2350.ld
    )
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES
        PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/memmap_rp2-ikbd_default.ld
    )
endif()

# Print the variables on screen
message(STATUS "RELEASE_TYPE: " ${RELEASE_TYPE})
message(STATUS "RELEASE_VERSION: " ${RELEASE_VERSION})
message(STATUS "RELEASE_DATE: " ${RELEASE_DATE})
message(STATUS "DEBUG_MODE: " ${_DEBUG})
message(STATUS "LATEST_RELEASE_URL: " ${LATEST_RELEASE_URL})
message(STATUS "PICO_USE_FASTEST_SUPPORTED_CLOCK: " ${PICO_USE_FASTEST_SUPPORTED_CLOCK})
