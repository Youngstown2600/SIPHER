#include "trunkmonkey/SipEngine.h"
#include "trunkmonkey/CallSession.h"
#include "trunkmonkey/CaptureManager.h"
#include "trunkmonkey/Logger.h"
#include "trunkmonkey/RuntimePaths.h"
#include "trunkmonkey/SipAccount.h"
#include "trunkmonkey/SipWireMonitor.h"
#include "trunkmonkey/Version.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <thread>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace trunkmonkey {
static_assert(PJSUA_MAX_CALLS >= 50,
              "S.I.P.H.E.R. requires PJSIP built with PJSUA_MAX_CALLS >= 50. Use scripts/build-pjsip.sh.");
static_assert(PJ_IOQUEUE_MAX_HANDLES >= 192,
              "S.I.P.H.E.R. requires PJ_IOQUEUE_MAX_HANDLES >= 192 for 64-call PJSIP. Rebuild PJSIP with scripts/build-pjsip.sh.");

namespace {
std::string trim(std::string value)
{
    const auto isSpace=[](unsigned char c){ return std::isspace(c)!=0; };
    while(!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while(!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

std::string quotedDisplayName(std::string value)
{
    std::string escaped;
    escaped.reserve(value.size()+2);
    for(char c:value){
        if(c=='\\' || c=='"') escaped.push_back('\\');
        escaped.push_back(c);
    }
    return "\""+escaped+"\"";
}

std::string nameAddr(const std::string& value)
{
    return value.find('<')!=std::string::npos ? value : "<"+value+">";
}
}

SipEngine::SipEngine(Logger& logger):logger_(logger){}
SipEngine::~SipEngine(){ stop(); }

void SipEngine::start(const SipProfile& p,unsigned maxCalls)
{
    if(stopping_) throw std::runtime_error("SIP engine is still shutting down");
    if(started_) return;
    if(maxCalls<1 || maxCalls>50) throw std::runtime_error("maxCalls must be 1-50");

    profile_=p;
    registered_=false;
    {
        std::lock_guard<std::mutex> lock(regMutex_);
        registrationText_="Starting";
    }

    try{
        captures_=std::make_unique<CaptureManager>(logger_);
        endpoint_=std::make_unique<pj::Endpoint>();
        endpoint_->libCreate();

        pj::EpConfig ec;
        ec.uaConfig.maxCalls=maxCalls;
        ec.uaConfig.userAgent=SIPHER_USER_AGENT;
        ec.uaConfig.threadCnt=2;
        // Keep PJSIP's verbose internal trace out of stdout/stderr so the CLI
        // dashboard is never destroyed by asynchronous SIP/media log lines.
        // The full engine trace is still retained in S.I.P.H.E.R.'s private
        // per-user /tmp directory and can be viewed from the CLI Engine Log page.
        ec.logConfig.level=5;
        ec.logConfig.consoleLevel=0;
        ec.logConfig.msgLogging=1;
        ec.logConfig.filename=runtime::pjsipLogPath().string();
        if(!p.stunServer.empty()) ec.uaConfig.stunServer.push_back(p.stunServer);
        endpoint_->libInit(ec);

        sipMonitor_=std::make_unique<SipWireMonitor>(*this,logger_);
        sipMonitor_->start();

        pj::TransportConfig tc;
        tc.port=p.localSipPort;
        pjsip_transport_type_e transportType=PJSIP_TRANSPORT_UDP;
        if(p.transport==Transport::Tcp) transportType=PJSIP_TRANSPORT_TCP;
        else if(p.transport==Transport::Tls) transportType=PJSIP_TRANSPORT_TLS;
        const pj::TransportId transportId=endpoint_->transportCreate(transportType,tc);
        endpoint_->libStart();

        // Enumerate audio devices after PJSIP has started. Capture and playback
        // are intentionally selected independently: FreeBSD laptops commonly
        // expose an internal duplex codec plus a dedicated headset-mic capture
        // device. Leaving both on the system default can silently route calls
        // through the internal microphone even when the headset mic works.
        try {
            auto& audio=endpoint_->audDevManager();
            const auto devices=audio.enumDev2();
            logger_.info("PJSIP audio devices detected: "+std::to_string(devices.size()));
            for(std::size_t i=0;i<devices.size();++i){
                const auto& d=devices[i];
                logger_.info("Audio device ["+std::to_string(i)+"] driver=\""+d.driver+
                             "\" name=\""+d.name+"\" inputs="+std::to_string(d.inputCount)+
                             " outputs="+std::to_string(d.outputCount));
            }

            auto requestedDevice=[&](const char* envName,bool capture)->int {
                const char* raw=std::getenv(envName);
                if(raw==nullptr || *raw=='\0') return -1;
                char* end=nullptr;
                const long id=std::strtol(raw,&end,10);
                if(end==raw || *end!='\0' || id<0 || static_cast<std::size_t>(id)>=devices.size()){
                    logger_.warn(std::string(envName)+" must be a valid numeric PJSIP audio device ID; ignoring value "+raw);
                    return -1;
                }
                const auto& d=devices[static_cast<std::size_t>(id)];
                if(capture && d.inputCount<=0){
                    logger_.warn(std::string(envName)+" selects a device with no capture channels; ignoring it");
                    return -1;
                }
                if(!capture && d.outputCount<=0){
                    logger_.warn(std::string(envName)+" selects a device with no playback channels; ignoring it");
                    return -1;
                }
                return static_cast<int>(id);
            };

            int captureId=requestedDevice("SIPHER_CAPTURE_DEVICE",true);
            if(captureId<0) captureId=requestedDevice("SIPCLIENT_CAPTURE_DEVICE",true);
            if(captureId<0) captureId=requestedDevice("TRUNKMONKEY_CAPTURE_DEVICE",true);
            int playbackId=requestedDevice("SIPHER_PLAYBACK_DEVICE",false);
            if(playbackId<0) playbackId=requestedDevice("SIPCLIENT_PLAYBACK_DEVICE",false);
            if(playbackId<0) playbackId=requestedDevice("TRUNKMONKEY_PLAYBACK_DEVICE",false);

#ifdef __FreeBSD__
            // FreeBSD snd_hda/OSS commonly exposes a laptop as a duplex pcm0
            // plus a dedicated combo-jack/headset microphone. PortAudio device
            // metadata is not perfectly consistent across releases, so score
            // capture candidates instead of relying only on outputCount==0.
            if(captureId<0){
                int bestCapture=-1;
                int bestScore=-1;
                bool tie=false;
                for(std::size_t i=0;i<devices.size();++i){
                    const auto& d=devices[i];
                    if(d.inputCount<=0) continue;
                    std::string name=d.name;
                    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
                    int score=0;
                    if(d.outputCount==0) score+=40;
                    if(name.find("headset")!=std::string::npos) score+=80;
                    if(name.find("right analog mic")!=std::string::npos) score+=90;
                    if(name.find("microphone")!=std::string::npos) score+=55;
                    if(name.find(" mic")!=std::string::npos || name.rfind("mic",0)==0) score+=50;
                    if(name.find("internal")!=std::string::npos) score-=50;
                    if(score>bestScore){bestScore=score;bestCapture=static_cast<int>(i);tie=false;}
                    else if(score==bestScore && score>0){tie=true;}
                }
                if(bestCapture>=0 && bestScore>=40 && !tie){
                    captureId=bestCapture;
                    logger_.info("FreeBSD audio routing: selecting preferred microphone device ["+
                                 std::to_string(captureId)+"] "+devices[static_cast<std::size_t>(captureId)].name+
                                 " score="+std::to_string(bestScore));
                }else if(tie){
                    logger_.warn("FreeBSD audio routing: microphone candidates tied; keeping PJSIP default. "
                                 "Set SIPHER_CAPTURE_DEVICE to the desired numeric device ID.");
                }
            }
#endif

            if(captureId>=0) audio.setCaptureDev(captureId);
            if(playbackId>=0) audio.setPlaybackDev(playbackId);

            logger_.info("Active PJSIP capture device ID: "+std::to_string(audio.getCaptureDev()));
            logger_.info("Active PJSIP playback device ID: "+std::to_string(audio.getPlaybackDev()));
        } catch(const pj::Error& e){
            logger_.warn("Unable to enumerate/select PJSIP audio devices: "+e.info());
        }

        pj::AccountConfig ac;
        ac.idUri=quotedDisplayName(p.displayName)+" <sip:"+p.username+"@"+p.sipDomain+">";
        ac.regConfig.registrarUri=p.registrar;
        ac.regConfig.timeoutSec=p.registrationExpires;
        ac.sipConfig.transportId=transportId;
        const auto authUser=p.authUsername.empty()?p.username:p.authUsername;
        ac.sipConfig.authCreds.emplace_back("digest","*",authUser,0,p.password);
        if(!p.outboundProxy.empty()) ac.sipConfig.proxies.push_back(p.outboundProxy);
        ac.natConfig.iceEnabled=p.useIce;
        if(p.enableSrtp) ac.mediaConfig.srtpUse=PJMEDIA_SRTP_OPTIONAL;

        account_=std::make_unique<SipAccount>(*this,logger_);
        account_->create(ac,true);
        {
            std::lock_guard<std::mutex> lock(regMutex_);
            registrationText_="Registering";
        }
        stopping_=false;
        started_=true;
        logger_.info("S.I.P.H.E.R. SIP engine started: "+p.name+" transport="+toString(p.transport)+" pjsip_log="+runtime::pjsipLogPath().string());
    }catch(...){
        stop();
        throw;
    }
}

void SipEngine::stop()
{
    if(stopping_.exchange(true)) return;
    started_=false;
    registered_=false;

    // Barrier against an outgoing/incoming CallSession that passed its first
    // state check just before stopping_ became true. Once this lock has been
    // acquired and released, no call-creation path can still be in progress.
    {
        std::lock_guard<std::mutex> creationBarrier(callCreateMutex_);
    }

    if(captures_){
        try{ captures_->stopAll(); }catch(...){}
    }

    // Stop new wire-monitor callbacks before draining call/account state.
    if(sipMonitor_){
        try{ sipMonitor_->stop(); }catch(...){}
        sipMonitor_.reset();
    }

    if(endpoint_){
        try{ hangupAll(); }catch(...){}

        // PJSIP 2.17 Account::shutdown2(force=false) intentionally rejects
        // account deletion while calls are active. Give DISCONNECTED callbacks
        // a bounded window to complete before releasing Call wrappers.
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(1500);
        for(;;){
            bool active=false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for(const auto& item:calls_){
                    if(!item.second->snapshot().disconnected){ active=true; break; }
                }
            }
            if(!active || std::chrono::steady_clock::now()>=deadline) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }

    // pj::Call destructors touch PJSUA call user-data. Release every wrapper
    // while PJSUA-LIB and the Account still exist, never after libDestroy().
    std::vector<std::shared_ptr<CallSession>> drainingCalls;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        drainingCalls.reserve(calls_.size());
        for(auto& item:calls_) drainingCalls.push_back(std::move(item.second));
        calls_.clear();
        callIdIndex_.clear();
        pendingSip_.clear();
        foregroundId_=-1;
    }
    // Destroy wrappers without holding mutex_. pj::Call teardown can touch
    // PJSUA-LIB and must never be allowed to re-enter our call map lock.
    drainingCalls.clear();

    if(account_ && account_->isValid()){
        pj::AccountShutdownParam prm;
        prm.force=false;
        try{
            account_->shutdown2(prm);
        }catch(const pj::Error& e){
            logger_.warn("Graceful SIP account shutdown was busy/failed; forcing final account cleanup: "+e.info());
            try{
                prm.force=true;
                account_->shutdown2(prm);
            }catch(const pj::Error& forced){
                logger_.warn("Forced SIP account shutdown failed: "+forced.info());
            }
        }
    }
    account_.reset();

    if(endpoint_){
        try{ endpoint_->libDestroy(); }
        catch(const pj::Error& e){ logger_.warn("PJSUA2 endpoint shutdown: "+e.info()); }
        catch(...){ logger_.warn("PJSUA2 endpoint shutdown raised an unknown error"); }
    }
    endpoint_.reset();
    captures_.reset();

    {
        std::lock_guard<std::mutex> lock(regMutex_);
        registrationText_="Stopped";
    }
    stopping_=false;
}

bool SipEngine::started()const{return started_;}
bool SipEngine::registered()const{return registered_;}
std::string SipEngine::registrationText()const{std::lock_guard<std::mutex> lock(regMutex_);return registrationText_;}
const SipProfile& SipEngine::profile()const{return profile_;}

std::string SipEngine::normalizeDestination(const std::string& value)const
{
    const auto v=trim(value);
    if(v.empty()) throw std::runtime_error("Destination is empty");
    if(v.rfind("sip:",0)==0 || v.rfind("sips:",0)==0 || v.find('<')!=std::string::npos) return v;
    if(v.find('@')!=std::string::npos) return "sip:"+v;
    return "sip:"+v+"@"+profile_.sipDomain;
}

std::string SipEngine::callerIdentityUri(const std::string& value)const
{
    const auto v=trim(value);
    if(v.empty()) return {};
    if(v.rfind("sip:",0)==0 || v.rfind("sips:",0)==0 || v.find('<')!=std::string::npos) return v;
    if(v.find('@')!=std::string::npos) return "sip:"+v;
    const auto domain=profile_.callerIdDomain.empty()?profile_.sipDomain:profile_.callerIdDomain;
    return "sip:"+v+"@"+domain;
}

void SipEngine::configureIdentity(pj::CallOpParam& param,const std::string& callerId)const
{
    if(callerId.empty()) return;
    const auto uri=callerIdentityUri(callerId);
    if(profile_.identityMode==IdentityMode::From || profile_.identityMode==IdentityMode::FromAndPai){
        param.txOption.localUri=uri;
    }
    if(profile_.identityMode==IdentityMode::Pai || profile_.identityMode==IdentityMode::FromAndPai){
        pj::SipHeader header;
        header.hName="P-Asserted-Identity";
        header.hValue=nameAddr(uri);
        param.txOption.headers.push_back(header);
    }
    if(profile_.identityMode==IdentityMode::Rpid){
        pj::SipHeader header;
        header.hName="Remote-Party-ID";
        header.hValue=nameAddr(uri)+";party=calling;screen=yes;privacy=off";
        param.txOption.headers.push_back(header);
    }
}

int SipEngine::makeCall(const std::string& destination,const std::string& callerId,bool makeForeground,CallPurpose purpose)
{
    std::lock_guard<std::mutex> creationLock(callCreateMutex_);
    if(!started_ || stopping_ || !account_) throw std::runtime_error("No active SIP account");
    auto call=std::make_shared<CallSession>(*account_,logger_,CallDirection::Outgoing,purpose);
    call->setRequestedCallerId(callerId);
    call->setUpdateCallback([this](int id){ onCallUpdated(id); });
    pj::CallOpParam param(true);
    configureIdentity(param,callerId);
    const auto uri=normalizeDestination(destination);
    call->makeCall(uri,param);

    // A very fast failure can reach DISCONNECTED before makeCall() returns.
    // PJSUA2 documents the Call as invalid once that callback returns, so do
    // not call getId()/getInfo() on an already disconnected wrapper.
    const auto afterMake=call->snapshot();
    const int id=afterMake.id!=PJSUA_INVALID_ID ? afterMake.id : call->getId();
    const bool live=addCall(call);
    logger_.info("Outgoing call "+std::to_string(id)+" -> "+uri+(callerId.empty()?"":" CID="+callerId));
    if(makeForeground && live) setForeground(id);
    return id;
}

bool SipEngine::addCall(const std::shared_ptr<CallSession>& call)
{
    auto initial=call->snapshot();
    if(initial.disconnected){
        archiveDisconnectedCall(call,initial);
        return false;
    }

    std::string sipCallId;
    try{
        const auto info=call->getInfo();
        sipCallId=info.callIdString;
        call->refreshMediaInfo();
    }catch(...){
        // If the call disconnected during the getInfo/media refresh race,
        // preserve its final snapshot without invoking more live PJSUA2 APIs.
        initial=call->snapshot();
        if(initial.disconnected){
            archiveDisconnectedCall(call,initial);
            return false;
        }
    }

    std::vector<SipTraceEntry> pending;
    const auto current=call->snapshot();
    const int id=current.id!=PJSUA_INVALID_ID ? current.id : call->getId();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // PJSUA call slot numbers are reusable. Remove any stale SIP Call-ID
        // mapping that still points at this numeric slot before replacing it.
        for(auto it=callIdIndex_.begin();it!=callIdIndex_.end();){
            if(it->second==id) it=callIdIndex_.erase(it); else ++it;
        }
        // Numeric PJSUA call slots are reusable. A new live call supersedes
        // any archived diagnostics that used the same slot ID.
        archivedCalls_.erase(id);
        calls_[id]=call;
        if(!sipCallId.empty()){
            callIdIndex_[sipCallId]=id;
            const auto it=pendingSip_.find(sipCallId);
            if(it!=pendingSip_.end()){
                if(call->snapshot().purpose==CallPurpose::Phone) pending=std::move(it->second);
                pendingSip_.erase(it);
            }
        }
    }
    if(call->snapshot().purpose==CallPurpose::Phone){
        for(auto& entry:pending) call->recordSipMessage(std::move(entry));
    }
    return true;
}

std::shared_ptr<CallSession> SipEngine::findCall(int id)const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it=calls_.find(id);
    return it==calls_.end()?nullptr:it->second;
}

