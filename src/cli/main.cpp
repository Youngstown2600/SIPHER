#include "CliDashboard.h"
#include "trunkmonkey/CaptureManager.h"
#include "trunkmonkey/Logger.h"
#include "trunkmonkey/MultiCallManager.h"
#include "trunkmonkey/PbxAudit.h"
#include "trunkmonkey/Profile.h"
#include "trunkmonkey/RuntimePaths.h"
#include "trunkmonkey/SipEngine.h"
#include "trunkmonkey/SipTrace.h"
#include "trunkmonkey/TextPool.h"
#include "trunkmonkey/Version.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <climits>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#include <poll.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#endif

using namespace trunkmonkey;
using trunkmonkey::cli::CliDashboard;
using trunkmonkey::cli::DashboardNotice;
using trunkmonkey::cli::DashboardState;
using trunkmonkey::cli::DashboardPage;

namespace {
constexpr unsigned kMaxCalls=50;

std::filesystem::path cliThemePath()
{
    return runtime::configDir()/"cli-theme.conf";
}

std::string loadCliTheme()
{
    std::ifstream in(cliThemePath());
    std::string value;
    if(std::getline(in,value) && !value.empty()) return value;
    return "classic";
}

void saveCliTheme(const std::string& value)
{
    std::ofstream out(cliThemePath(),std::ios::trunc);
    if(!out) throw std::runtime_error("Unable to save CLI theme: "+cliThemePath().string());
    out<<value<<'\n';
#ifndef _WIN32
    (void)::chmod(cliThemePath().c_str(),S_IRUSR|S_IWUSR);
#endif
}

std::vector<std::string> readEngineLogLines(std::size_t maxLines=8000)
{
    std::ifstream in(runtime::pjsipLogPath());
    std::vector<std::string> lines;
    std::string line;
    while(std::getline(in,line)){
        lines.push_back(line);
        if(lines.size()>maxLines+1000) lines.erase(lines.begin(),lines.begin()+1000);
    }
    return lines;
}

std::vector<std::string> loadList(const std::string& path)
{
    TextPool pool;
    pool.load(path);
    return pool.values();
}

std::string stamp(std::uint64_t ms)
{
    const auto point=std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
    const auto tt=std::chrono::system_clock::to_time_t(point);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm,&tt);
#else
    localtime_r(&tt,&tm);
#endif
    std::ostringstream out;
    out<<std::put_time(&tm,"%H:%M:%S")<<'.'<<std::setfill('0')<<std::setw(3)<<(ms%1000);
    return out.str();
}

int requireCallId(std::istringstream& in)
{
    long long value=-1;
    if(!(in>>value) || value<0 || value>INT_MAX) throw std::runtime_error("valid call id required");
    return static_cast<int>(value);
}

std::size_t requireCount(std::istringstream& in)
{
    unsigned long long value=0;
    if(!(in>>value) || value<1 || value>kMaxCalls) throw std::runtime_error("call count must be 1-50");
    return static_cast<std::size_t>(value);
}

unsigned requireInterval(std::istringstream& in)
{
    unsigned long long value=0;
    if(!(in>>value) || value>std::numeric_limits<unsigned>::max()) throw std::runtime_error("valid launch interval required");
    return static_cast<unsigned>(value);
}

bool yesNo(const std::string& prompt,bool current)
{
    for(;;){
        std::cout<<prompt<<" ["<<(current?"Y/n":"y/N")<<"]: ";
        std::string value;
        if(!std::getline(std::cin,value)) return current;
        if(value.empty()) return current;
        if(value=="y"||value=="Y"||value=="yes"||value=="YES") return true;
        if(value=="n"||value=="N"||value=="no"||value=="NO") return false;
    }
}

std::string promptValue(const std::string& label,const std::string& current)
{
    std::cout<<label;
    if(!current.empty()) std::cout<<" ["<<current<<"]";
    std::cout<<": ";
    std::string value;
    if(!std::getline(std::cin,value)) throw std::runtime_error("profile editing cancelled");
    if(value=="-") return {};
    return value.empty()?current:value;
}

std::string promptSecret(const std::string& label,const std::string& current)
{
    std::cout<<label<<(current.empty()?"":" [saved; Enter keeps current]")<<": "<<std::flush;
#ifndef _WIN32
    termios oldt{};
    bool changed=false;
    if(::isatty(STDIN_FILENO)&&::tcgetattr(STDIN_FILENO,&oldt)==0){
        termios t=oldt;
        t.c_lflag&=static_cast<tcflag_t>(~ECHO);
        changed=(::tcsetattr(STDIN_FILENO,TCSANOW,&t)==0);
    }
#endif
    std::string value;
    const bool ok=static_cast<bool>(std::getline(std::cin,value));
#ifndef _WIN32
    if(changed){::tcsetattr(STDIN_FILENO,TCSANOW,&oldt);std::cout<<'\n';}
#endif
    if(!ok) throw std::runtime_error("profile editing cancelled");
    if(value=="-") return {};
    return value.empty()?current:value;
}

unsigned promptUnsigned(const std::string& label,unsigned current,unsigned minValue,unsigned maxValue)
{
    for(;;){
        const auto value=promptValue(label,std::to_string(current));
        try{
            std::size_t used=0;
            const auto parsed=std::stoull(value,&used);
            if(used==value.size() && parsed>=minValue && parsed<=maxValue) return static_cast<unsigned>(parsed);
        }catch(...){}
        std::cerr<<"Enter a value from "<<minValue<<" to "<<maxValue<<".\n";
    }
}

bool editProfileInteractive(SipProfile& p,const std::filesystem::path& path)
{
    std::cout<<"\nS.I.P.H.E.R. SIP Profile Editor\nProfile: "<<path
             <<"\nPress Enter to keep the current value. Type - to clear a field. Ctrl+D cancels.\n\n";
    try{
        p.name=promptValue("Profile name",p.name);
        p.sipDomain=promptValue("SIP domain",p.sipDomain);
        p.registrar=promptValue("Registrar",p.registrar);
        p.username=promptValue("Username",p.username);
        p.authUsername=promptValue("Auth username",p.authUsername);
        p.password=promptSecret("Password",p.password);
        p.displayName=promptValue("Display name",p.displayName);
        p.outboundProxy=promptValue("Outbound proxy",p.outboundProxy);
        p.callerIdDomain=promptValue("Caller-ID domain",p.callerIdDomain);
        for(;;){
            try{p.transport=transportFromString(promptValue("Transport (udp/tcp/tls)",toString(p.transport)));break;}
            catch(const std::exception& error){std::cerr<<error.what()<<'\n';}
        }
        p.localSipPort=static_cast<std::uint16_t>(promptUnsigned("Local SIP port",p.localSipPort,1,65535));
        p.registrationExpires=promptUnsigned("Registration expires (seconds)",p.registrationExpires,1,std::numeric_limits<unsigned>::max());
        for(;;){
            try{p.identityMode=identityModeFromString(promptValue("Identity mode (from/pai/rpid/from+pai)",toString(p.identityMode)));break;}
            catch(const std::exception& error){std::cerr<<error.what()<<'\n';}
        }
        p.stunServer=promptValue("STUN server",p.stunServer);
        p.useIce=yesNo("Use ICE",p.useIce);
        p.enableSrtp=yesNo("Enable SRTP",p.enableSrtp);
        ProfileStore::validate(p);
        ProfileStore::save(p,path.string());
        std::cout<<"Profile saved: "<<path<<"\n";
        return true;
    }catch(const std::exception& error){
        std::cerr<<"Profile editor: "<<error.what()<<'\n';
        return false;
    }
}

