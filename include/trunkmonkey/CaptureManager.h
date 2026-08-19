#pragma once
#include "trunkmonkey/CallSnapshot.h"
#include <cstdint>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#endif
namespace trunkmonkey {
class Logger;
enum class CaptureKind { Sip, Rtp, Call };
class CaptureManager {
public:
    explicit CaptureManager(Logger& logger);
    ~CaptureManager();
    void startSip(const std::string& path,unsigned localSipPort,const std::string& interfaceName="any");
    void startRtp(const std::string& path,const CallSnapshot& call,const std::string& interfaceName="any");
    void startCall(const std::string& path,unsigned localSipPort,const CallSnapshot& call,const std::string& interfaceName="any");
    void stop(CaptureKind kind);
    void stopAll();
    bool active(CaptureKind kind)const;
    std::string status()const;

    // Shared CLI/GUI capture discovery. These do not require an active SIP engine.
    static std::string captureTool();
    static std::vector<std::string> availableInterfaces();
    static std::string permissionHint();

    // Launch Wireshark with this call's dynamic media ports pre-decoded.
    // The capture remains a normal PCAP/PCAPNG; Decode As is supplied on
    // Wireshark's command line so the operator does not have to set it by hand.
    static std::string wiresharkTool();
    static std::vector<std::string> wiresharkDecodeArguments(const std::string& path,const CallSnapshot& call);
    static std::vector<std::string> wiresharkSipDecodeArguments(const std::string& path,unsigned localSipPort);
    static void openInWireshark(const std::string& path,const CallSnapshot& call);
    static void openSipInWireshark(const std::string& path,unsigned localSipPort);
private:
    struct Proc {
#ifdef _WIN32
        HANDLE process{nullptr};
        DWORD pid{0};
        bool pktmon{false};
        std::string etlPath;
#else
        pid_t pid{-1};
        std::string errorPath;
#endif
        std::string path;
        std::string tool;
        std::string filter;
        bool running{false};
    };
    void start(Proc& proc,const std::string& path,const std::string& filter,const std::string& interfaceName);
    void stopProc(Proc& proc);
    static std::string findTool();
    static std::string rtpFilter(const CallSnapshot& call);
    Logger& logger_;
    Proc sip_,rtp_,call_;
};
}
