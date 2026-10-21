#pragma once
/**
 * ==================================================
 *  @file        WebSocketClient.h
 *  @brief       TODO 
 *  @author      邢嘉一
 *  @date        2026/10/21
 *  @version     1.0
 *  @copyright   Copyright (c) 2026 SUISHI Intelligence. All Rights Reserved.
 * ==================================================
 */
#ifndef EEGBRIDGE_WEBSOCKETCLIENT_H
#define EEGBRIDGE_WEBSOCKETCLIENT_H
#include "EEGBridge_export.h"
#include <functional>
#include <string>
#include <memory>

namespace EEGBridge {

    class EEGBRIDGE_EXPORT WebSocketClient final {

    public:

        enum class State {
            Connecting, // Includes waiting to retry the first connection.
            Open,
            Closing, // An explicit stop is shutting down the connection.
            Closed
        };

        using StateCallback = std::function<void(State)>;

        using MessageCallback = std::function<void(const std::string &)>;

        using BinaryCallback = std::function<void(const std::string &)>;

        WebSocketClient();

        ~WebSocketClient();

        //Copy and move are disabled
        WebSocketClient(const WebSocketClient &) = delete;

        WebSocketClient &operator=(const WebSocketClient &) = delete;

        WebSocketClient(WebSocketClient &&) = delete;

        WebSocketClient &operator=(WebSocketClient &&) = delete;

        // async start connect websocket server
        [[nodiscard]] bool start(const std::string &url = "ws://127.0.0.1:8765");

        void stop();

        // send type of message
        [[nodiscard]] bool sendText(std::string message);

        // send type of binary
        [[nodiscard]] bool sendBinary(std::string data);

        //set message callback
        void setTextCallback(MessageCallback callback);

        //set binary callback
        void setBinaryCallback(BinaryCallback callback);

        // State callbacks run on the send worker; text/binary callbacks run on the IX thread.
        // Callbacks may query state or replace callbacks, but must not call stop() or destroy this client.
        // Changes are queued in order; a later query may return a newer state than a notification.
        void setStateCallback(StateCallback callback);

        // EEGBridge connection state, including connection attempts and explicit shutdown.
        [[nodiscard]] State currentState() const;

    private:

        // Internal Implementation
        class Impl;

        std::unique_ptr<Impl> impl_;
    };
}




#endif //EEGBRIDGE_WEBSOCKETCLIENT_H
