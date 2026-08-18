#include "trunkmonkey/MultiCallManager.h"
#include "trunkmonkey/Logger.h"
#include "trunkmonkey/SipEngine.h"
#include "trunkmonkey/RuntimePaths.h"
#include <pj/os.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <sstream>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <stdexcept>

namespace trunkmonkey {
namespace {
std::string shellQuote(const std::string& value)
{
#ifdef _WIN32
    std::string out="\"";
    for(char c:value){if(c=='\"')out+="\\\"";else out.push_back(c);}out+="\"";return out;
#else
    std::string out="'";
    for(char c:value){if(c=='\'')out+="'\\''";else out.push_back(c);}out+="'";return out;
#endif
}

std::string findExecutable(const std::string& name)
{
    if(const char* path=std::getenv("PATH")){
#ifdef _WIN32
        constexpr char separator=';';
        const std::string executable=(std::filesystem::path(name).extension().empty()?name+".exe":name);
#else
        constexpr char separator=':';
        const std::string executable=name;
#endif
        std::stringstream ss(path);std::string dir;
        while(std::getline(ss,dir,separator)){if(dir.empty())dir=".";const auto p=std::filesystem::path(dir)/executable;std::error_code ec;if(std::filesystem::is_regular_file(p,ec)&&!ec)return p.string();}
    }
#ifdef _WIN32
    const char* portableRoot=std::getenv("SIPHER_PORTABLE_ROOT");if(!portableRoot)portableRoot=std::getenv("SAK_PORTABLE_ROOT");if(portableRoot){const auto p=std::filesystem::path(portableRoot)/"tools"/(name+".exe");std::error_code ec;if(std::filesystem::is_regular_file(p,ec)&&!ec)return p.string();}
#endif
    return {};
}

std::string prepareQueueAudio(const std::string& input)
{
    if(input.empty())return {};
    if(!std::filesystem::is_regular_file(input))throw std::runtime_error("Queue audio file not found: "+input);
    const auto ffmpeg=findExecutable("ffmpeg");if(ffmpeg.empty())throw std::runtime_error("Queue audio requires ffmpeg. Re-run ./build.sh so the builder can install it.");
    runtime::ensureUserDirectories();
#ifndef _WIN32
    const auto pid=static_cast<unsigned long>(::getpid());
#else
    const auto pid=static_cast<unsigned long>(::GetCurrentProcessId());
#endif
    const auto hash=std::hash<std::string>{}(input);
    const auto output=runtime::tempDir()/("queue-audio-"+std::to_string(pid)+"-"+std::to_string(hash)+".wav");
    const std::string command=shellQuote(ffmpeg)+" -y -loglevel error -i "+shellQuote(input)+" -vn -ac 1 -c:a pcm_s16le "+shellQuote(output.string());
    const int rc=std::system(command.c_str());if(rc!=0||!std::filesystem::is_regular_file(output))throw std::runtime_error("ffmpeg could not convert queue audio to a PJSIP-compatible mono WAV");
    return output.string();
}

class PjThreadRegistration {
public:
    PjThreadRegistration()
    {
        if (pj_thread_is_registered()) {
            return;
        }
        std::memset(desc_, 0, sizeof(desc_));
        const auto status = pj_thread_register("tm-queue-launch", desc_, &thread_);
        if (status != PJ_SUCCESS) {
            throw std::runtime_error("pj_thread_register failed with status " + std::to_string(status));
        }
        registeredHere_ = true;
    }

    ~PjThreadRegistration()
    {
        if (registeredHere_) {
            (void)pj_thread_unregister();
        }
    }

    PjThreadRegistration(const PjThreadRegistration&) = delete;
    PjThreadRegistration& operator=(const PjThreadRegistration&) = delete;

private:
    pj_thread_desc desc_{};
    pj_thread_t* thread_{nullptr};
    bool registeredHere_{false};
};
}

MultiCallManager::MultiCallManager(SipEngine& engine, Logger& logger)
    : engine_(engine), logger_(logger) {}

MultiCallManager::~MultiCallManager()
{
    cancelLaunching();
}

void MultiCallManager::start(const MultiCallPlan& plan)
{
    if (!engine_.started()) {
        throw std::runtime_error("SIP engine is not started");
    }
    if (plan.callCount < 1 || plan.callCount > 50) {
        throw std::runtime_error("callCount must be 1-50");
    }
    if (plan.singleDestination.empty() && plan.destinations.empty()) {
        throw std::runtime_error("No destination configured");
    }
    if (plan.callCount > 1 && plan.launchIntervalMs < 50) {
        throw std::runtime_error("PJSIP 2.17 queue tests require at least 50 ms launch spacing for multi-call batches");
    }
    if (launching_) {
        throw std::runtime_error("A launch batch is already running");
    }
    if (worker_.joinable()) {
        worker_.join();
    }

    MultiCallPlan prepared=plan;
    if(!prepared.audioFile.empty()){
        prepared.preparedAudioFile=prepareQueueAudio(prepared.audioFile);
        logger_.info("Queue audio normalized for PJSIP: "+prepared.preparedAudioFile);
    }

    cancel_ = false;
    launching_ = true;
    try {
        worker_ = std::thread(&MultiCallManager::run, this, std::move(prepared));
    } catch (...) {
        launching_ = false;
        throw;
    }
}

void MultiCallManager::cancelLaunching()
{
    cancel_ = true;
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
        worker_.join();
    }
    launching_ = false;
}

bool MultiCallManager::launching() const
{
    return launching_;
}

void MultiCallManager::run(MultiCallPlan plan)
{
    try {
        PjThreadRegistration registration;
        std::size_t destinationIndex = 0;
        std::size_t callerIdIndex = 0;

        for (std::size_t i = 0; i < plan.callCount && !cancel_; ++i) {
            if (!engine_.started()) {
                logger_.warn("Queue launch stopped because the SIP engine is shutting down");
                break;
            }

            const auto destination = plan.destinations.empty()
                ? plan.singleDestination
                : plan.destinations[destinationIndex++ % plan.destinations.size()];
            const auto callerId = plan.callerIds.empty()
                ? plan.fixedCallerId
                : plan.callerIds[callerIdIndex++ % plan.callerIds.size()];

            try {
                const int id=engine_.makeCall(destination, callerId, false, CallPurpose::QueueTest);
                if(!plan.preparedAudioFile.empty())engine_.setCallAudioFile(id,plan.preparedAudioFile);
            } catch (const pj::Error& e) {
                logger_.error("PJSIP queue launch failed: " + e.info());
            } catch (const std::exception& e) {
                logger_.error("Queue launch failed: " + std::string(e.what()));
            }

            if (i + 1 < plan.callCount && !cancel_) {
                unsigned remaining = plan.launchIntervalMs;
                while (remaining && !cancel_) {
                    const auto slice = remaining > 50 ? 50u : remaining;
                    std::this_thread::sleep_for(std::chrono::milliseconds(slice));
                    remaining -= slice;
                }
            }
        }
    } catch (const std::exception& e) {
        logger_.error("Unable to register/use queue-launch thread with PJSIP: " + std::string(e.what()));
    }
    launching_ = false;
}
} // namespace trunkmonkey