std::string profileText(const SipProfile& p,const std::filesystem::path& path)
{
    std::ostringstream out;
    out<<"Profile: "<<path<<"\n"
       <<"  name="<<p.name<<"\n  sip_domain="<<p.sipDomain<<"\n  registrar="<<p.registrar
       <<"\n  username="<<p.username<<"\n  auth_username="<<p.authUsername
       <<"\n  password="<<(p.password.empty()?"<empty>":"<saved>")<<"\n  display_name="<<p.displayName
       <<"\n  outbound_proxy="<<p.outboundProxy<<"\n  caller_id_domain="<<p.callerIdDomain
       <<"\n  transport="<<toString(p.transport)<<"\n  local_sip_port="<<p.localSipPort
       <<"\n  registration_expires="<<p.registrationExpires<<"\n  identity_mode="<<toString(p.identityMode)
       <<"\n  stun_server="<<p.stunServer<<"\n  use_ice="<<(p.useIce?"true":"false")
       <<"\n  enable_srtp="<<(p.enableSrtp?"true":"false")<<'\n';
    return out.str();
}

bool activeCalls(const SipEngine& engine)
{
    for(const auto& call:engine.calls()) if(!call.disconnected) return true;
    return false;
}

std::string helpText()
{
    return R"(S.I.P.H.E.R. Operator Mode (recommended):
  1  Place a call
  2  Manage active calls
  3  Queue / call-blast test
  4  Call diagnostics & packet capture
  5  PBX / SIP security audit
  6  Audio devices & registration history
  7  SIP account / profile
  8  Themes & display
  9  Logs & capture utilities
 10  Advanced command reference
  0  Exit

Type 'menu' at any time to reopen the guided menu. A leading '/' is accepted on every command; `/commands` opens this reference.

Call slash commands:
 /dial <dest> [cid] | /answer <id> | /hangup [id] | /hangup-all
 /hold <id> | /resume <id> | /mute <id> | /unmute <id> | /dtmf <id> <digits> | /calls

Advanced Commands:
 status | calls | refresh
 profile-show | profile-edit | profile-reload
 dial <dest> [cid]
 answer <id> | reject <id> [code] | hangup <id> | hangup-all
 foreground <id> | foreground-none
 hold <id> | resume <id> | mute <id> | unmute <id> | dtmf <id> <digits>
 media <id> | stats <id> | ladder <id>
 audio-devices | audio-use <capture-id> <playback-id> | reg-history
 report <id> [file]            show or export a call diagnostic report
 siplog <id>                   select call and show live SIP signals in dashboard
 sipraw <id> <index>           full raw SIP message from siplog
 siptrace-start <id> <file>    write full SIP messages to a text trace
 siptrace-stop <id>
 sipcap-start <id> <file> [interface]
 rtpcap-start <id> <file> [interface]
 callcap-start <id> <file> [interface]   combined SIP+RTP/RTCP for an active call
 capture-stop [sip|rtp|call|all] | capture-status | capture-ifaces
 pcap-open <id> <file>            open in Wireshark with RTP/RTCP auto-decoded
 pjsiplog | log-up [n] | log-down [n] | log-tail   (PgUp/PgDn also scroll Engine Log)
 themes | theme <name>
 blast <count> <interval-ms> <dest> [cid]
 blast-audio <count> <interval-ms> <dest> <audio.(wav|mp3)> [cid]
 blast-file <count> <interval-ms> <destinations.txt> [callerids.txt]
 blast-file-audio <count> <interval-ms> <destinations.txt> <audio.(wav|mp3)> [callerids.txt]

PBX Audit (authorized systems only; active SIP probes):
 audit-warning
 audit-fingerprint <host> [port] [udp|tcp]
 audit-vulns <host> [port] [udp|tcp]     fingerprint + NVD/Exploit-DB metadata
 audit-probe <host> [port] [udp|tcp]
 audit-discover <ipv4-cidr> [port] [udp|tcp]   (/27-/32 only; rate-limited)
 audit-methods <host> [port] [udp|tcp]
 audit-auth <host> <user> [port] [udp|tcp]
 audit-oracle <host> <user> [port] [udp|tcp]  digest nonce/account-response audit
 audit-ext <host> <first> <last> [port] [udp|tcp]   (max 100, rate-limited)
 audit-compliance <host> [port] [udp|tcp]
 audit-parser <host> [port] [udp|tcp]       bounded parser-abuse simulation
 audit-resilience <host> [port] [udp|tcp]   capped low-volume rate test
 audit-scenario <host> [user] [port] [udp|tcp]  real-world-style attack chain
 audit-tls <host> [port]
 audit-full <host> [port] [udp|tcp]
 audit-save <file>             save the most recent PBX audit report
 cancel-launch | help | quit

Dashboard pages:
 - Alt+1 Main | Alt+2 SIP Log | Alt+3 Media/RTP | Alt+4 Calls | Alt+5 Security Audit | Alt+6 Profile | Alt+7 Help | Alt+8 Engine Log | Alt+9 Queue/Activity
 - 'media <id>' selects Media/RTP; 'siplog <id>' selects SIP Log; 'pjsiplog' opens the scrollable PJSIP engine log.
 - 'theme <name>' changes and saves the CLI palette; 'themes' lists available palettes.
 - Press Enter on a blank command to refresh the dashboard.
 - Wide terminals use a two-column layout; narrow terminals collapse cleanly.
 - ANSI colors are disabled automatically when output is redirected or TERM=dumb.

Detailed SIP/RTP capture commands remain available for normal single Phone calls.
Queue-test sessions remain independent calls and are not conferenced/audio-mixed.
)";
}

std::string callsText(const SipEngine& engine)
{
    std::ostringstream out;
    out<<"ID MODE  DIR FG STATE          SIP REMOTE / CID / RTP\n"
         "--------------------------------------------------------------------------\n";
    for(const auto& call:engine.calls()){
        out<<std::setw(2)<<call.id<<" "
           <<(call.purpose==CallPurpose::Phone?"PHONE":"QUEUE")<<" "
           <<(call.direction==CallDirection::Incoming?"IN ":"OUT")<<" "
           <<(call.foreground?"* ":"  ")
           <<std::setw(14)<<std::left<<call.state<<std::right
           <<std::setw(4)<<call.lastStatusCode<<" "<<call.remoteUri;
        if(!call.callerId.empty()) out<<" CID="<<call.callerId;
        if(!call.remoteRtpAddress.empty()) out<<" RTP="<<call.remoteRtpAddress;
        out<<'\n';
    }
    return out.str();
}

std::string mediaText(const SipEngine& engine,int id)
{
    const auto call=engine.callSnapshot(id);
    std::ostringstream out;
    out<<"Call "<<id<<" media\n"
       <<"  SIP Call-ID:       "<<(call.callIdString.empty()?"--":call.callIdString)<<"\n"
       <<"  RTP target:        "<<(call.remoteRtpAddress.empty()?"--":call.remoteRtpAddress)<<"\n"
       <<"  RTP source seen:   "<<(call.sourceRtpAddress.empty()?"--":call.sourceRtpAddress)<<"\n"
       <<"  Local RTP:         "<<(call.localRtpAddress.empty()?"--":call.localRtpAddress)<<"\n"
       <<"  Remote RTCP:       "<<(call.remoteRtcpAddress.empty()?"--":call.remoteRtcpAddress)<<"\n"
       <<"  Local RTCP:        "<<(call.localRtcpAddress.empty()?"--":call.localRtcpAddress)<<"\n"
       <<"  Codec:             "<<(call.codecName.empty()?"--":call.codecName);
    if(call.codecClockRate) out<<" / "<<call.codecClockRate<<" Hz";
    const double den=static_cast<double>(call.rtpRxPackets+call.rtpRxLoss);
    const double lossPct=den>0.0?100.0*call.rtpRxLoss/den:0.0;
    out<<'\n'
       <<"  RTP TX/RX packets: "<<call.rtpTxPackets<<" / "<<call.rtpRxPackets<<"\n"
       <<"  RTP TX/RX bytes:   "<<call.rtpTxBytes<<" / "<<call.rtpRxBytes<<"\n"
       <<"  RX loss:           "<<std::fixed<<std::setprecision(2)<<lossPct<<"% ("<<call.rtpRxLoss<<" packets)\n"
       <<"  Jitter TX/RX:      "<<std::setprecision(1)<<call.txJitterMs<<" / "<<call.rxJitterMs<<" ms\n"
       <<"  RTT / JBuf delay:  "<<call.rttMs<<" / "<<call.jitterBufferDelayMs<<" ms\n"
       <<"  Est. R / MOS:      "<<call.estimatedRFactor<<" / "<<call.estimatedMos<<" (engineering estimate)\n";
    return out.str();
}

