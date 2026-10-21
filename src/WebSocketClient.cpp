/**
 * ==================================================
 *  @file        WebSocketClient.cpp
 *  @brief       TODO 
 *  @author      邢嘉一
 *  @date        2026/10/21
 *  @version     1.0
 *  @copyright   Copyright (c) 2026 SUISHI Intelligence. All Rights Reserved.
 * ==================================================
 */
#include "WebSocketClient.h"
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>
namespace EEGBridge {

    class WebSocketClient::Impl final
    {
    public:
        using MessageCallback = WebSocketClient::MessageCallback;
        using BinaryMessageCallback =
            WebSocketClient::BinaryCallback;

        Impl() = default;

        ~Impl()
        {
            stop();
        }

        bool start(std::string url)
        {
            if (url.empty())
            {
                return false;
            }

            std::unique_lock lifecycleLock(lifecycleMutex_);

            // 不允许重复启动，也不允许停止过程中重新启动
            if (started_ || stopping_)
            {
                return false;
            }

            // Windows 下初始化 Winsock。
            // 必须在创建 IXWebSocket 对象之前完成。
            if (!ix::initNetSystem())
            {
                std::cerr << "[WebSocketClient] Network initialization failed\n";
                return false;
            }

            try
            {
                socket_ = std::make_unique<ix::WebSocket>();

                socket_->setUrl(url);

                socket_->setOnMessageCallback(
                    [this](const ix::WebSocketMessagePtr& message)
                    {
                        handleSocketMessage(message);
                    });

                // 首次连接失败时自动重试。
                // 第一次 Open 事件中会关闭自动重连。
                socket_->enableAutomaticReconnection();

                {
                    std::lock_guard queueLock(sendMutex_);

                    sendQueue_.clear();
                    pendingStates_.clear();
                    queuedBytes_ = 0;
                    senderStopRequested_ = false;
                    changeState(State::Connecting);
                }

                started_ = true;

                // 启动发送工作线程
                senderThread_ = std::thread(
                    [this]
                    {
                        runSender();
                    });

                // IXWebSocket 自己管理网络线程和连接重试
                socket_->start();

                return true;
            }
            catch (const std::exception& e)
            {
                std::cerr << "[WebSocketClient] Start failed: "
                          << e.what() << '\n';

                started_ = false;
                stopping_ = true;

                {
                    std::lock_guard queueLock(sendMutex_);

                    sendQueue_.clear();
                    queuedBytes_ = 0;
                    changeState(State::Closed);
                }

                lifecycleLock.unlock();

                if (socket_)
                {
                    socket_->stop();
                }

                stopSender();

                socket_.reset();
                ix::uninitNetSystem();

                lifecycleLock.lock();
                stopping_ = false;
                lifecycleLock.unlock();

                lifecycleCv_.notify_all();

                return false;
            }
        }

        bool sendText(std::string message)
        {
            return enqueue(std::move(message), false);
        }

        bool sendBinary(std::string data)
        {
            return enqueue(std::move(data), true);
        }

        void setTextCallback(MessageCallback callback)
        {
            std::lock_guard lock(callbackMutex_);
            messageCallback_ = std::move(callback);
        }

        void setBinaryCallback(BinaryMessageCallback callback)
        {
            std::lock_guard lock(callbackMutex_);
            binaryMessageCallback_ = std::move(callback);
        }

        void setStateCallback(StateCallback callback)
        {
            std::lock_guard lock(callbackMutex_);
            stateCallback_ = std::move(callback);
        }

        State currentState() const
        {
            std::lock_guard lock(sendMutex_);
            return state_;
        }

        void stop()
        {
            std::unique_lock lifecycleLock(lifecycleMutex_);

            // 等待另一个线程正在进行的 stop() 完成
            lifecycleCv_.wait(
                lifecycleLock,
                [this]
                {
                    return !stopping_;
                });

            if (!started_)
            {
                return;
            }

            stopping_ = true;
            started_ = false;

            {
                std::lock_guard queueLock(sendMutex_);

                sendQueue_.clear();
                queuedBytes_ = 0;
                if (state_ != State::Closed)
                {
                    changeState(State::Closing);
                }
            }
            sendCv_.notify_one();

            lifecycleLock.unlock();

            // Keep the socket alive until the sender has delivered the final state notifications.
            socket_->stop();
            {
                std::lock_guard lock(sendMutex_);
                changeState(State::Closed);
            }

            stopSender();

            socket_.reset();

            // 所有使用该网络运行时的对象均已停止
            ix::uninitNetSystem();

            lifecycleLock.lock();
            stopping_ = false;
            lifecycleLock.unlock();

            lifecycleCv_.notify_all();
        }

