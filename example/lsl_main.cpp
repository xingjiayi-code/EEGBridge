#include "LslOutlet.h"

#include <cstdint>
#include <iostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class LslExample {
public:
    LslExample()
        : sessionId_("eegbridge-example-" + std::to_string(EEGBridge::localClock())),
          config_(makeConfig(sessionId_)), eeg_(config_),
          markers_({"EEGBridgeDemo-Markers", sessionId_ + "-markers"}),
          samples_(samplesPerChunk_ * static_cast<std::size_t>(config_.channelCount)) {}

    void run() {
        std::cout << "EEG stream: " << config_.name << ", source_id=" << config_.sourceId << '\n'
                  << "Marker stream: EEGBridgeDemo-Markers, source_id=" << sessionId_ << "-markers\n"
                  << "Creation is asynchronous. Use /status to check readiness.\n"
                  << "Ready means the outlet exists; connect your receiver before sending data.\n"
                  << "This demo generates one synthetic batch per /eeg command." << std::endl;
        printHelp();

        std::string line;
        while (std::getline(std::cin, line)) {
            std::istringstream input(line);
            std::string command;
            input >> command;
            try {
                if (command == "/quit") break;
                if (command == "/eeg") {
                    pushEeg();
                } else if (command == "/marker") {
                    std::int32_t code;
                    if (input >> code) {
                        // Omitted timestamps use the LSL clock at submission, not at background send.
                        std::cout << "[marker] " << (markers_.push(code) ? "queued" : "rejected") << std::endl;
                    } else {
                        std::cout << "Usage: /marker <int32 code>" << std::endl;
                    }
                } else if (command == "/config") {
                    int channels;
                    double rate;
                    if (input >> channels >> rate) {
                        reconfigure(channels, rate);
                    } else {
                        std::cout << "Usage: /config <channels> <sample_rate>" << std::endl;
                    }
                } else if (command == "/status") {
                    printStatus();
                } else if (command == "/stop") {
                    stop();
                } else {
                    printHelp();
                }
            } catch (const std::invalid_argument& e) {
                std::cout << "[input error] " << e.what() << std::endl;
            }
        }
        stop();
        // The destructors join the workers here at application shutdown, not on each push.
    }

private:
    static constexpr std::size_t samplesPerChunk_ = 4;

    static EEGBridge::EegConfig makeConfig(const std::string& sessionId) {
        EEGBridge::EegConfig config;
        config.name = "EEGBridgeDemo-EEG";
        config.sourceId = sessionId + "-eeg-0";
        config.channelCount = 2;
        config.sampleRate = 250.0;
        config.maxQueuedChunks = 32;
        config.maxQueuedBytes = 8 * 1024 * 1024;
        return config;
    }

    void pushEeg() {
        if (!eeg_.isReady()) {
            std::cout << "[eeg] not ready; check /status" << std::endl;
            return;
        }

        const auto channels = static_cast<std::size_t>(config_.channelCount);
        for (std::size_t sample = 0; sample < samplesPerChunk_; ++sample) {
            for (std::size_t ch = 0; ch < channels; ++ch) {
                // Sample-major layout: [s0_ch0, s0_ch1, ..., s1_ch0, s1_ch1, ...].
                samples_[sample * channels + ch] = static_cast<float>((sampleOffset_ + sample) * 10 + ch);
            }
        }

        // This is the LAST sample's time. Earlier sample times follow the configured sample rate.
        const double lastTimestamp = EEGBridge::localClock();
        // span copies into the background queue, so samples_ can be reused after this call.
        // For a buffer whose ownership can be transferred, pushChunk(std::move(buffer), time) avoids that copy.
        if (eeg_.pushChunk(std::span<const float>(samples_), lastTimestamp)) {
            sampleOffset_ += samplesPerChunk_;
            std::cout << "[eeg] queued " << samplesPerChunk_ << " samples, " << channels
                      << " channels, " << config_.sampleRate << " Hz" << std::endl;
        } else {
            std::cout << "[eeg] rejected; check /status (the queue lock may also be busy)" << std::endl;
        }
    }

    void reconfigure(int channels, double rate) {
        if (channels > 0 && static_cast<std::size_t>(channels) >
            config_.maxQueuedBytes / sizeof(float) / samplesPerChunk_) {
            throw std::invalid_argument("The example's chunk would exceed the queue byte limit");
        }
        auto next = config_;
        next.channelCount = channels;
        next.sampleRate = rate;
        // A new source identity separates incompatible stream configurations at the receiver.
        next.sourceId = sessionId_ + "-eeg-" + std::to_string(revision_ + 1);
        if (!eeg_.reconfigure(next)) {
            std::cout << "[config] rejected: busy or stopped; configuration unchanged" << std::endl;
            return;
        }
        config_ = std::move(next);
        ++revision_;
        samples_.resize(samplesPerChunk_ * static_cast<std::size_t>(config_.channelCount));
        sampleOffset_ = 0;
        std::cout << "[config] requested " << config_.channelCount << " channels, " << config_.sampleRate
                  << " Hz, source_id=" << config_.sourceId << '\n'
                  << "Use /status until ready, then reconnect the EEG receiver to this new source_id.\n"
                  << "The marker stream remains unchanged." << std::endl;
    }

    void printStatus() const {
        std::cout << "[eeg] ready=" << eeg_.isReady() << " stopped=" << eeg_.isStopped()
                  << " dropped_chunks=" << eeg_.droppedChunks() << " error=" << eeg_.lastError() << '\n'
                  << "[markers] ready=" << markers_.isReady() << " stopped=" << markers_.isStopped()
                  << " error=" << markers_.lastError() << std::endl;
    }

    void stop() {
        eeg_.stop();
        markers_.stop();
        std::cout << "[stop] requested; /status reports when background cleanup finishes" << std::endl;
    }

    static void printHelp() {
        std::cout << "Commands: /status, /eeg, /marker <code>, /config <channels> <Hz>, /stop, /quit\n"
                  << "Queued means accepted locally, not confirmed by the receiver.\n"
                  << "/stop is terminal for these objects; restart the example to publish again." << std::endl;
    }

    std::string sessionId_;
    EEGBridge::EegConfig config_;
    EEGBridge::EegOutlet eeg_;
    EEGBridge::MarkerOutlet markers_;
    std::vector<float> samples_;
    std::size_t sampleOffset_ = 0;
    unsigned int revision_ = 0;
};

int main() {
    try {
        LslExample example;
        example.run();
    } catch (const std::exception& e) {
        std::cerr << "LSL example failed: " << e.what() << '\n';
        return 1;
    }
}
