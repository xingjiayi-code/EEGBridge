# EEGBridge

EEGBridge is a C++ communication framework designed to connect EEG applications with algorithms implemented in Python. Built with CMake, it uses Lab Streaming Layer (LSL) to stream EEG data to Python algorithms and WebSocket communication to exchange control commands and responses with them.

## Communication Design

- **EEG data:** LSL streams EEG data from the C++ application to Python algorithms for processing.
- **Control commands:** WebSocket provides a separate channel for exchanging control commands and responses between the C++ application and Python algorithms.

This design separates EEG data streaming from control interactions, allowing each channel to serve its specific purpose.

## Development Environment

- **Platform:** Windows
- **IDE:** CLion
- **Compiler:** MSVC from Visual Studio 2022
- **Build system:** CMake 3.22.1 or later

## Dependencies

- **liblsl:** EEG data streaming through Lab Streaming Layer.
- **IXWebSocket:** WebSocket communication for control commands.
- **nlohmann/json:** JSON support for communication messages.

Third-party dependencies are located in the `third_party/` directory.

## Building with CLion

1. Open the project directory in CLion.
2. Configure a Visual Studio toolchain using MSVC from Visual Studio 2022.
3. Select that toolchain in the project's CMake profile.
4. Reload the CMake project and build the `EEGBridge` target.

The project builds a shared library by default. Set `BUILD_SHARED_LIBS=OFF` in the CMake options to build a static library.