const SipEngine::ArchivedCall* SipEngine::findArchivedCallLocked(int id)const
{
    const auto it=archivedCalls_.find(id);
    return it==archivedCalls_.end()?nullptr:&it->second;
}

std::shared_ptr<CallSession> SipEngine::requirePhoneCall(int id)const
{
    auto call=findCall(id);
    if(!call) throw std::runtime_error("Call not found");
    if(call->snapshot().purpose!=CallPurpose::Phone)
        throw std::runtime_error("2.0 SIP/RTP diagnostic capture is limited to normal single Phone calls.");
    return call;
}

void SipEngine::answer(int id)
{
    auto call=findCall(id);
    if(!call) throw std::runtime_error("Call not found");
    if(call->snapshot().disconnected) throw std::runtime_error("Call is disconnected");
    setForeground(id);
    call->answerCall();
}

void SipEngine::reject(int id,int code)
{
    auto call=findCall(id);
    if(!call) throw std::runtime_error("Call not found");
    if(call->snapshot().disconnected) throw std::runtime_error("Call is disconnected");
    if(code<300 || code>699) throw std::runtime_error("Reject code must be 300-699");
    call->rejectCall(code);
}

void SipEngine::hangup(int id)
{
    auto call=findCall(id);
    if(!call) throw std::runtime_error("Call not found");
    if(call->snapshot().disconnected) throw std::runtime_error("Call is already disconnected");
    call->hangupCall();
}

