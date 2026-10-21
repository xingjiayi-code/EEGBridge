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

## WebSocket connection state

`WebSocketClient::currentState()` returns EEGBridge's own `State` enum:
`Connecting`, `Open`, `Closing`, or `Closed`. IXWebSocket headers remain private
to the library. This replaces the previous `isConnected()` API; compare the
result with `State::Open` to check whether the connection is open.

```cpp
#include <WebSocketClient.h>

EEGBridge::WebSocketClient client;
client.setStateCallback([](EEGBridge::WebSocketClient::State state) {
    // Dispatch the observed state to your application's event loop here.
});
if (client.start("ws://127.0.0.1:8765")) {
    // start() launches the connection attempt; it does not wait for Open.
}
// Call client.stop() when the application has finished using the connection.
```

The enum describes EEGBridge's connection process:

| State | Meaning |
| --- | --- |
| `Connecting` | Attempting the first connection, including waiting to retry after failure. |
| `Open` | IXWebSocket has reported that the connection is established. |
| `Closing` | An explicit `stop()` is shutting down the connection. |
| `Closed` | No active connection or connection attempt. |

`start()` publishes `Connecting` before starting IXWebSocket. Its Open event
publishes `Open`; its Close event publishes `Closed` for a lost connection.
Initial connection failures leave the state at `Connecting` while IX retries.
An active client's `stop()` publishes `Closing`, then `Closed` after the IX
network thread exits. Stopping an already closed connection does not add
another state change.

The query and notifications use the same state, updated by these operations
and events without polling. Every change queues a notification in order,
including brief transitions, and repeated assignments of the same state do
not generate notifications. State callbacks run serially on the existing
`std::thread` send worker, outside internal locks. The initial `Closed` state
is not replayed when registering a callback. A queued notification can describe
an earlier state than a subsequent `currentState()` query.

Text and binary callbacks run on the IX network thread. Callbacks may query the
state, enqueue messages, or replace callbacks. Call `stop()` and destroy the
client from outside these callbacks; `stop()` waits for both worker threads.
The client retries until its first successful connection and does not
automatically reconnect after that connection closes. `Closed` describes the
connection, not whether thread cleanup has finished: after a remote close,
call `stop()` before starting the same client again. `stop()` drains pending
state notifications and joins the workers before returning.

## Running the WebSocket example

`example/main.cpp` shows application-level use of `WebSocketClient`: registering
all three callbacks, querying state, sending text and binary messages, stopping,
and connecting again. It only includes EEGBridge's public header.

Build the `EEGBridgeExample` target (`EEGBRIDGE_BUILD_EXAMPLES=ON`) and start your
WebSocket server. With the default Release build directory, run:

```powershell
.\cmake-build-release\bin\EEGBridgeExample.exe
# Or supply a different server URL:
.\cmake-build-release\bin\EEGBridgeExample.exe ws://127.0.0.1:8765
```

Wait for `[state] Open`, then enter text or one of these commands:

| Input | Action |
| --- | --- |
| Any other line | Send the line as a text frame. |
| `/binary` | Send four bytes: `00 01 02 ff`, including a null byte. |
| `/state` | Print the current connection state. |
| `/stop` | Stop the client and wait for its workers to exit. |
| `/connect` | Start another connection attempt after `/stop`. |
| `/quit` | Stop the client and exit. |

Received text is printed with `[text]`; binary messages are printed as hexadecimal
bytes with `[binary]`. An echo server returns the messages you send; another
server may respond according to its own protocol. `[send] Queued` means accepted
into the local send queue, not confirmation of remote receipt.

Receive callbacks process messages directly without another receive queue.
`std::osyncstream` keeps console output from the application and callback threads
from interleaving. Keep real callback processing brief, and copy message contents
if passing them to another thread for later use.

## Building with CLion

1. Open the project directory in CLion.
2. Configure a Visual Studio toolchain using MSVC from Visual Studio 2022.
3. Select that toolchain in the project's CMake profile.
4. Reload the CMake project and build the `EEGBridge` target.

The project builds a shared library by default. Set `BUILD_SHARED_LIBS=OFF` in the CMake options to build a static library.
