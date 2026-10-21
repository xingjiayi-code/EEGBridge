#include "WebSocketClient.h"

#include <iomanip>
#include <iostream>
#include <string>
#include <syncstream>
#include <utility>

class WebSocketExample {
public:
    WebSocketExample() {
        client_.setStateCallback([](State state) {
            std::osyncstream(std::cout) << "[state] " << stateName(state) << std::endl;
        });

        // Receive callbacks run on the IX network thread; keep processing brief.
        client_.setTextCallback([](const std::string& message) {
            std::osyncstream(std::cout) << "[text] " << message << std::endl;
        });

        client_.setBinaryCallback([](const std::string& data) {
            std::osyncstream out(std::cout);
            out << "[binary] " << data.size() << " bytes:" << std::hex << std::setfill('0');
            for (const unsigned char byte : data) {
                out << ' ' << std::setw(2) << static_cast<unsigned int>(byte);
            }
            out << std::endl;
        });
    }

    int run(const std::string& url = "ws://127.0.0.1:8765") {
        std::osyncstream(std::cout)
            << "Server: " << url << '\n'
            << "Type a message to send text. Wait for Open before sending.\n"
            << "Commands: /state, /binary, /stop, /connect, /quit" << std::endl;

        // start() is asynchronous; connection success arrives through the state callback.
        if (!client_.start(url)) {
            std::osyncstream(std::cerr) << "Could not start the WebSocket client.\n";
            return 1;
        }

        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "/quit") {
                break;
            }
            if (line == "/state") {
                std::osyncstream(std::cout) << "[current state] " << stateName(client_.currentState()) << std::endl;
            } else if (line == "/binary") {
                reportSend(client_.sendBinary(std::string("\x00\x01\x02\xff", 4)));
            } else if (line == "/stop") {
                client_.stop();
            } else if (line == "/connect") {
                if (!client_.start(url)) {
                    std::osyncstream(std::cout) << "Could not start. Use /stop before connecting again." << std::endl;
                }
            } else {
                reportSend(client_.sendText(std::move(line)));
            }
        }

        // stop() joins the workers, so call it from the application thread.
        client_.stop();
        return 0;
    }

private:
    using State = EEGBridge::WebSocketClient::State;

    static const char* stateName(State state) {
        switch (state) {
            case State::Connecting: return "Connecting";
            case State::Open: return "Open";
            case State::Closing: return "Closing";
            case State::Closed: return "Closed";
        }
        return "Unknown";
    }

    static void reportSend(bool accepted) {
        // A successful send call means queued locally, not acknowledged by the server.
        std::osyncstream(std::cout)
            << (accepted ? "[send] Queued" : "[send] Rejected: connection is not Open or the queue is full")
            << std::endl;
    }

    EEGBridge::WebSocketClient client_;
};

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Usage: EEGBridgeExample [ws://host:port/path]\n";
        return 1;
    }

    WebSocketExample example;
    return argc == 2 ? example.run(argv[1]) : example.run();
}