void SipEngine::hold(int id)
{
    auto call=findCall(id);
    if(!call) throw std::runtime_error("Call not found");
    if(call->snapshot().disconnected) throw std::runtime_error("Call is disconnected");
    call->holdCall();
}

void SipEngine::resume(int id)
{
    auto call=findCall(id);
    if(!call) throw std::runtime_error("Call not found");
    if(call->snapshot().disconnected) throw std::runtime_error("Call is disconnected");
    call->resumeCall();
}

void SipEngine::sendDtmf(int id,const std::string& digits,unsigned durationMs)
{
    auto call=findCall(id);
    if(!call) throw std::runtime_error("Call not found");
    if(call->snapshot().disconnected) throw std::runtime_error("Call is disconnected");
    if(digits.empty()) throw std::runtime_error("DTMF digits required");
    call->sendDtmfDigits(digits,durationMs);
}

void SipEngine::setMicrophoneMuted(int id,bool muted)
{
    auto call=findCall(id);
    if(!call)throw std::runtime_error("Call not found");
    if(call->snapshot().disconnected)throw std::runtime_error("Call is disconnected");
    call->setMicrophoneMuted(muted);
}

std::vector<AudioDeviceInfo> SipEngine::audioDevices()const
{
    if(!endpoint_)return {};
    std::vector<AudioDeviceInfo> out;
    const auto devices=endpoint_->audDevManager().enumDev2();out.reserve(devices.size());
    for(std::size_t i=0;i<devices.size();++i){const auto&d=devices[i];out.push_back({static_cast<int>(i),d.driver,d.name,static_cast<unsigned>(d.inputCount),static_cast<unsigned>(d.outputCount)});}
    return out;
}