std::string sipLogText(const SipEngine& engine,int id)
{
    const auto trace=engine.sipTrace(id);
    std::ostringstream out;
    out<<"IDX TIME         DIR CSEQ     SIGNAL                         CODE REASON\n"
         "--------------------------------------------------------------------------------\n";
    for(std::size_t i=0;i<trace.size();++i){
        const auto& entry=trace[i];
        out<<std::setw(3)<<i<<" "<<stamp(entry.timestampMs)<<" "
           <<(entry.direction==SipDirection::Sent?"TX ":"RX ")
           <<std::setw(8)<<entry.cseq<<" "
           <<std::setw(30)<<std::left<<entry.label<<std::right<<" "
           <<std::setw(4)<<entry.statusCode<<" "<<entry.reason<<'\n';
    }
    if(trace.empty()) out<<"(no SIP messages captured for this call yet)\n";
    return out.str();
}



std::string readArg(std::istringstream& in)
{
    in>>std::ws;
    std::string value;
    if(in.peek()=='"') in>>std::quoted(value);
    else in>>value;
    return value;
}

std::string commandArg(const std::string& value)
{
    std::ostringstream out;
    out<<std::quoted(value);
    return out.str();
}

std::string askOperator(const std::string& label,const std::string& current={})
{
    std::cout<<label;
    if(!current.empty()) std::cout<<" ["<<current<<"]";
    std::cout<<": "<<std::flush;
    std::string value;
    if(!std::getline(std::cin,value)) return {};
    return value.empty()?current:value;
}

int askOperatorChoice(const std::string& title,const std::vector<std::string>& choices)
{
    std::cout<<"\n"<<title<<"\n"<<std::string(title.size(),'=')<<"\n";
    for(std::size_t i=0;i<choices.size();++i) std::cout<<"  "<<(i+1)<<") "<<choices[i]<<"\n";
    std::cout<<"  0) Back / Cancel\n\n";
    for(;;){
        const auto value=askOperator("Select an option");
        if(value.empty()) return 0;
        try{
            std::size_t used=0;const int n=std::stoi(value,&used);
            if(used==value.size() && n>=0 && n<=static_cast<int>(choices.size())) return n;
        }catch(...){}
        std::cout<<"Please enter a number from 0 to "<<choices.size()<<".\n";
    }
}

std::string guidedOperatorWorkflow(CliDashboard& dashboard,const SipEngine& engine,int category)
{
    dashboard.clear(std::cout);
    if(category==0){
        category=askOperatorChoice("S.I.P.H.E.R. — Operator Menu",{
            "Place a call",
            "Manage active calls",
            "Run a queue / call-blast test",
            "Call diagnostics & packet capture",
            "PBX / SIP security audit",
            "Audio devices & registration history",
            "SIP account / profile",
            "Themes & display",
            "Logs & capture utilities",
            "Advanced command reference"
        });
    }
    if(category==0) return {};
    if(category==10) return "help";

    if(category==1){
        std::cout<<"\nPLACE A CALL\n------------\n";
        const auto destination=askOperator("Destination (number or SIP URI)");
        if(destination.empty()) return {};
        const auto cid=askOperator("Caller ID (optional)");
        return "dial "+commandArg(destination)+(cid.empty()?std::string{}:" "+commandArg(cid));
    }

    if(category==2){
        std::cout<<"\nACTIVE CALLS\n------------\n"<<callsText(engine)<<"\n";
        bool any=false;for(const auto& c:engine.calls())if(!c.disconnected){any=true;break;}
        if(!any){std::cout<<"There are no active calls.\n";askOperator("Press Enter to return");return {};}
        const auto id=askOperator("Call ID");if(id.empty())return{};
        const int action=askOperatorChoice("Call action",{
            "Make this the headset/foreground call","Answer","Hang up","Hold","Resume","Mute microphone","Unmute microphone","Send DTMF","View call report"
        });
        switch(action){
            case 1:return "foreground "+id;case 2:return "answer "+id;case 3:return "hangup "+id;case 4:return "hold "+id;
            case 5:return "resume "+id;case 6:return "mute "+id;case 7:return "unmute "+id;
            case 8:{auto d=askOperator("DTMF digits");return d.empty()?std::string{}:"dtmf "+id+" "+commandArg(d);} 
            case 9:return "report "+id;default:return{};
        }
    }

    if(category==3){
        const int action=askOperatorChoice("QUEUE / CALL-BLAST TEST",{
            "Single destination","Single destination + audio file","Destination list file","Destination list + audio file","Cancel an active launch"
        });
        if(action==0) return {};
        if(action==5) return "cancel-launch";
        const auto count=askOperator("Number of calls (1-50)","1");
        const auto interval=askOperator("Launch interval in milliseconds","250");
        if(action==1||action==2){
            const auto dest=askOperator("Destination");if(dest.empty())return{};
            const auto cid=askOperator("Caller ID (optional)");
            if(action==1)return "blast "+count+" "+interval+" "+commandArg(dest)+(cid.empty()?std::string{}:" "+commandArg(cid));
            const auto audio=askOperator("Audio file (WAV/MP3/etc.)");if(audio.empty())return{};
            return "blast-audio "+count+" "+interval+" "+commandArg(dest)+" "+commandArg(audio)+(cid.empty()?std::string{}:" "+commandArg(cid));
        }
        const auto list=askOperator("Destination list file");if(list.empty())return{};
        const auto cidList=askOperator("Caller-ID list file (optional)");
        if(action==3)return "blast-file "+count+" "+interval+" "+commandArg(list)+(cidList.empty()?std::string{}:" "+commandArg(cidList));
        const auto audio=askOperator("Audio file (WAV/MP3/etc.)");if(audio.empty())return{};
        return "blast-file-audio "+count+" "+interval+" "+commandArg(list)+" "+commandArg(audio)+(cidList.empty()?std::string{}:" "+commandArg(cidList));
    }

    if(category==4){
        std::cout<<"\nCALL DIAGNOSTICS\n----------------\n"<<callsText(engine)<<"\n";
        const auto id=askOperator("Call ID");if(id.empty())return{};
        const int action=askOperatorChoice("Diagnostic action",{
            "Media / RTP summary","Detailed PJSIP media statistics","SIP ladder","Live SIP message view","Combined SIP + RTP PCAP","SIP-only PCAP","RTP-only PCAP","Export diagnostic report"
        });
        if(action==0)return{};
        if(action==1) return "media "+id;
        if(action==2) return "stats "+id;
        if(action==3) return "ladder "+id;
        if(action==4) return "siplog "+id;
        if(action>=5&&action<=7){
            const std::string type=action==5?"callcap-start":action==6?"sipcap-start":"rtpcap-start";
            const std::string suffix=action==5?"call":action==6?"sip":"rtp";
            const auto path=askOperator("Capture file","/tmp/sipher-call-"+id+"-"+suffix+".pcapng");
            const auto iface=askOperator("Capture interface","any");
            return type+" "+id+" "+commandArg(path)+" "+commandArg(iface);
        }
        const auto path=askOperator("Report file (blank = view on screen)");
        return "report "+id+(path.empty()?std::string{}:" "+commandArg(path));
    }

    if(category==5){
        std::cout<<"\nPBX / SIP SECURITY AUDIT\n------------------------\n"
                 <<"Use only on systems you own or are explicitly authorized to test.\n"
                 <<"Active tests may trigger IDS/IPS, alarms, rate limits, or PBX protection controls.\n\n";
        const auto confirm=askOperator("Type YES to continue");if(confirm!="YES")return{};
        const int action=askOperatorChoice("Audit type",{
            "PBX / SIP fingerprint","Fingerprint + NIST CVE / Exploit-DB lookup","Full engineering audit","Single SIP service probe","Discover SIP hosts in a small CIDR","Extension differential audit","SIP method-policy audit","Authentication challenge audit","Digest/account-oracle audit","Compliance checks","Parser-abuse simulation (bounded)","Rate-resilience simulation (capped)","Attack-scenario simulation","TLS audit","Save last audit report"
        });
        if(action==0)return{};
        if(action==15){const auto path=askOperator("Report file","sipher-pbx-audit.txt");return"audit-save "+commandArg(path);}
        if(action==5){const auto cidr=askOperator("IPv4 CIDR (/27 through /32)");const auto port=askOperator("SIP port","5060");const auto tr=askOperator("Transport (udp/tcp)","udp");return"audit-discover "+cidr+" "+port+" "+tr;}
        const auto host=askOperator("PBX / SIP host");if(host.empty())return{};
        if(action==14){const auto port=askOperator("TLS port","5061");return"audit-tls "+host+" "+port;}
        const auto port=askOperator("SIP port","5060");const auto tr=askOperator("Transport (udp/tcp)","udp");
        if(action==1)return"audit-fingerprint "+host+" "+port+" "+tr;
        if(action==2)return"audit-vulns "+host+" "+port+" "+tr;
        if(action==3)return"audit-full "+host+" "+port+" "+tr;
        if(action==4)return"audit-probe "+host+" "+port+" "+tr;
        if(action==6){const auto first=askOperator("First extension");const auto last=askOperator("Last extension");return"audit-ext "+host+" "+first+" "+last+" "+port+" "+tr;}
        if(action==7)return"audit-methods "+host+" "+port+" "+tr;
        if(action==8){const auto user=askOperator("Test username / extension",engine.profile().username);return"audit-auth "+host+" "+user+" "+port+" "+tr;}
        if(action==9){const auto user=askOperator("Test username / extension",engine.profile().username);return"audit-oracle "+host+" "+user+" "+port+" "+tr;}
        if(action==10)return"audit-compliance "+host+" "+port+" "+tr;
        if(action==11)return"audit-parser "+host+" "+port+" "+tr;
        if(action==12)return"audit-resilience "+host+" "+port+" "+tr;
        if(action==13){const auto user=askOperator("Test username / extension",engine.profile().username);return"audit-scenario "+host+" "+user+" "+port+" "+tr;}
        return{};
    }

    if(category==6){
        const int action=askOperatorChoice("AUDIO & REGISTRATION",{"Show audio devices","Choose microphone and playback device","Registration history"});
        if(action==1) return "audio-devices";
        if(action==2){
            const auto c=askOperator("Capture / microphone device ID");
            const auto p=askOperator("Playback device ID");
            return(c.empty()||p.empty())?std::string{}:"audio-use "+c+" "+p;
        }
        if(action==3) return "reg-history";
        return {};
    }
    if(category==7){
        const int action=askOperatorChoice("SIP ACCOUNT / PROFILE",{"Show current profile","Edit profile","Reload profile from disk"});
        if(action==1) return "profile-show";
        if(action==2) return "profile-edit";
        if(action==3) return "profile-reload";
        return {};
    }
    if(category==8){
        std::cout<<"\nAVAILABLE THEMES\n----------------\n";const auto names=CliDashboard::themeNames();for(std::size_t i=0;i<names.size();++i)std::cout<<"  "<<(i+1)<<") "<<names[i]<<"\n";
        const auto value=askOperator("Theme number or name");if(value.empty())return{};
        try{std::size_t used=0;const auto n=std::stoul(value,&used);if(used==value.size()&&n>=1&&n<=names.size())return"theme "+names[n-1];}catch(...){}
        return"theme "+value;
    }
    if(category==9){
        const int action=askOperatorChoice("LOGS & CAPTURE UTILITIES",{"PJSIP engine log","Capture status","Available capture interfaces","Stop all packet captures","Open SIP log for a call"});
        if(action==1) return "pjsiplog";
        if(action==2) return "capture-status";
        if(action==3) return "capture-ifaces";
        if(action==4) return "capture-stop all";
        if(action==5){const auto id=askOperator("Call ID");return id.empty()?std::string{}:"siplog "+id;}
        return {};
    }
    return{};
}

