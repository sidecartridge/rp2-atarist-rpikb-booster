# Create a library for the cJSON files
add_library(cjson
    cJSON.c
)

# Add the include directory (for cJSON.h)
target_include_directories(cjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Link necessary libraries
target_link_libraries(cjson
    pico_stdlib    # Core Pico SDK library
)