int SipEngine::activeCaptureDevice()const
{
    if(!endpoint_) return -1;
    return endpoint_->audDevManager().getCaptureDev();
}

int SipEngine::activePlaybackDevice()const
{
    if(!endpoint_) return -1;
    return endpoint_->audDevManager().getPlaybackDev();
}

void SipEngine::selectAudioDevices(int captureId,int playbackId)
{
    if(!endpoint_)throw std::runtime_error("SIP engine is not started");
    auto& audio=endpoint_->audDevManager();const auto devices=audio.enumDev2();
    if(captureId<0||static_cast<std::size_t>(captureId)>=devices.size()||devices[static_cast<std::size_t>(captureId)].inputCount<=0)throw std::runtime_error("Invalid capture device ID");
    if(playbackId<0||static_cast<std::size_t>(playbackId)>=devices.size()||devices[static_cast<std::size_t>(playbackId)].outputCount<=0)throw std::runtime_error("Invalid playback device ID");
    audio.setCaptureDev(captureId);audio.setPlaybackDev(playbackId);
    logger_.info("Audio devices selected: capture="+std::to_string(captureId)+" playback="+std::to_string(playbackId));
    std::shared_ptr<CallSession> foreground;{std::lock_guard<std::mutex> lock(mutex_);auto it=calls_.find(foregroundId_);if(it!=calls_.end())foreground=it->second;}
    if(foreground)foreground->attachAudio();
}

