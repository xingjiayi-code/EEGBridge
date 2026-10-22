cmake_minimum_required(VERSION 3.22.1)
project(EEGBridgeExample LANGUAGES CXX)

# Keep the executable and runtime DLLs together so the example can run directly.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Also allow opening the example as a standalone project.
if(NOT TARGET EEGBridge::EEGBridge)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_BINARY_DIR}/EEGBridge")
endif()

set_target_properties(EEGBridge PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

add_executable(EEGBridgeExample main.cpp)
target_link_libraries(EEGBridgeExample PRIVATE EEGBridge::EEGBridge)

add_executable(EEGBridgeLslExample lsl_main.cpp)
target_link_libraries(EEGBridgeLslExample PRIVATE EEGBridge::EEGBridge)
