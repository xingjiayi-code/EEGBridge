#include "LslOutlet.h"

#include <lsl_c.h>

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <variant>

namespace EEGBridge {
namespace {

struct StreamConfig {
    std::string name;
    std::string sourceId;
    int channels;
    double sampleRate;
    lsl_channel_format_t format;
    std::size_t maxItems;
    std::size_t maxBytes;
    int lslBuffer;
};

void validateIdentity(const std::string& name, const std::string& sourceId) {
    if (name.empty() || sourceId.empty() || name.find('\0') != std::string::npos ||
        sourceId.find('\0') != std::string::npos) {
        throw std::invalid_argument("LSL name and sourceId must be nonempty strings without null bytes");
    }
}

StreamConfig streamConfig(const EegConfig& config) {
    validateIdentity(config.name, config.sourceId);
    if (config.channelCount <= 0 || !std::isfinite(config.sampleRate) || config.sampleRate <= 0.0 ||
        config.maxQueuedChunks == 0 || config.maxQueuedBytes / sizeof(float) <
            static_cast<std::size_t>(config.channelCount) || config.maxBufferedSeconds <= 0) {
        throw std::invalid_argument("Invalid EEG channels, sample rate, or buffer limits");
    }
    return {config.name, config.sourceId, config.channelCount, config.sampleRate, cft_float32,
        config.maxQueuedChunks, config.maxQueuedBytes, config.maxBufferedSeconds};
}

StreamConfig streamConfig(const MarkerConfig& config) {
    validateIdentity(config.name, config.sourceId);
    if (config.maxQueuedMarkers == 0 ||
        config.maxQueuedMarkers > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
        config.maxQueuedMarkers > (std::numeric_limits<std::size_t>::max)() / sizeof(std::int32_t)) {
        throw std::invalid_argument("Invalid marker queue limit");
    }
    return {config.name, config.sourceId, 1, 0.0, cft_int32, config.maxQueuedMarkers,
        config.maxQueuedMarkers * sizeof(std::int32_t), static_cast<int>(config.maxQueuedMarkers)};
}

double captureTimestamp(double timestamp) {
    if (!std::isfinite(timestamp) || timestamp < 0.0) {
        throw std::invalid_argument("Timestamp must be zero or a positive finite LSL time");
    }
    return timestamp == 0.0 ? lsl_local_clock() : timestamp;
}

[[noreturn]] void throwLslError(const char* operation) {
    throw std::runtime_error(std::string(operation) + ": " + lsl_last_error());
}

// The two public outlets share one concrete worker implementation, with no public LSL types.
class OutletWorker final {
public:
    explicit OutletWorker(StreamConfig config) : config_(std::move(config)),
        worker_([this] { run(); }) {}

    ~OutletWorker() {
        stop();
        worker_.join();
    }

    bool pushChunk(std::span<const float> data, double timestamp) {
        timestamp = captureTimestamp(timestamp);
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock || !accepting()) return false;
        validateChunk(data.size());
        if (data.size() > config_.maxBytes / sizeof(float)) return false;
        return enqueue({std::vector<float>(data.begin(), data.end()), timestamp});
    }

    bool pushChunk(std::vector<float>&& data, double timestamp) {
        timestamp = captureTimestamp(timestamp);
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock || !accepting()) return false;
        validateChunk(data.size());
        // Capacity is retained by a moved vector and must count toward the memory limit.
        if (data.capacity() > config_.maxBytes / sizeof(float)) return false;
        return enqueue({std::move(data), timestamp});
    }

    bool push(std::int32_t code, double timestamp) {
        timestamp = captureTimestamp(timestamp);
        std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock || !accepting() || queue_.size() == config_.maxItems) return false;
        return enqueue({code, timestamp});
    }

    bool reconfigure(StreamConfig config) {
        std::unique_lock lock(mutex_, std::try_to_lock);
        const auto status = status_.load();
        if (!lock || status == Status::Stopping || status == Status::Stopped) return false;
        discardPending();
        config_ = std::move(config);
        ++generation_;
        error_.clear();
        status_.store(Status::Starting);
        cv_.notify_one();
        return true;
    }

    void stop() {
        {
            std::lock_guard lock(mutex_);
            const auto status = status_.load();
            if (status == Status::Stopping || status == Status::Stopped) return;
            status_.store(Status::Stopping);
            discardPending();
        }
        cv_.notify_one();
    }

    bool isReady() const noexcept { return status_.load() == Status::Ready; }
    bool isStopped() const noexcept { return status_.load() == Status::Stopped; }
    std::size_t droppedChunks() const noexcept { return dropped_.load(); }

    std::string lastError() const {
        std::lock_guard lock(mutex_);
        return error_;
    }