void SipEngine::setCallAudioFile(int id,const std::string& path)
{
    auto call=findCall(id);
    if(!call)throw std::runtime_error("Call not found");
    if(call->snapshot().purpose!=CallPurpose::QueueTest)throw std::runtime_error("Audio-file injection is limited to Queue Test calls");
    call->setQueueAudioFile(path);
}

void SipEngine::hangupAll()
{
    std::vector<std::shared_ptr<CallSession>> activeCalls;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for(const auto& item:calls_){
            if(!item.second->snapshot().disconnected) activeCalls.push_back(item.second);
        }
    }
    for(auto& call:activeCalls){
        try{ call->hangupCall(); }catch(...){}
    }
}

void SipEngine::setForeground(int id)
{
    std::shared_ptr<CallSession> oldCall,newCall;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(foregroundId_==id) return;
        const auto oldIt=calls_.find(foregroundId_);
        if(oldIt!=calls_.end()) oldCall=oldIt->second;
        const auto newIt=calls_.find(id);
        if(newIt==calls_.end()) throw std::runtime_error("Call not found");
        if(newIt->second->snapshot().disconnected) throw std::runtime_error("Cannot foreground a disconnected call");
        newCall=newIt->second;
        foregroundId_=id;
    }
    if(oldCall) oldCall->setForeground(false);
    if(newCall) newCall->setForeground(true);
}

void SipEngine::clearForeground()
{
    std::shared_ptr<CallSession> oldCall;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it=calls_.find(foregroundId_);
        if(it!=calls_.end()) oldCall=it->second;
        foregroundId_=-1;
    }
    if(oldCall) oldCall->setForeground(false);
}

