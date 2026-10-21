#pragma once

#include "EEGBridge_export.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace EEGBridge {

struct EegConfig {
    std::string name = "EEG";
    std::string sourceId;
    int channelCount = 0;
    double sampleRate = 0.0;
    std::size_t maxQueuedChunks = 32;
    // Pending owned buffers, including the retained capacity of moved vectors.
    std::size_t maxQueuedBytes = 8 * 1024 * 1024;
    int maxBufferedSeconds = 2;
};

struct MarkerConfig {
    std::string name = "Markers";
    std::string sourceId;
    std::size_t maxQueuedMarkers = 1024;
};

// Seconds on the LSL clock, not Unix time.
[[nodiscard]] EEGBRIDGE_EXPORT double localClock() noexcept;

class EEGBRIDGE_EXPORT EegOutlet final {
public:
    // Validates configuration and launches a std::thread; LSL creation runs on that thread.
    explicit EegOutlet(const EegConfig& config);
    // Waits for worker cleanup. Destroy on a non-UI thread if shutdown latency matters.
    ~EegOutlet();

    EegOutlet(const EegOutlet&) = delete;
    EegOutlet& operator=(const EegOutlet&) = delete;
    EegOutlet(EegOutlet&&) = delete;
    EegOutlet& operator=(EegOutlet&&) = delete;

    // Sample-major float32 data. Zero timestamps are captured at submission, before queuing.
    // Data can queue during creation/reconfiguration; late subscribers do not receive past data.
    // The span overload copies; the vector overload transfers ownership without copying samples.
    // Queue overflow evicts oldest pending chunks. A chunk exceeding the byte limit is rejected.
    // false means busy, stopped, failed, or too large; true means accepted, not delivered.
    [[nodiscard]] bool pushChunk(std::span<const float> data, double lastSampleTimestamp = 0.0);
    [[nodiscard]] bool pushChunk(std::vector<float>&& data, double lastSampleTimestamp = 0.0);

    // Asynchronous: drops old pending chunks and switches to the latest accepted configuration.
    // Subsequent submissions use that configuration; an in-flight old chunk may finish first.
    // Call directly while running, without stop(); receivers must rediscover the new stream info.
    // false means busy or stopped. Invalid configurations throw std::invalid_argument.
    [[nodiscard]] bool reconfigure(const EegConfig& config);

    // Requests shutdown and discards pending data; does not join or wait for LSL cleanup.
    void stop();
    // Ready means the latest requested outlet exists, not that any receiver is connected.
    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] bool isStopped() const noexcept;
    [[nodiscard]] std::string lastError() const;
    [[nodiscard]] std::size_t droppedChunks() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class EEGBRIDGE_EXPORT MarkerOutlet final {
public:
    explicit MarkerOutlet(const MarkerConfig& config);
    // Like EegOutlet, destruction waits for worker cleanup; stop() only requests shutdown.
    ~MarkerOutlet();

    MarkerOutlet(const MarkerOutlet&) = delete;
    MarkerOutlet& operator=(const MarkerOutlet&) = delete;
    MarkerOutlet(MarkerOutlet&&) = delete;
    MarkerOutlet& operator=(MarkerOutlet&&) = delete;

    // Single-channel int32, irregular rate. No filtering or deduplication, including code zero.
    // Queue overflow rejects this marker instead of evicting an older event.
    // false means busy, stopped, failed, or full; timestamp zero is captured at submission.
    [[nodiscard]] bool push(std::int32_t code, double timestamp = 0.0);
    void stop();
    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] bool isStopped() const noexcept;
    [[nodiscard]] std::string lastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace EEGBridge