private:
    enum class Status { Starting, Ready, Failed, Stopping, Stopped };

    struct Data {
        std::variant<std::vector<float>, std::int32_t> value;
        double timestamp;

        std::size_t bytes() const {
            const auto* eeg = std::get_if<std::vector<float>>(&value);
            return eeg ? eeg->capacity() * sizeof(float) : sizeof(std::int32_t);
        }
    };

    using Info = std::unique_ptr<lsl_streaminfo_struct_, decltype(&lsl_destroy_streaminfo)>;
    using Outlet = std::unique_ptr<lsl_outlet_struct_, decltype(&lsl_destroy_outlet)>;

    bool accepting() const {
        const auto status = status_.load();
        return status == Status::Starting || status == Status::Ready;
    }

    void validateChunk(std::size_t elements) const {
        if (elements == 0 || elements % static_cast<std::size_t>(config_.channels) != 0 ||
            elements > (std::numeric_limits<unsigned long>::max)()) {
            throw std::invalid_argument("EEG chunk must contain whole samples and fit the LSL element limit");
        }
    }

    // Queue mutation methods require mutex_. All liblsl outlet operations run outside it.
    bool enqueue(Data data) {
        const auto bytes = data.bytes();
        if (bytes > config_.maxBytes) return false;
        while (queue_.size() >= config_.maxItems || bytes > config_.maxBytes - queuedBytes_) {
            queuedBytes_ -= queue_.front().bytes();
            queue_.pop_front();
            ++dropped_;
        }
        queue_.push_back(std::move(data));
        queuedBytes_ += bytes;
        cv_.notify_one();
        return true;
    }

    void discardPending() {
        if (config_.format == cft_float32) dropped_.fetch_add(queue_.size());
        queue_.clear();
        queuedBytes_ = 0;
    }

    static Outlet createOutlet(const StreamConfig& config) {
        const bool eeg = config.format == cft_float32;
        Info info(lsl_create_streaminfo(config.name.c_str(), eeg ? "EEG" : "Markers", config.channels,
            config.sampleRate, config.format, config.sourceId.c_str()), &lsl_destroy_streaminfo);
        if (!info) throwLslError("Creating LSL stream info");
        Outlet outlet(lsl_create_outlet_ex(info.get(), 0, config.lslBuffer,
            eeg ? transp_default : transp_bufsize_samples), &lsl_destroy_outlet);
        if (!outlet) throwLslError("Creating LSL outlet");
        return outlet;
    }

    void run() {
        Outlet outlet(nullptr, &lsl_destroy_outlet);
        std::size_t processedGeneration = 0;
        while (true) {
            std::optional<StreamConfig> nextConfig;
            std::optional<Data> data;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this, processedGeneration] {
                    return status_.load() == Status::Stopping ||
                        generation_ != processedGeneration || !queue_.empty();
                });
                if (status_.load() == Status::Stopping) break;
                if (generation_ != processedGeneration) {
                    nextConfig = config_;
                    processedGeneration = generation_;
                } else {
                    data = std::move(queue_.front());
                    queuedBytes_ -= data->bytes();
                    queue_.pop_front();
                }
            }

            try {
                if (nextConfig) {
                    outlet.reset();
                    auto replacement = createOutlet(*nextConfig);
                    std::lock_guard lock(mutex_);
                    if (status_.load() != Status::Stopping && processedGeneration == generation_) {
                        outlet = std::move(replacement);
                        status_.store(Status::Ready);
                    }
                } else {
                    int result;
                    if (const auto* eeg = std::get_if<std::vector<float>>(&data->value)) {
                        result = lsl_push_chunk_ftp(outlet.get(), eeg->data(),
                            static_cast<unsigned long>(eeg->size()), data->timestamp, 1);
                    } else {
                        const auto& marker = std::get<std::int32_t>(data->value);
                        result = lsl_push_sample_itp(outlet.get(), &marker, data->timestamp, 1);
                    }
                    if (result != lsl_no_error) throwLslError("Pushing LSL data");
                }
            } catch (const std::exception& e) {
                {
                    std::lock_guard lock(mutex_);
                    if (status_.load() != Status::Stopping && processedGeneration == generation_) {
                        error_ = e.what();
                        status_.store(Status::Failed);
                        discardPending();
                    }
                }
                outlet.reset();
            }
        }
        outlet.reset();
        status_.store(Status::Stopped);
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    StreamConfig config_;
    std::size_t generation_ = 1;
    std::deque<Data> queue_;
    std::size_t queuedBytes_ = 0;
    std::string error_;
    std::atomic<Status> status_{Status::Starting};
    std::atomic<std::size_t> dropped_{0};
    std::thread worker_;
};

} // namespace

class EegOutlet::Impl final {
public:
    explicit Impl(const EegConfig& config) : worker(streamConfig(config)) {}
    OutletWorker worker;
};

class MarkerOutlet::Impl final {
public:
    explicit Impl(const MarkerConfig& config) : worker(streamConfig(config)) {}
    OutletWorker worker;
};

double localClock() noexcept { return lsl_local_clock(); }

EegOutlet::EegOutlet(const EegConfig& config) : impl_(std::make_unique<Impl>(config)) {}
EegOutlet::~EegOutlet() = default;
bool EegOutlet::pushChunk(std::span<const float> data, double timestamp) {
    return impl_->worker.pushChunk(data, timestamp);
}
bool EegOutlet::pushChunk(std::vector<float>&& data, double timestamp) {
    return impl_->worker.pushChunk(std::move(data), timestamp);
}
bool EegOutlet::reconfigure(const EegConfig& config) { return impl_->worker.reconfigure(streamConfig(config)); }
void EegOutlet::stop() { impl_->worker.stop(); }
bool EegOutlet::isReady() const noexcept { return impl_->worker.isReady(); }
bool EegOutlet::isStopped() const noexcept { return impl_->worker.isStopped(); }
std::string EegOutlet::lastError() const { return impl_->worker.lastError(); }
std::size_t EegOutlet::droppedChunks() const noexcept { return impl_->worker.droppedChunks(); }

MarkerOutlet::MarkerOutlet(const MarkerConfig& config) : impl_(std::make_unique<Impl>(config)) {}
MarkerOutlet::~MarkerOutlet() = default;
bool MarkerOutlet::push(std::int32_t code, double timestamp) { return impl_->worker.push(code, timestamp); }
void MarkerOutlet::stop() { impl_->worker.stop(); }
bool MarkerOutlet::isReady() const noexcept { return impl_->worker.isReady(); }
bool MarkerOutlet::isStopped() const noexcept { return impl_->worker.isStopped(); }
std::string MarkerOutlet::lastError() const { return impl_->worker.lastError(); }

} // namespace EEGBridge