std::vector<CallSnapshot> SipEngine::calls()const
{
    std::vector<std::shared_ptr<CallSession>> callObjects;
    std::vector<CallSnapshot> snapshots;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshots.reserve(archivedCalls_.size()+calls_.size());
        for(const auto& item:archivedCalls_) snapshots.push_back(item.second.snapshot);
        for(const auto& item:calls_) callObjects.push_back(item.second);
    }
    for(auto& call:callObjects){
        auto state=call->snapshot();
        if(!stopping_ && !state.disconnected){
            call->refreshMediaInfo();
            state=call->snapshot();
        }
        snapshots.push_back(std::move(state));
    }
    std::sort(snapshots.begin(),snapshots.end(),[](const CallSnapshot& a,const CallSnapshot& b){
        if(a.createdMs!=b.createdMs) return a.createdMs<b.createdMs;
        return a.id<b.id;
    });
    return snapshots;
}

CallSnapshot SipEngine::callSnapshot(int id)const
{
    auto call=findCall(id);
    if(call){
        auto state=call->snapshot();
        if(!stopping_ && !state.disconnected){
            call->refreshMediaInfo();
            state=call->snapshot();
        }
        return state;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if(const auto* archived=findArchivedCallLocked(id)) return archived->snapshot;
    throw std::runtime_error("Call not found");
}

std::string SipEngine::mediaDump(int id)const
{
    if(stopping_) return "SIP engine is shutting down; live PJSIP media dump is unavailable.";
    auto call=findCall(id);
    if(!call){
        CallSnapshot state;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto* archived=findArchivedCallLocked(id);
            if(!archived) throw std::runtime_error("Call not found");
            state=archived->snapshot;
        }
        std::ostringstream out;
        out << "Call " << id << " is disconnected; live PJSIP media dump is unavailable.\n"
            << "Last media: codec=" << (state.codecName.empty()?"unknown":state.codecName);
        if(state.codecClockRate) out << "/" << state.codecClockRate;
        out << " remote=" << (state.remoteRtpAddress.empty()?"unknown":state.remoteRtpAddress)
            << " source=" << (state.sourceRtpAddress.empty()?"unknown":state.sourceRtpAddress)
            << " local=" << (state.localRtpAddress.empty()?"unknown":state.localRtpAddress);
        return out.str();
    }
    const auto state=call->snapshot();
    if(state.disconnected) return "Call is disconnecting; live PJSIP media dump is unavailable.";
    call->refreshMediaInfo();
    return call->mediaDump();
}

std::string SipEngine::sipLadder(int id)const
{
    const auto trace=sipTrace(id);std::ostringstream out;
    out<<"S.I.P.H.E.R. SIP ladder — call "<<id<<"\n"
         <<"LOCAL                                      REMOTE\n"
         <<"  |                                           |\n";
    for(const auto&e:trace){
        std::ostringstream label;label<<e.label;if(e.statusCode)label<<" ["<<e.statusCode<<"]";
        auto text=label.str();if(text.size()>34)text=text.substr(0,31)+"...";
        if(e.direction==SipDirection::Sent)out<<"  |---- "<<std::left<<std::setw(34)<<text<<" -->|\n";
        else out<<"  |<--- "<<std::left<<std::setw(34)<<text<<" ---|\n";
    }
    out<<"  |                                           |\n";return out.str();
}

std::string SipEngine::callReport(int id)const
{
    const auto c=callSnapshot(id);std::ostringstream o;
    const double rxDen=static_cast<double>(c.rtpRxPackets+c.rtpRxLoss);const double lossPct=rxDen>0?100.0*c.rtpRxLoss/rxDen:0.0;
    o<<"S.I.P.H.E.R. 1.0.0 — SIP Inspection, Protocol Handling, Enumeration & Recon\nCALL DIAGNOSTIC REPORT\n\n"
     <<"Call ID:        "<<id<<"\nSIP Call-ID:    "<<c.callIdString<<"\nRemote URI:     "<<c.remoteUri<<"\nCaller ID:      "<<c.callerId<<"\nState:          "<<c.state<<"\nLast SIP:       "<<c.lastStatusCode<<" "<<c.lastReason<<"\n"
     <<"Codec:          "<<c.codecName<<(c.codecClockRate?"/"+std::to_string(c.codecClockRate):std::string{})<<"\n"
     <<"Microphone:     "<<(c.microphoneMuted?"MUTED":"live")<<"\n"
     <<"Audio devices:  capture="<<activeCaptureDevice()<<" playback="<<activePlaybackDevice()<<"\n"
     <<"Local RTP:      "<<c.localRtpAddress<<"\nRemote RTP:     "<<c.remoteRtpAddress<<"\nSource RTP:     "<<c.sourceRtpAddress<<"\n"
     <<"RTP TX:         "<<c.rtpTxPackets<<" packets / "<<c.rtpTxBytes<<" bytes / loss="<<c.rtpTxLoss<<" discard="<<c.rtpTxDiscard<<"\n"
     <<"RTP RX:         "<<c.rtpRxPackets<<" packets / "<<c.rtpRxBytes<<" bytes / loss="<<c.rtpRxLoss<<" discard="<<c.rtpRxDiscard<<"\n"
     <<"RX loss:        "<<std::fixed<<std::setprecision(2)<<lossPct<<"%\n"
     <<"Jitter TX/RX:   "<<std::setprecision(1)<<c.txJitterMs<<" / "<<c.rxJitterMs<<" ms\n"
     <<"RTT:            "<<c.rttMs<<" ms\nJitter buffer:  "<<c.jitterBufferDelayMs<<" ms\n"
     <<"Est. R-factor:  "<<c.estimatedRFactor<<"\nEst. MOS:       "<<c.estimatedMos<<" (engineering estimate; not PESQ/POLQA)\n\n";
    if(c.purpose==CallPurpose::Phone){try{o<<sipLadder(id)<<"\n";}catch(...){} }
    return o.str();
}

