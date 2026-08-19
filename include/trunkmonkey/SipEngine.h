#pragma once
#include "trunkmonkey/CallSnapshot.h"
#include "trunkmonkey/Profile.h"
#include "trunkmonkey/SipTrace.h"
#include <pjsua2.hpp>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace trunkmonkey {
class CallSession; class CaptureManager; class Logger; class SipAccount; class SipWireMonitor;
enum class CaptureKind;
struct AudioDeviceInfo { int id{-1}; std::string driver; std::string name; unsigned inputCount{0}; unsigned outputCount{0}; };
class SipEngine {
public:
    explicit SipEngine(Logger& logger); ~SipEngine();
    SipEngine(const SipEngine&)=delete; SipEngine& operator=(const SipEngine&)=delete;
    void start(const SipProfile& profile,unsigned maxCalls=50);
    void stop();
    bool started()const; bool registered()const; std::string registrationText()const;
    const SipProfile& profile()const;
    int makeCall(const std::string& destination,const std::string& callerId={},bool makeForeground=true,CallPurpose purpose=CallPurpose::Phone);
    void answer(int id); void reject(int id,int code=603); void hangup(int id); void hangupAll();
    void hold(int id); void resume(int id); void sendDtmf(int id,const std::string& digits,unsigned durationMs=0);
    void setMicrophoneMuted(int id,bool muted);
    std::vector<AudioDeviceInfo> audioDevices()const;
    int activeCaptureDevice()const; int activePlaybackDevice()const;
    void selectAudioDevices(int captureId,int playbackId);
    void selectPlaybackDevice(int playbackId);
    void setCallAudioFile(int id,const std::string& path);
    void setForeground(int id); void clearForeground();
    std::vector<CallSnapshot> calls()const;
    CallSnapshot callSnapshot(int id)const;
    std::string mediaDump(int id)const;
    std::string sipLadder(int id)const;
    std::string callReport(int id)const;
    void exportCallReport(int id,const std::string& path)const;

    // Detailed diagnostics are intentionally exposed for normal Phone calls only.
    std::vector<SipTraceEntry> sipTrace(int id)const;
    void startSipTraceFile(int id,const std::string& path);
    void stopSipTraceFile(int id);
    bool sipTraceRecording(int id)const;
    std::string sipTracePath(int id)const;
    void startSipPcap(int id,const std::string& path,const std::string& interfaceName="any");
    void startRtpPcap(int id,const std::string& path,const std::string& interfaceName="any");
    void startCallPcap(int id,const std::string& path,const std::string& interfaceName="any");
    void stopCapture(CaptureKind kind);
    void stopCaptures();
    std::string captureStatus()const;
    void openPcapInWireshark(int id,const std::string& path)const;

    std::string normalizeDestination(const std::string& value)const;
    std::string callerIdentityUri(const std::string& value)const;
    void onIncomingCall(int id);
    void onRegistrationState(bool active,int code,const std::string& reason);

    // Called by SipWireMonitor from PJSIP worker threads.
    void onSipMessage(SipTraceEntry entry);
private:
    struct ArchivedCall {
        CallSnapshot snapshot;
        std::vector<SipTraceEntry> sipTrace;
        std::string sipTracePath;
    };
    std::shared_ptr<CallSession> findCall(int id)const;
    const ArchivedCall* findArchivedCallLocked(int id)const;
    std::shared_ptr<CallSession> requirePhoneCall(int id)const;
    bool addCall(const std::shared_ptr<CallSession>& call);
    void archiveDisconnectedCall(const std::shared_ptr<CallSession>& call,const CallSnapshot& state);
    void onCallUpdated(int id);
    void configureIdentity(pj::CallOpParam& prm,const std::string& callerId)const;
    Logger& logger_;
    mutable std::mutex mutex_;
    // Serializes call-wrapper creation against stop()/profile reload.
    mutable std::mutex callCreateMutex_;
    SipProfile profile_;
    std::unique_ptr<pj::Endpoint> endpoint_;
    std::unique_ptr<SipAccount> account_;
    std::unique_ptr<SipWireMonitor> sipMonitor_;
    std::unique_ptr<CaptureManager> captures_;
    std::map<int,std::shared_ptr<CallSession>> calls_;
    std::map<int,ArchivedCall> archivedCalls_;
    std::map<std::string,int> callIdIndex_;
    std::map<std::string,std::vector<SipTraceEntry>> pendingSip_;
    int foregroundId_{-1};
    std::atomic<bool> started_{false},registered_{false},stopping_{false};
    mutable std::mutex regMutex_;
    std::string registrationText_{"Stopped"};
    std::vector<std::string> registrationHistory_;
public:
    std::vector<std::string> registrationHistory()const;
};
}
