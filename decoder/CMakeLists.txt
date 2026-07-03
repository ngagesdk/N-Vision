cmake_minimum_required(VERSION 3.10)
project(LCDDecoder C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Add the executable
add_executable(decoder main.c)

# Include directories
target_include_directories(decoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Platform-specific settings
if(MSVC)
    # MSVC compiler flags
    target_compile_options(decoder PRIVATE /W4 /WX-)
else()
    # GCC/Clang compiler flags
    target_compile_options(decoder PRIVATE -Wall -Wextra -pedantic)
endif()

# Output directory
set_target_properties(decoder PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