    private:
        struct PendingMessage
        {
            std::string data;
            bool binary = false;
        };

        // 内部发送队列的容量限制
        static constexpr std::size_t maxQueuedMessages_ = 256;
        static constexpr std::size_t maxQueuedBytes_ = 1024 * 1024;

        bool enqueue(std::string data, bool binary)
        {
            {
                std::lock_guard lock(sendMutex_);

                if (state_ != State::Open)
                {
                    return false;
                }

                if (sendQueue_.size() >= maxQueuedMessages_)
                {
                    return false;
                }

                if (data.size() > maxQueuedBytes_ - queuedBytes_)
                {
                    return false;
                }

                const auto dataSize = data.size();

                sendQueue_.push_back(
                    PendingMessage{std::move(data), binary});

                queuedBytes_ += dataSize;
            }

            sendCv_.notify_one();
            return true;
        }

        void stopSender()
        {
            {
                std::lock_guard lock(sendMutex_);
                senderStopRequested_ = true;
            }
            sendCv_.notify_all();
            if (senderThread_.joinable())
            {
                senderThread_.join();
            }
        }

        void runSender()
        {
            std::deque<State> states;
            while (true)
            {
                PendingMessage message;
                bool hasMessage = false;
                states.clear();

                {
                    std::unique_lock lock(sendMutex_);

                    sendCv_.wait(
                        lock,
                        [this]
                        {
                            return senderStopRequested_ || !sendQueue_.empty() || !pendingStates_.empty();
                        });

                    states.swap(pendingStates_);

                    if (!senderStopRequested_ && state_ == State::Open && !sendQueue_.empty())
                    {
                        message = std::move(sendQueue_.front());
                        sendQueue_.pop_front();
                        queuedBytes_ -= message.data.size();
                        hasMessage = true;
                    }
                }

                for (const auto state : states)
                {
                    notifyState(state);
                }

                {
                    std::lock_guard lock(sendMutex_);
                    if (senderStopRequested_)
                    {
                        if (pendingStates_.empty())
                        {
                            return;
                        }
                        continue;
                    }
                }

                // 连接可能在消息出队后断开
                // 这种情况下丢弃该消息，不在以后重连时补发
                if (!hasMessage || currentState() != State::Open)
                {
                    continue;
                }

                // 文本和二进制消息共用队列，但使用不同的帧类型
                const auto result = message.binary
                    ? socket_->sendBinary(message.data)
                    : socket_->sendText(message.data);

                if (!result.success &&
                    currentState() == State::Open)
                {
                    std::clog
                        << "[WebSocketClient] Message send failed\n";
                }
            }
        }

        // Requires sendMutex_: update the query result and queue its notification together.
        void changeState(State state)
        {
            if (state_ == state)
            {
                return;
            }
            pendingStates_.push_back(state);
            state_ = state;
        }

        void notifyState(State state)
        {
            StateCallback callback;
            {
                std::lock_guard lock(callbackMutex_);
                callback = stateCallback_;
            }
            if (callback)
            {
                try
                {
                    callback(state);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "[WebSocketClient] State callback failed: " << e.what() << '\n';
                }
                catch (...)
                {
                    std::cerr << "[WebSocketClient] State callback threw an exception\n";
                }
            }
        }