#ifndef _WIN32
volatile sig_atomic_t gTerminalResized=0;
std::string gPendingInput;
void onTerminalResize(int){gTerminalResized=1;}
#endif

bool readInteractiveCommand(bool dashboardEnabled,std::string& line,int& altPage)
{
    altPage=0;
    if(!dashboardEnabled) return static_cast<bool>(std::getline(std::cin,line));
#ifdef _WIN32
    return static_cast<bool>(std::getline(std::cin,line));
#else
    termios oldt{};
    if(tcgetattr(STDIN_FILENO,&oldt)!=0) return static_cast<bool>(std::getline(std::cin,line));
    termios raw=oldt;
    raw.c_lflag&=static_cast<tcflag_t>(~(ICANON|ECHO));
    raw.c_cc[VMIN]=1;raw.c_cc[VTIME]=0;
    if(tcsetattr(STDIN_FILENO,TCSANOW,&raw)!=0) return static_cast<bool>(std::getline(std::cin,line));
    struct Restore{termios value;~Restore(){tcsetattr(STDIN_FILENO,TCSANOW,&value);}} restore{oldt};
    line=gPendingInput;if(!line.empty())std::cout<<line<<std::flush;
    for(;;){
        if(gTerminalResized){gTerminalResized=0;gPendingInput=line;line="__resize__";return true;}
        pollfd pfd{STDIN_FILENO,POLLIN,0};
        const int prc=::poll(&pfd,1,250);
        if(prc<0){if(errno==EINTR)continue;return false;}
        if(prc==0)continue;
        unsigned char ch=0;const auto n=::read(STDIN_FILENO,&ch,1);
        if(n<=0) return false;
        if(ch==0x1b){
            unsigned char next=0;if(::read(STDIN_FILENO,&next,1)<=0)return false;
            if(next>='1'&&next<='9'){gPendingInput.clear();altPage=next-'0';std::cout<<"\r\n"<<std::flush;return true;}
            if(next=='['){
                unsigned char key=0;if(::read(STDIN_FILENO,&key,1)<=0)return false;
                if(key=='5'||key=='6'){
                    unsigned char tilde=0;if(::read(STDIN_FILENO,&tilde,1)<=0)return false;
                    if(tilde=='~'){line=(key=='5')?"log-up 20":"log-down 20";std::cout<<"\r\n"<<std::flush;return true;}
                }
            }
            continue;
        }
        if(ch=='\r'||ch=='\n'){gPendingInput.clear();std::cout<<"\r\n"<<std::flush;return true;}
        if(ch==4&&line.empty()){gPendingInput.clear();return false;}
        if(ch==127||ch==8){if(!line.empty()){line.pop_back();std::cout<<"\b \b"<<std::flush;}continue;}
        if(ch>=32&&ch<127){line.push_back(static_cast<char>(ch));std::cout<<static_cast<char>(ch)<<std::flush;}
    }
#endif
}

void plainBanner()
{
    std::cout<<"\nS.I.P.H.E.R. By GITSC "<<TRUNKMONKEY_VERSION<<"\n"
             <<"SIP / RTP Troubleshooting & PBX Diagnostics\n"
             <<"Type 'menu' for guided workflows or 'help' for advanced commands.\n\n";
}
}