void SipEngine::exportCallReport(int id,const std::string& path)const
{
    const std::filesystem::path p(path);if(p.has_parent_path()){std::error_code ec;std::filesystem::create_directories(p.parent_path(),ec);if(ec&&!std::filesystem::is_directory(p.parent_path()))throw std::runtime_error("Unable to create report directory: "+ec.message());}
    std::ofstream out(path,std::ios::trunc);if(!out)throw std::runtime_error("Unable to create call report: "+path);out<<callReport(id);out.close();
#ifndef _WIN32
    (void)::chmod(path.c_str(),S_IRUSR|S_IWUSR);
#endif
}

std::vector<SipTraceEntry> SipEngine::sipTrace(int id)const
{
    if(auto call=findCall(id)){
        if(call->snapshot().purpose!=CallPurpose::Phone) throw std::runtime_error("SIP trace is limited to normal Phone calls");
        return call->sipTrace();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto* archived=findArchivedCallLocked(id);
    if(!archived) throw std::runtime_error("Call not found");
    if(archived->snapshot.purpose!=CallPurpose::Phone) throw std::runtime_error("SIP trace is limited to normal Phone calls");
    return archived->sipTrace;
}
void SipEngine::startSipTraceFile(int id,const std::string& path){requirePhoneCall(id)->startSipTraceFile(path);logger_.info("SIP trace file started: "+path);}
void SipEngine::stopSipTraceFile(int id){auto c=requirePhoneCall(id);auto p=c->sipTracePath();c->stopSipTraceFile();logger_.info("SIP trace file stopped"+(p.empty()?std::string{}:": "+p));}
bool SipEngine::sipTraceRecording(int id)const
{
    if(auto call=findCall(id)) return call->snapshot().purpose==CallPurpose::Phone && call->sipTraceRecording();
    std::lock_guard<std::mutex> lock(mutex_);
    if(findArchivedCallLocked(id)) return false;
    throw std::runtime_error("Call not found");
}
std::string SipEngine::sipTracePath(int id)const
{
    if(auto call=findCall(id)) return call->snapshot().purpose==CallPurpose::Phone ? call->sipTracePath() : std::string{};
    std::lock_guard<std::mutex> lock(mutex_);
    const auto* archived=findArchivedCallLocked(id);
    if(!archived) throw std::runtime_error("Call not found");
    return archived->sipTracePath;
}
void SipEngine::startSipPcap(int id,const std::string& path,const std::string& iface){requirePhoneCall(id);if(!captures_)throw std::runtime_error("Capture manager is not available");captures_->startSip(path,profile_.localSipPort,iface);}
void SipEngine::startRtpPcap(int id,const std::string& path,const std::string& iface)
{
    auto call=requirePhoneCall(id);
    auto state=call->snapshot();
    if(state.disconnected) throw std::runtime_error("Cannot start RTP capture for a disconnected call");
    call->refreshMediaInfo();
    state=call->snapshot();
    if(!captures_) throw std::runtime_error("Capture manager is not available");
    captures_->startRtp(path,state,iface);
}
void SipEngine::startCallPcap(int id,const std::string& path,const std::string& iface)
{
    auto call=requirePhoneCall(id);auto state=call->snapshot();if(state.disconnected)throw std::runtime_error("Cannot start call capture for a disconnected call");
    call->refreshMediaInfo();state=call->snapshot();if(!captures_)throw std::runtime_error("Capture manager is not available");captures_->startCall(path,profile_.localSipPort,state,iface);
}
void SipEngine::stopCapture(CaptureKind kind){if(captures_)captures_->stop(kind);}
void SipEngine::stopCaptures(){if(captures_)captures_->stopAll();}
std::string SipEngine::captureStatus()const{return captures_?captures_->status():"Packet capture unavailable";}

void SipEngine::onSipMessage(SipTraceEntry entry)
{
    std::shared_ptr<CallSession> call;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto mapped=callIdIndex_.find(entry.callIdString);
        if(mapped!=callIdIndex_.end()){
            const auto it=calls_.find(mapped->second);
            if(it!=calls_.end()) call=it->second;
        }
        if(!call){
            // The monitor can observe the initial INVITE before makeCall()/the
            // incoming-call callback has registered its CallSession. Buffer
            // only INVITE transactions for that short race window; buffering
            // arbitrary unmatched OPTIONS/NOTIFY/etc. would retain unrelated
            // raw SIP traffic and grow memory unnecessarily.
            if(entry.method=="INVITE" && !entry.callIdString.empty()){
                auto& queue=pendingSip_[entry.callIdString];
                if(queue.size()<32) queue.push_back(std::move(entry));
                if(pendingSip_.size()>64) pendingSip_.erase(pendingSip_.begin());
            }
            return;
        }
    }
    if(call->snapshot().purpose==CallPurpose::Phone) call->recordSipMessage(std::move(entry));
}