        void handleSocketMessage(
            const ix::WebSocketMessagePtr& message)
        {
            if (!message)
            {
                return;
            }

            switch (message->type)
            {
                case ix::WebSocketMessageType::Open:
                {
                    // 第一次连接成功后关闭自动重连。
                    // IXWebSocket 会在该回调返回后继续网络事件循环。
                    socket_->disableAutomaticReconnection();

                    {
                        std::lock_guard lock(sendMutex_);
                        // An Open event can race with an explicit stop request.
                        if (state_ == State::Connecting)
                        {
                            changeState(State::Open);
                        }
                    }
                    sendCv_.notify_one();
                    break;
                }

                case ix::WebSocketMessageType::Close:
                {
                    // 丢弃断线时尚未发送的消息
                    {
                        std::lock_guard lock(sendMutex_);
                        sendQueue_.clear();
                        queuedBytes_ = 0;
                        // stop() publishes Closed after the IX network thread has exited.
                        if (state_ == State::Open)
                        {
                            changeState(State::Closed);
                        }
                    }
                    sendCv_.notify_one();
                    break;
                }

                case ix::WebSocketMessageType::Error:
                {
                    // 首次连接失败时会收到错误事件。
                    // IXWebSocket 负责按照其退避策略继续尝试。
                    std::clog
                        << "[WebSocketClient] Connection error: "
                        << message->errorInfo.reason << '\n';
                    break;
                }

                case ix::WebSocketMessageType::Message:
                {
                    if (message->binary)
                    {
                        BinaryMessageCallback callback;

                        {
                            std::lock_guard lock(callbackMutex_);
                            callback = binaryMessageCallback_;
                        }

                        if (callback)
                        {
                            try
                            {
                                callback(message->str);
                            }
                            catch (const std::exception& e)
                            {
                                std::cerr
                                    << "[WebSocketClient] Binary callback failed: "
                                    << e.what() << '\n';
                            }
                            catch (...)
                            {
                                std::cerr
                                    << "[WebSocketClient] Binary callback threw an exception\n";
                            }
                        }
                    }
                    else
                    {
                        MessageCallback callback;

                        {
                            std::lock_guard lock(callbackMutex_);
                            callback = messageCallback_;
                        }

                        if (callback)
                        {
                            try
                            {
                                callback(message->str);
                            }
                            catch (const std::exception& e)
                            {
                                std::cerr
                                    << "[WebSocketClient] Message callback failed: "
                                    << e.what() << '\n';
                            }
                            catch (...)
                            {
                                std::cerr
                                    << "[WebSocketClient] Message callback threw an exception\n";
                            }
                        }
                    }

                    break;
                }

                default:
                    // 忽略 Ping、Pong 等非业务消息
                    break;
            }
        }

    private:
        // 生命周期
        std::mutex lifecycleMutex_;
        std::condition_variable lifecycleCv_;
        bool started_ = false;
        bool stopping_ = false;

        // 底层 WebSocket
        // 在网络初始化完成后创建，在网络清理前销毁
        std::unique_ptr<ix::WebSocket> socket_;

        // 回调
        std::mutex callbackMutex_;
        MessageCallback messageCallback_;
        BinaryMessageCallback binaryMessageCallback_;
        StateCallback stateCallback_;

        // 发送队列
        mutable std::mutex sendMutex_;
        std::condition_variable sendCv_;
        std::deque<PendingMessage> sendQueue_;
        std::deque<State> pendingStates_;
        State state_ = State::Closed;
        std::size_t queuedBytes_ = 0;
        bool senderStopRequested_ = false;

        // 发送线程
        std::thread senderThread_;
    };


    //===================public interface Implementation========

    WebSocketClient::WebSocketClient():impl_(std::make_unique<Impl>()){};

    WebSocketClient::~WebSocketClient() = default;

    bool WebSocketClient::start(const std::string &url) {
        return impl_->start(url);
    }

    void WebSocketClient::stop() {
        impl_->stop();
    }

    bool WebSocketClient::sendText(std::string message) {
        return impl_->sendText(std::move(message));
    }

    bool WebSocketClient::sendBinary(std::string data) {
        return impl_->sendBinary(std::move(data));
    }

    void WebSocketClient::setTextCallback(MessageCallback callback) {
        impl_->setTextCallback(std::move(callback));
    }

    void WebSocketClient::setBinaryCallback(BinaryCallback callback) {
        impl_->setBinaryCallback(std::move(callback));
    }

    void WebSocketClient::setStateCallback(StateCallback callback) {
        impl_->setStateCallback(std::move(callback));
    }

    WebSocketClient::State WebSocketClient::currentState() const {
        return impl_->currentState();
    }
}