int main(int argc,char** argv)
{
    try{
        runtime::ensureUserDirectories();
    }catch(const std::exception& error){
        std::cerr<<error.what()<<'\n';
        return 2;
    }

    CliDashboard dashboard;
#ifndef _WIN32
    struct sigaction resizeAction{};resizeAction.sa_handler=onTerminalResize;sigemptyset(&resizeAction.sa_mask);resizeAction.sa_flags=0;(void)sigaction(SIGWINCH,&resizeAction,nullptr);
#endif
    (void)dashboard.setTheme(loadCliTheme());
    if(!dashboard.enabled()) plainBanner();

    const std::filesystem::path executable=argc>0?std::filesystem::path(argv[0]):std::filesystem::path{};
    const std::filesystem::path profilePath=argc>=2?std::filesystem::path(argv[1]):runtime::defaultProfilePath(executable);
    try{
        const bool created=ProfileStore::createDefaultIfMissing(profilePath.string());
        SipProfile draft=ProfileStore::loadDraft(profilePath.string());
        if(created || !ProfileStore::isConfigured(draft)){
            dashboard.clear(std::cout);
            std::cout<<(created?"Created":"Found unconfigured")<<" SIP profile: "<<profilePath<<"\n";
            std::cout<<"Configure it now; S.I.P.H.E.R. will not overwrite it on future builds.\n";
            if(!editProfileInteractive(draft,profilePath)) return 0;
        }
    }catch(const std::exception& error){
        std::cerr<<error.what()<<'\n';
        return 2;
    }

    Logger log(runtime::logPath().string());
    log.setConsoleEnabled(!dashboard.enabled());
    SipEngine engine(log);
    MultiCallManager multi(engine,log);
    try{
        engine.start(ProfileStore::load(profilePath.string()),kMaxCalls);
    }catch(const pj::Error& error){
        std::cerr<<error.info()<<'\n';
        return 1;
    }catch(const std::exception& error){
        std::cerr<<error.what()<<'\n';
        return 1;
    }

    std::vector<DashboardNotice> notices;
    int focusCallId=-1;
    std::string lastAuditReport;
    std::size_t engineLogOffset=0;
    DashboardPage currentPage=DashboardPage::Main;
    const auto addNotice=[&](std::string text,DashboardNotice::Level level=DashboardNotice::Level::Info){
        if(notices.size()>=32) notices.erase(notices.begin(),notices.begin()+8);
        notices.push_back({std::move(text),level});
        if(!dashboard.enabled()) std::cout<<notices.back().text<<'\n';
    };
    addNotice("S.I.P.H.E.R. ready. Select 1-9 for guided workflows, or type 'help' for advanced commands.",DashboardNotice::Level::Success);

    const auto makeDashboardState=[&](){
        DashboardState state;
        state.profile=engine.profile();
        state.profilePath=profilePath.string();
        state.page=currentPage;
        state.registrationText=engine.registrationText();
        state.registered=engine.registered();
        state.maxCalls=kMaxCalls;
        state.calls=engine.calls();
        state.focusCallId=focusCallId;
        state.captureStatus=engine.captureStatus();
        state.notices=notices;
        state.engineLogPath=runtime::pjsipLogPath().string();
        state.engineLogLines=readEngineLogLines();
        state.engineLogOffset=engineLogOffset;
        state.auditSummary=lastAuditReport;
        if(focusCallId>=0){
            try{state.focusTrace=engine.sipTrace(focusCallId);}catch(...){}
        }
        return state;
    };

    if(!dashboard.enabled()) std::cout<<"Type menu for guided workflows, or help for advanced commands.\n\n";

    std::string line;
    for(;;){
        if(dashboard.enabled()) dashboard.render(makeDashboardState(),std::cout);
        else std::cout<<"sipher> "<<std::flush;
        int altPage=0;
        if(!readInteractiveCommand(dashboard.enabled(),line,altPage)) break;
        if(line=="__resize__") continue;
        if(altPage>=1 && altPage<=9){currentPage=static_cast<DashboardPage>(altPage);continue;}

        std::istringstream in(line);
        std::string cmd;
        in>>cmd;
        try{
            if(cmd.empty()) continue;
            if(cmd.size()>1 && cmd.front()=='/') cmd.erase(cmd.begin());
            if(cmd=="call") cmd="dial";
            else if(cmd=="bye") cmd="hangup";
            else if(cmd=="hangupall") cmd="hangup-all";
            else if(cmd=="commands") cmd="help";
            if(cmd=="0" || cmd=="quit" || cmd=="exit") break;
            int guidedCategory=-1;
            if(cmd=="menu" || cmd=="m") guidedCategory=0;
            else if(cmd.size()<=2 && std::all_of(cmd.begin(),cmd.end(),[](unsigned char c){return std::isdigit(c);})) {
                try{guidedCategory=std::stoi(cmd);}catch(...){guidedCategory=-1;}
                if(guidedCategory<1 || guidedCategory>10) guidedCategory=-1;
            }
            if(guidedCategory>=0){
                line=guidedOperatorWorkflow(dashboard,engine,guidedCategory);
                if(line.empty()) continue;
                in.clear();in.str(line);cmd.clear();in>>cmd;
                if(cmd=="quit" || cmd=="exit") break;
            }
            if(cmd=="help"){
                dashboard.showOverlay("S.I.P.H.E.R. ADVANCED COMMAND REFERENCE",helpText(),std::cout);
                dashboard.pauseForEnter(std::cin,std::cout);
                continue;
            }
            if(cmd=="refresh") continue;
            if(cmd=="page"){
                int page=0;if(!(in>>page)||page<1||page>9)throw std::runtime_error("page expects 1-9");
                currentPage=static_cast<DashboardPage>(page);continue;
            }
            if(cmd=="themes"){
                std::ostringstream names;
                names<<"CLI themes:";
                for(const auto& name:CliDashboard::themeNames()) names<<" "<<name;
                dashboard.showOverlay("S.I.P.H.E.R. THEMES",names.str(),std::cout);
                dashboard.pauseForEnter(std::cin,std::cout);
                continue;
            }else if(cmd=="theme"){
                std::string name;in>>name;
                if(name.empty()) throw std::runtime_error("theme name required; use 'themes' to list choices");
                if(!dashboard.setTheme(name)) throw std::runtime_error("unknown theme: "+name+" (use 'themes')");
                saveCliTheme(dashboard.themeName());
                addNotice("CLI theme changed to "+dashboard.themeName()+".",DashboardNotice::Level::Success);
                continue;
            }else if(cmd=="pjsiplog" || cmd=="engine-log"){
                currentPage=DashboardPage::EngineLog;
                engineLogOffset=0;
                continue;
            }else if(cmd=="log-tail"){
                currentPage=DashboardPage::EngineLog;
                engineLogOffset=0;
                continue;
            }else if(cmd=="log-up"){
                unsigned long long count=20;in>>std::ws;if(!in.eof() && !(in>>count)) throw std::runtime_error("log-up expects a line count");
                const auto lines=readEngineLogLines();
                engineLogOffset=std::min<std::size_t>(lines.size(),engineLogOffset+static_cast<std::size_t>(count));
                currentPage=DashboardPage::EngineLog;
                continue;
            }else if(cmd=="log-down"){
                unsigned long long count=20;in>>std::ws;if(!in.eof() && !(in>>count)) throw std::runtime_error("log-down expects a line count");
                const auto n=static_cast<std::size_t>(count);
                engineLogOffset=n>=engineLogOffset?0:engineLogOffset-n;
                currentPage=DashboardPage::EngineLog;
                continue;
            }
            if(cmd=="status"){
                addNotice(engine.registrationText(),engine.registered()?DashboardNotice::Level::Success:DashboardNotice::Level::Warning);
            }else if(cmd=="calls"){
                if(dashboard.enabled()) addNotice("Active-call table refreshed.");
                else std::cout<<callsText(engine);
            }else if(cmd=="profile-show"){
                currentPage=DashboardPage::Profile;
                const auto text=profileText(ProfileStore::loadDraft(profilePath.string()),profilePath);
                dashboard.showOverlay("SIP PROFILE",text,std::cout);
                dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="profile-edit"){
                if(activeCalls(engine)) throw std::runtime_error("hang up active calls before editing the SIP profile");
                multi.cancelLaunching();
                SipProfile oldProfile=engine.profile();
                SipProfile edited=ProfileStore::loadDraft(profilePath.string());
                dashboard.clear(std::cout);
                if(editProfileInteractive(edited,profilePath)){
                    try{
                        engine.stop();
                        engine.start(ProfileStore::load(profilePath.string()),kMaxCalls);
                        addNotice("SIP profile saved and reloaded.",DashboardNotice::Level::Success);
                    }catch(...){
                        ProfileStore::save(oldProfile,profilePath.string());
                        try{engine.stop();engine.start(oldProfile,kMaxCalls);}catch(...){}
                        throw;
                    }
                }
            }else if(cmd=="profile-reload"){
                if(activeCalls(engine)) throw std::runtime_error("hang up active calls before reloading the SIP profile");
                multi.cancelLaunching();
                SipProfile oldProfile=engine.profile();
                try{
                    const auto updated=ProfileStore::load(profilePath.string());
                    engine.stop();
                    engine.start(updated,kMaxCalls);
                    addNotice("SIP profile reloaded.",DashboardNotice::Level::Success);
                }catch(...){
                    try{engine.stop();engine.start(oldProfile,kMaxCalls);}catch(...){}
                    throw;
                }
            }else if(cmd=="dial"){
                std::string destination,callerId;
                destination=readArg(in);callerId=readArg(in);
                if(destination.empty()) throw std::runtime_error("destination required");
                focusCallId=engine.makeCall(destination,callerId,true,CallPurpose::Phone);
                addNotice("Outgoing call started: ID "+std::to_string(focusCallId)+" -> "+destination,DashboardNotice::Level::Success);
            }else if(cmd=="answer"){
                const int id=requireCallId(in);engine.answer(id);focusCallId=id;
                addNotice("Answered call "+std::to_string(id)+".",DashboardNotice::Level::Success);
            }else if(cmd=="reject"){
                const int id=requireCallId(in);int code=603;
                in>>std::ws;if(!in.eof() && !(in>>code)) throw std::runtime_error("valid reject code required");
                engine.reject(id,code);focusCallId=id;
                addNotice("Rejected call "+std::to_string(id)+" with SIP "+std::to_string(code)+".",DashboardNotice::Level::Warning);
            }else if(cmd=="hangup"){
                int id=-1;long long requested=-1;
                if(in>>requested){if(requested<0||requested>INT_MAX)throw std::runtime_error("valid call id required");id=static_cast<int>(requested);}
                else{
                    const auto calls=engine.calls();
                    for(const auto& c:calls)if(!c.disconnected&&c.foreground){id=c.id;break;}
                    if(id<0&&focusCallId>=0)for(const auto& c:calls)if(!c.disconnected&&c.id==focusCallId){id=c.id;break;}
                    if(id<0){int only=-1;for(const auto& c:calls)if(!c.disconnected){if(only>=0){only=-2;break;}only=c.id;}if(only>=0)id=only;}
                    if(id<0)throw std::runtime_error("hangup needs a call id when more than one call is active; use /calls to list IDs");
                }
                engine.hangup(id);focusCallId=id;
                addNotice("Hangup requested for call "+std::to_string(id)+".");
            }else if(cmd=="hangup-all"){
                engine.hangupAll();addNotice("Hangup requested for all active calls.");
            }else if(cmd=="foreground"){
                const int id=requireCallId(in);engine.setForeground(id);focusCallId=id;
                addNotice("Call "+std::to_string(id)+" moved to headset foreground.");
            }else if(cmd=="foreground-none"){
                engine.clearForeground();addNotice("No call is routed to the local headset.");
            }else if(cmd=="hold"){
                const int id=requireCallId(in);engine.hold(id);focusCallId=id;
                addNotice("Hold requested for call "+std::to_string(id)+".");
            }else if(cmd=="resume"){
                const int id=requireCallId(in);engine.resume(id);focusCallId=id;
                addNotice("Resume/re-INVITE requested for call "+std::to_string(id)+".");
            }else if(cmd=="mute"||cmd=="unmute"){
                const int id=requireCallId(in);const bool muted=cmd=="mute";engine.setMicrophoneMuted(id,muted);focusCallId=id;
                addNotice(std::string("Microphone ")+(muted?"muted":"unmuted")+" on call "+std::to_string(id)+".",muted?DashboardNotice::Level::Warning:DashboardNotice::Level::Success);
            }else if(cmd=="dtmf"){
                const int id=requireCallId(in);std::string digits=readArg(in);
                if(digits.empty()) throw std::runtime_error("DTMF digits required");
                engine.sendDtmf(id,digits);focusCallId=id;
                addNotice("DTMF sent on call "+std::to_string(id)+": "+digits);
            }else if(cmd=="audio-devices"){
                std::ostringstream out;out<<"Active capture ID: "<<engine.activeCaptureDevice()<<"\nActive playback ID: "<<engine.activePlaybackDevice()<<"\n\n";
                for(const auto&d:engine.audioDevices())out<<"["<<d.id<<"] "<<d.driver<<" / "<<d.name<<"  inputs="<<d.inputCount<<" outputs="<<d.outputCount<<"\n";
                dashboard.showOverlay("AUDIO DEVICES",out.str(),std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="audio-use"){
                int capture=-1,playback=-1;if(!(in>>capture>>playback))throw std::runtime_error("audio-use requires capture-id and playback-id");engine.selectAudioDevices(capture,playback);
                addNotice("Audio devices selected: capture="+std::to_string(capture)+" playback="+std::to_string(playback),DashboardNotice::Level::Success);
            }else if(cmd=="reg-history"){
                std::ostringstream out;for(const auto&line:engine.registrationHistory())out<<line<<"\n";if(out.str().empty())out<<"No registration state changes recorded yet.\n";dashboard.showOverlay("REGISTRATION HISTORY",out.str(),std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="media"){
                const int id=requireCallId(in);(void)engine.callSnapshot(id);focusCallId=id;
                if(!dashboard.enabled()) std::cout<<mediaText(engine,id);
                else {currentPage=DashboardPage::Media;addNotice("Showing RTP/media diagnostics for call "+std::to_string(id)+".");}
            }else if(cmd=="stats"){
                const int id=requireCallId(in);focusCallId=id;
                dashboard.showOverlay("PJSIP MEDIA STATS — CALL "+std::to_string(id),engine.mediaDump(id),std::cout);
                dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="siplog"){
                const int id=requireCallId(in);(void)engine.sipTrace(id);focusCallId=id;
                if(!dashboard.enabled()) std::cout<<sipLogText(engine,id);
                else {currentPage=DashboardPage::SipLog;addNotice("Showing live SIP signals for call "+std::to_string(id)+".");}
            }else if(cmd=="sipraw"){
                const int id=requireCallId(in);std::size_t index=0;
                if(!(in>>index)) throw std::runtime_error("SIP log index required");
                const auto trace=engine.sipTrace(id);
                if(index>=trace.size()) throw std::runtime_error("SIP log index out of range");
                focusCallId=id;
                dashboard.showOverlay("RAW SIP — CALL "+std::to_string(id)+" ENTRY "+std::to_string(index),trace[index].rawMessage,std::cout);
                dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="siptrace-start"){
                const int id=requireCallId(in);std::string path=readArg(in);
                if(path.empty()) throw std::runtime_error("trace file path required");
                engine.startSipTraceFile(id,path);focusCallId=id;
                addNotice("Raw SIP trace started: "+path,DashboardNotice::Level::Success);
            }else if(cmd=="siptrace-stop"){
                const int id=requireCallId(in);engine.stopSipTraceFile(id);focusCallId=id;
                addNotice("Raw SIP trace stopped for call "+std::to_string(id)+".");
            }else if(cmd=="sipcap-start" || cmd=="rtpcap-start" || cmd=="callcap-start"){
                const int id=requireCallId(in);std::string path=readArg(in),iface="any";
                if(path.empty()) throw std::runtime_error("pcap file path required");
                in>>std::ws;if(!in.eof()) iface=readArg(in);
                if(cmd=="sipcap-start")engine.startSipPcap(id,path,iface);else if(cmd=="rtpcap-start")engine.startRtpPcap(id,path,iface);else engine.startCallPcap(id,path,iface);
                focusCallId=id;
                const std::string kind=cmd=="sipcap-start"?"SIP":(cmd=="rtpcap-start"?"RTP":"CALL");addNotice(kind+" PCAP started: "+path,DashboardNotice::Level::Success);
            }else if(cmd=="capture-stop"){
                std::string which="all";in>>which;
                if(which=="sip") engine.stopCapture(CaptureKind::Sip);
                else if(which=="rtp") engine.stopCapture(CaptureKind::Rtp);
                else if(which=="call") engine.stopCapture(CaptureKind::Call);
                else if(which=="all") engine.stopCaptures();
                else throw std::runtime_error("capture-stop expects sip, rtp, call, or all");
                addNotice("Packet capture stopped: "+which+".");
            }else if(cmd=="capture-ifaces"){
                std::ostringstream list;list<<"Capture tool: "<<CaptureManager::captureTool()<<"\nWireshark: "<<CaptureManager::wiresharkTool()<<"\nInterfaces:";
                for(const auto& name:CaptureManager::availableInterfaces())list<<" "<<name;
                list<<"\n"<<CaptureManager::permissionHint()<<"\n";
                dashboard.showOverlay("CAPTURE INTERFACES",list.str(),std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="pcap-open"){
                const int id=requireCallId(in);std::string path=readArg(in);if(path.empty())throw std::runtime_error("pcap file path required");focusCallId=id;engine.openPcapInWireshark(id,path);addNotice("Opened PCAP in Wireshark with automatic RTP/RTCP decoding: "+path,DashboardNotice::Level::Success);
            }else if(cmd=="capture-status"){
                if(dashboard.enabled()) addNotice(engine.captureStatus());
                else std::cout<<engine.captureStatus()<<'\n';
            }else if(cmd=="ladder"){
                const int id=requireCallId(in);focusCallId=id;
                dashboard.showOverlay("SIP LADDER — CALL "+std::to_string(id),engine.sipLadder(id),std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="report"){
                const int id=requireCallId(in);std::string path=readArg(in);focusCallId=id;
                if(path.empty()){dashboard.showOverlay("CALL DIAGNOSTIC REPORT — "+std::to_string(id),engine.callReport(id),std::cout);dashboard.pauseForEnter(std::cin,std::cout);}
                else{engine.exportCallReport(id,path);addNotice("Call report exported: "+path,DashboardNotice::Level::Success);}
            }else if(cmd=="audit-warning"){
                dashboard.showOverlay("PBX AUDIT WARNING",PbxAudit::warningText(),std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="audit-fingerprint"){
                std::string host,transport="udp";unsigned port=5060;in>>host;if(host.empty())throw std::runtime_error("audit-fingerprint requires a host");in>>std::ws;if(!in.eof())in>>port>>transport;
                auto fp=PbxAudit::fingerprint(host,(std::uint16_t)port,PbxAudit::transportFromString(transport));lastAuditReport=fp.toText();currentPage=DashboardPage::SecurityAudit;dashboard.showOverlay("PBX FINGERPRINT",lastAuditReport,std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="audit-vulns"){
                std::string host,transport="udp";unsigned port=5060;in>>host;if(host.empty())throw std::runtime_error("audit-vulns requires a host");in>>std::ws;if(!in.eof())in>>port>>transport;
                auto fp=PbxAudit::fingerprint(host,(std::uint16_t)port,PbxAudit::transportFromString(transport));lastAuditReport=fp.toText()+"\n"+PbxAudit::vulnerabilityLookupReport(fp);currentPage=DashboardPage::SecurityAudit;dashboard.showOverlay("PBX FINGERPRINT + VULNERABILITY CORRELATION",lastAuditReport,std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="audit-probe"){
                std::string host,transport="udp";unsigned port=5060;in>>host;if(host.empty())throw std::runtime_error("audit-probe requires a host");in>>std::ws;if(!in.eof())in>>port>>transport;
                auto r=PbxAudit::serviceProbe(host,static_cast<std::uint16_t>(port),PbxAudit::transportFromString(transport));lastAuditReport=PbxAudit::report("PBX SERVICE PROBE",{r});dashboard.showOverlay("PBX AUDIT — SERVICE PROBE",lastAuditReport,std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="audit-discover"){
                std::string cidr,transport="udp";unsigned port=5060;in>>cidr;if(cidr.empty())throw std::runtime_error("audit-discover requires an IPv4 CIDR");in>>std::ws;if(!in.eof())in>>port>>transport;
                auto entries=PbxAudit::discoverIpv4Cidr(cidr,static_cast<std::uint16_t>(port),PbxAudit::transportFromString(transport));lastAuditReport=PbxAudit::report("BOUNDED SIP DISCOVERY",{}, {}, {},entries);dashboard.showOverlay("PBX AUDIT — DISCOVERY",lastAuditReport,std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="audit-methods"){
                std::string host,transport="udp";unsigned port=5060;in>>host;if(host.empty())throw std::runtime_error("audit-methods requires a host");in>>std::ws;if(!in.eof())in>>port>>transport;
                auto rs=PbxAudit::methodAudit(host,static_cast<std::uint16_t>(port),PbxAudit::transportFromString(transport));lastAuditReport=PbxAudit::report("PBX METHOD POLICY AUDIT",rs);dashboard.showOverlay("PBX AUDIT — METHODS",lastAuditReport,std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="audit-auth"){
                std::string host,user,transport="udp";unsigned port=5060;in>>host>>user;if(host.empty()||user.empty())throw std::runtime_error("audit-auth requires host and user");in>>std::ws;if(!in.eof())in>>port>>transport;
                auto r=PbxAudit::authenticationAudit(host,user,static_cast<std::uint16_t>(port),PbxAudit::transportFromString(transport));lastAuditReport=PbxAudit::report("PBX AUTHENTICATION AUDIT",{r});dashboard.showOverlay("PBX AUDIT — AUTH",lastAuditReport,std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="audit-oracle"){
                std::string host,user,transport="udp";unsigned port=5060;in>>host>>user;if(host.empty()||user.empty())throw std::runtime_error("audit-oracle requires host and test user");in>>std::ws;if(!in.eof())in>>port>>transport;
                auto rs=PbxAudit::digestOracleAudit(host,user,(std::uint16_t)port,PbxAudit::transportFromString(transport));lastAuditReport=PbxAudit::report("PBX DIGEST NONCE / ACCOUNT-ORACLE AUDIT",rs);dashboard.showOverlay("PBX AUDIT",lastAuditReport,std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="audit-ext"){
                std::string host,transport="udp";unsigned first=0,last=0,port=5060;in>>host>>first>>last;if(host.empty())throw std::runtime_error("audit-ext requires host first last");in>>std::ws;if(!in.eof())in>>port>>transport;
                auto entries=PbxAudit::extensionAudit(host,first,last,static_cast<std::uint16_t>(port),PbxAudit::transportFromString(transport));lastAuditReport=PbxAudit::report("PBX EXTENSION DIFFERENTIAL AUDIT",{},entries);dashboard.showOverlay("PBX AUDIT — EXTENSIONS",lastAuditReport,std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="audit-compliance"){
                std::string host,transport="udp";unsigned port=5060;in>>host;if(host.empty())throw std::runtime_error("audit-compliance requires a host");in>>std::ws;if(!in.eof())in>>port>>transport;
                auto rs=PbxAudit::complianceAudit(host,static_cast<std::uint16_t>(port),PbxAudit::transportFromString(transport));lastAuditReport=PbxAudit::report("PBX BOUNDED COMPLIANCE AUDIT",rs);dashboard.showOverlay("PBX AUDIT — COMPLIANCE",lastAuditReport,std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="audit-parser"){
                std::string host,transport="udp";unsigned port=5060;in>>host;if(host.empty())throw std::runtime_error("audit-parser requires a host");in>>std::ws;if(!in.eof())in>>port>>transport;
                auto rs=PbxAudit::parserAbuseAudit(host,static_cast<std::uint16_t>(port),PbxAudit::transportFromString(transport));lastAuditReport=PbxAudit::report("PBX PARSER-ABUSE SIMULATION",rs);dashboard.showOverlay("PBX AUDIT — PARSER",lastAuditReport,std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="audit-resilience"){
                std::string host,transport="udp";unsigned port=5060;in>>host;if(host.empty())throw std::runtime_error("audit-resilience requires a host");in>>std::ws;if(!in.eof())in>>port>>transport;
                auto rs=PbxAudit::resilienceAudit(host,static_cast<std::uint16_t>(port),PbxAudit::transportFromString(transport));lastAuditReport=PbxAudit::report("PBX BOUNDED RATE-RESILIENCE SIMULATION",rs);dashboard.showOverlay("PBX AUDIT — RESILIENCE",lastAuditReport,std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="audit-scenario"){
                std::string host,user,transport="udp";unsigned port=5060;in>>host;if(host.empty())throw std::runtime_error("audit-scenario requires a host");in>>user; if(user.empty())user=engine.profile().username; in>>std::ws;if(!in.eof())in>>port>>transport;
                auto rs=PbxAudit::attackScenarioAudit(host,user,static_cast<std::uint16_t>(port),PbxAudit::transportFromString(transport));std::string tls;try{tls=PbxAudit::tlsAudit(host,5061,3500);}catch(const std::exception&e){tls=std::string("TLS probe unavailable: ")+e.what();}lastAuditReport=PbxAudit::report("REAL-WORLD PBX ATTACK-SCENARIO SIMULATION",rs,{},tls);dashboard.showOverlay("PBX AUDIT — ATTACK SCENARIO",lastAuditReport,std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="audit-tls"){
                std::string host;unsigned port=5061;in>>host;if(host.empty())throw std::runtime_error("audit-tls requires a host");in>>std::ws;if(!in.eof())in>>port;
                const auto tls=PbxAudit::tlsAudit(host,static_cast<std::uint16_t>(port));lastAuditReport=PbxAudit::report("PBX TLS AUDIT",{}, {},tls);dashboard.showOverlay("PBX AUDIT — TLS",lastAuditReport,std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="audit-full"){
                std::string host,transport="udp";unsigned port=5060;in>>host;if(host.empty())throw std::runtime_error("audit-full requires a host");in>>std::ws;if(!in.eof())in>>port>>transport;const auto t=PbxAudit::transportFromString(transport);
                auto rs=PbxAudit::attackScenarioAudit(host,engine.profile().username,static_cast<std::uint16_t>(port),t);const auto alt=t==AuditTransport::Udp?AuditTransport::Tcp:AuditTransport::Udp;rs.push_back(PbxAudit::serviceProbe(host,static_cast<std::uint16_t>(port),alt));auto more=PbxAudit::complianceAudit(host,static_cast<std::uint16_t>(port),t);rs.insert(rs.end(),more.begin(),more.end());std::string tls;try{tls=PbxAudit::tlsAudit(host,5061,3500);}catch(const std::exception&e){tls=std::string("TLS probe unavailable: ")+e.what();}lastAuditReport=PbxAudit::report("FULL PBX ENGINEERING AUDIT",rs,{},tls);dashboard.showOverlay("PBX AUDIT — FULL",lastAuditReport,std::cout);dashboard.pauseForEnter(std::cin,std::cout);
            }else if(cmd=="audit-save"){
                std::string path=readArg(in);if(path.empty())throw std::runtime_error("audit-save requires a file path");if(lastAuditReport.empty())throw std::runtime_error("no PBX audit has been run yet");PbxAudit::saveReport(path,lastAuditReport);addNotice("PBX audit report saved: "+path,DashboardNotice::Level::Success);
            }else if(cmd=="blast"){
                MultiCallPlan plan;plan.callCount=requireCount(in);plan.launchIntervalMs=requireInterval(in);
                plan.singleDestination=readArg(in);plan.fixedCallerId=readArg(in);
                if(plan.singleDestination.empty()) throw std::runtime_error("destination required");
                multi.start(plan);
                addNotice("Queue test launching "+std::to_string(plan.callCount)+" independent calls -> "+plan.singleDestination,DashboardNotice::Level::Success);
            }else if(cmd=="blast-audio"){
                MultiCallPlan plan;plan.callCount=requireCount(in);plan.launchIntervalMs=requireInterval(in);plan.singleDestination=readArg(in);plan.audioFile=readArg(in);plan.fixedCallerId=readArg(in);if(plan.singleDestination.empty()||plan.audioFile.empty())throw std::runtime_error("destination and audio file required");multi.start(plan);addNotice("Queue audio test launching "+std::to_string(plan.callCount)+" calls with media file "+plan.audioFile,DashboardNotice::Level::Success);
            }else if(cmd=="blast-file-audio"){
                MultiCallPlan plan;plan.callCount=requireCount(in);plan.launchIntervalMs=requireInterval(in);std::string destinationFile=readArg(in),callerIdFile;plan.audioFile=readArg(in);callerIdFile=readArg(in);if(destinationFile.empty()||plan.audioFile.empty())throw std::runtime_error("destination file and audio file required");plan.destinations=loadList(destinationFile);if(!callerIdFile.empty())plan.callerIds=loadList(callerIdFile);multi.start(plan);addNotice("Queue audio test launching "+std::to_string(plan.callCount)+" calls from destination pool.",DashboardNotice::Level::Success);
            }else if(cmd=="blast-file"){
                MultiCallPlan plan;plan.callCount=requireCount(in);plan.launchIntervalMs=requireInterval(in);
                std::string destinationFile=readArg(in),callerIdFile=readArg(in);
                if(destinationFile.empty()) throw std::runtime_error("destination file required");
                plan.destinations=loadList(destinationFile);
                if(!callerIdFile.empty()) plan.callerIds=loadList(callerIdFile);
                multi.start(plan);
                addNotice("Queue test launching "+std::to_string(plan.callCount)+" independent calls from destination pool.",DashboardNotice::Level::Success);
            }else if(cmd=="cancel-launch"){
                multi.cancelLaunching();addNotice("Queue-test call launching cancelled.",DashboardNotice::Level::Warning);
            }else{
                addNotice("Unknown command: "+cmd+". Type 'help'.",DashboardNotice::Level::Warning);
            }
        }catch(const pj::Error& error){
            addNotice("PJSIP: "+error.info(),DashboardNotice::Level::Error);
        }catch(const std::exception& error){
            addNotice("Error: "+std::string(error.what()),DashboardNotice::Level::Error);
        }
    }

    multi.cancelLaunching();
    engine.stop();
    dashboard.clear(std::cout);
    return 0;
}