void SipEngine::onIncomingCall(int id)
{
    std::lock_guard<std::mutex> creationLock(callCreateMutex_);
    if(stopping_ || !started_ || !account_){
        // Do not create a new C++ Call wrapper while the engine is draining.
        // Account/endpoint shutdown will dispose of the unaccepted C-level call.
        logger_.warn("Ignored incoming call during SIP engine shutdown: id="+std::to_string(id));
        return;
    }
    auto call=std::make_shared<CallSession>(*account_,logger_,CallDirection::Incoming,CallPurpose::Phone,id);
    call->setUpdateCallback([this](int callId){ onCallUpdated(callId); });
    addCall(call);
    try{ logger_.info("Incoming call "+std::to_string(id)+" from "+call->getInfo().remoteUri); }catch(...){}
}

void SipEngine::onRegistrationState(bool active,int code,const std::string& reason)
{
    if(stopping_) return;
    registered_=active;
    std::ostringstream status;
    status<<(active?"Registered":"Not registered")<<" ("<<code<<" "<<reason<<")";
    const auto now=std::chrono::system_clock::now();const auto tt=std::chrono::system_clock::to_time_t(now);std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm,&tt);
#else
    localtime_r(&tt,&tm);
#endif
    std::ostringstream hist;hist<<std::put_time(&tm,"%Y-%m-%d %H:%M:%S")<<"  "<<status.str();
    {
        std::lock_guard<std::mutex> lock(regMutex_);
        registrationText_=status.str();registrationHistory_.push_back(hist.str());if(registrationHistory_.size()>100)registrationHistory_.erase(registrationHistory_.begin(),registrationHistory_.begin()+25);
    }
    logger_.info(status.str());
}

std::vector<std::string> SipEngine::registrationHistory()const
{
    std::lock_guard<std::mutex> lock(regMutex_);return registrationHistory_;
}

void SipEngine::archiveDisconnectedCall(const std::shared_ptr<CallSession>& call,const CallSnapshot& state)
{
    ArchivedCall archived;
    archived.snapshot=state;
    if(state.purpose==CallPurpose::Phone){
        archived.sipTrace=call->sipTrace();
        archived.sipTracePath=call->sipTracePath();
        if(call->sipTraceRecording()) call->stopSipTraceFile();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if(state.id!=PJSUA_INVALID_ID) archivedCalls_[state.id]=std::move(archived);
    for(auto it=callIdIndex_.begin();it!=callIdIndex_.end();){
        if(it->second==state.id) it=callIdIndex_.erase(it); else ++it;
    }
    calls_.erase(state.id);
    if(foregroundId_==state.id) foregroundId_=-1;
}

void SipEngine::onCallUpdated(int id)
{
    auto call=findCall(id);
    if(!call) return;
    auto state=call->snapshot();
    if(!state.disconnected){
        call->refreshMediaInfo();
        state=call->snapshot();
    }

    std::vector<SipTraceEntry> pending;
    if(!state.callIdString.empty()){
        std::lock_guard<std::mutex> lock(mutex_);
        // A Call-ID can become available after the CallSession was inserted.
        // Keep the wire-monitor lookup synchronized with the latest snapshot.
        for(auto it=callIdIndex_.begin();it!=callIdIndex_.end();){
            if(it->second==id && it->first!=state.callIdString) it=callIdIndex_.erase(it);
            else ++it;
        }
        callIdIndex_[state.callIdString]=id;
        const auto it=pendingSip_.find(state.callIdString);
        if(it!=pendingSip_.end()){
            if(state.purpose==CallPurpose::Phone) pending=std::move(it->second);
            pendingSip_.erase(it);
        }
    }
    if(state.purpose==CallPurpose::Phone){
        for(auto& entry:pending) call->recordSipMessage(std::move(entry));
    }

    if(state.disconnected){
        archiveDisconnectedCall(call,state);
        // PJSIP has already removed its Call user-data association before the
        // DISCONNECTED callback. Keep only S.I.P.H.E.R.'s immutable diagnostics;
        // the live wrapper is allowed to die without any further PJSUA2 calls.
    }
}
} // namespace trunkmonkey
