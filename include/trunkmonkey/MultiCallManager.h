#pragma once
#include <atomic>
#include <cstddef>
#include <string>
#include <thread>
#include <vector>
namespace trunkmonkey {
class SipEngine; class Logger;
struct MultiCallPlan {
    std::size_t callCount{1};
    unsigned launchIntervalMs{250};
    std::string singleDestination;
    std::string fixedCallerId;
    std::vector<std::string> destinations;
    std::vector<std::string> callerIds;
    std::string audioFile;          // WAV/MP3/etc chosen by operator
    std::string preparedAudioFile;  // normalized 16-bit mono WAV for PJSIP
};
class MultiCallManager {
public:
    MultiCallManager(SipEngine& engine,Logger& logger);~MultiCallManager();
    void start(const MultiCallPlan& plan);void cancelLaunching();bool launching()const;
private:
    void run(MultiCallPlan plan);
    SipEngine& engine_;Logger& logger_;std::atomic<bool>cancel_{false},launching_{false};std::thread worker_;
};
}
