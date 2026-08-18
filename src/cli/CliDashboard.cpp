#include "CliDashboard.h"
#include "trunkmonkey/Version.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>
#ifndef _WIN32
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace trunkmonkey::cli {
namespace {
constexpr const char* RESET="\033[0m";
constexpr const char* DIM="\033[2m";
constexpr const char* BRIGHT_GREEN="\033[92m";
constexpr const char* YELLOW="\033[33m";
constexpr const char* BRIGHT_YELLOW="\033[93m";
constexpr const char* BRIGHT_CYAN="\033[96m";
constexpr const char* BRIGHT_RED="\033[91m";
constexpr const char* WHITE="\033[97m";

std::uint64_t nowMs()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string upper(std::string value)
{
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

std::string labelValue(const std::string& label,const std::string& value,std::size_t labelWidth=16)
{
    std::ostringstream out;
    out<<std::left<<std::setw(static_cast<int>(labelWidth))<<label<<": "<<value;
    return out.str();
}

std::string stateWord(bool registered,const std::string& text)
{
    if(registered) return "REGISTERED";
    if(text.find("Registering")!=std::string::npos || text.find("Starting")!=std::string::npos) return "REGISTERING";
    return "NOT REGISTERED";
}

std::string remoteParty(const std::string& uri)
{
    const auto sip=uri.find("sip:");
    const auto sips=uri.find("sips:");
    std::size_t start=std::string::npos;
    if(sip!=std::string::npos) start=sip+4;
    else if(sips!=std::string::npos) start=sips+5;
    if(start==std::string::npos) return uri;
    const auto at=uri.find('@',start);
    if(at!=std::string::npos && at>start) return uri.substr(start,at-start);
    auto end=uri.find_first_of(">; \t",start);
    if(end==std::string::npos) end=uri.size();
    return uri.substr(start,end-start);
}
}

CliDashboard::CliDashboard()
{
#ifndef _WIN32
    enabled_=::isatty(STDOUT_FILENO) && ::isatty(STDIN_FILENO);
#else
    enabled_=true;
#endif
    const char* term=std::getenv("TERM");
    if(term && std::string_view(term)=="dumb") enabled_=false;
    if(const char* force=std::getenv("SIPHER_FORCE_DASHBOARD")) {
        if(std::string_view(force)=="1") enabled_=true;
    } else if(const char* previewForce=std::getenv("SIPCLIENT_FORCE_DASHBOARD")) {
        if(std::string_view(previewForce)=="1") enabled_=true;
    } else if(const char* legacyForce=std::getenv("TRUNKMONKEY_FORCE_DASHBOARD")) {
        if(std::string_view(legacyForce)=="1") enabled_=true;
    }
    if(const char* disable=std::getenv("SIPHER_NO_DASHBOARD")) {
        if(std::string_view(disable)=="1") enabled_=false;
    } else if(const char* previewDisable=std::getenv("SIPCLIENT_NO_DASHBOARD")) {
        if(std::string_view(previewDisable)=="1") enabled_=false;
    } else if(const char* legacyDisable=std::getenv("TRUNKMONKEY_NO_DASHBOARD")) {
        if(std::string_view(legacyDisable)=="1") enabled_=false;
    }
    color_=enabled_ && std::getenv("NO_COLOR")==nullptr;
    if(term){
        const std::string_view t(term);
        consoleTty_=(t=="linux" || t=="cons25" || t=="vt100" || t=="vt220");
    }
    setTheme("classic");
    if(const char* envTheme=std::getenv("SIPHER_THEME")) setTheme(envTheme);
    else if(const char* previewTheme=std::getenv("SIPCLIENT_THEME")) setTheme(previewTheme);
    else if(const char* envTheme=std::getenv("TRUNKMONKEY_THEME")) setTheme(envTheme);
}

std::vector<std::string> CliDashboard::themeNames()
{
    return {"classic","hacker","matrix","phosphor","midnight","amber","ice","classic-light",
            "solarized","dracula","nord","cyberpunk","blood-moon","ocean","retro-blue","monochrome",
            "blue-box","red-box","beige-box","2600","wargames","crt-green","vt220","cobalt","vaporwave","stealth"};
}

bool CliDashboard::setTheme(const std::string& requested)
{
    std::string name=requested;
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    std::replace(name.begin(),name.end(),'_','-');
    if(name=="system") name="classic";
    const auto known=themeNames();
    if(std::find(known.begin(),known.end(),name)==known.end()) return false;
    themeName_=name;

    // Semantic ANSI palette. 256-color entries degrade gracefully on terminals
    // that only support the classic ANSI subset; NO_COLOR still disables all paint.
    if(name=="hacker") palette_={"\033[38;5;46m","\033[38;5;82m","\033[38;5;118m","\033[38;5;226m","\033[38;5;196m","\033[38;5;231m","\033[38;5;240m"};
    else if(name=="matrix") palette_={"\033[38;5;40m","\033[38;5;48m","\033[38;5;46m","\033[38;5;154m","\033[38;5;196m","\033[38;5;120m","\033[38;5;22m"};
    else if(name=="phosphor") palette_={"\033[38;5;120m","\033[38;5;114m","\033[38;5;156m","\033[38;5;221m","\033[38;5;203m","\033[38;5;151m","\033[38;5;65m"};
    else if(name=="midnight") palette_={"\033[38;5;111m","\033[38;5;75m","\033[38;5;84m","\033[38;5;222m","\033[38;5;203m","\033[38;5;252m","\033[38;5;244m"};
    else if(name=="amber") palette_={"\033[38;5;214m","\033[38;5;220m","\033[38;5;190m","\033[38;5;226m","\033[38;5;196m","\033[38;5;223m","\033[38;5;130m"};
    else if(name=="ice") palette_={"\033[38;5;117m","\033[38;5;45m","\033[38;5;123m","\033[38;5;229m","\033[38;5;203m","\033[38;5;255m","\033[38;5;67m"};
    else if(name=="classic-light") palette_={"\033[94m","\033[96m","\033[92m","\033[93m","\033[91m","\033[97m","\033[2m"};
    else if(name=="solarized") palette_={"\033[38;5;136m","\033[38;5;37m","\033[38;5;64m","\033[38;5;166m","\033[38;5;160m","\033[38;5;230m","\033[38;5;244m"};
    else if(name=="dracula") palette_={"\033[38;5;212m","\033[38;5;141m","\033[38;5;84m","\033[38;5;228m","\033[38;5;203m","\033[38;5;255m","\033[38;5;241m"};
    else if(name=="nord") palette_={"\033[38;5;110m","\033[38;5;81m","\033[38;5;108m","\033[38;5;222m","\033[38;5;167m","\033[38;5;254m","\033[38;5;245m"};
    else if(name=="cyberpunk") palette_={"\033[38;5;201m","\033[38;5;51m","\033[38;5;118m","\033[38;5;226m","\033[38;5;196m","\033[38;5;255m","\033[38;5;93m"};
    else if(name=="blood-moon") palette_={"\033[38;5;196m","\033[38;5;208m","\033[38;5;203m","\033[38;5;220m","\033[38;5;160m","\033[38;5;255m","\033[38;5;88m"};
    else if(name=="ocean") palette_={"\033[38;5;39m","\033[38;5;45m","\033[38;5;48m","\033[38;5;229m","\033[38;5;203m","\033[38;5;255m","\033[38;5;24m"};
    else if(name=="retro-blue") palette_={"\033[38;5;75m","\033[38;5;117m","\033[38;5;81m","\033[38;5;229m","\033[38;5;203m","\033[38;5;255m","\033[38;5;60m"};
    else if(name=="blue-box") palette_={"\033[38;5;33m","\033[38;5;45m","\033[38;5;51m","\033[38;5;226m","\033[38;5;196m","\033[38;5;255m","\033[38;5;24m"};
    else if(name=="red-box") palette_={"\033[38;5;196m","\033[38;5;203m","\033[38;5;208m","\033[38;5;226m","\033[38;5;160m","\033[38;5;255m","\033[38;5;88m"};
    else if(name=="beige-box") palette_={"\033[38;5;223m","\033[38;5;180m","\033[38;5;150m","\033[38;5;214m","\033[38;5;167m","\033[38;5;230m","\033[38;5;101m"};
    else if(name=="2600") palette_={"\033[38;5;46m","\033[38;5;51m","\033[38;5;118m","\033[38;5;226m","\033[38;5;201m","\033[38;5;255m","\033[38;5;34m"};
    else if(name=="wargames") palette_={"\033[38;5;40m","\033[38;5;82m","\033[38;5;46m","\033[38;5;154m","\033[38;5;196m","\033[38;5;120m","\033[38;5;22m"};
    else if(name=="crt-green") palette_={"\033[38;5;118m","\033[38;5;120m","\033[38;5;156m","\033[38;5;190m","\033[38;5;203m","\033[38;5;151m","\033[38;5;65m"};
    else if(name=="vt220") palette_={"\033[38;5;252m","\033[38;5;250m","\033[38;5;255m","\033[38;5;229m","\033[38;5;203m","\033[38;5;255m","\033[38;5;244m"};
    else if(name=="cobalt") palette_={"\033[38;5;69m","\033[38;5;81m","\033[38;5;48m","\033[38;5;221m","\033[38;5;203m","\033[38;5;254m","\033[38;5;25m"};
    else if(name=="vaporwave") palette_={"\033[38;5;213m","\033[38;5;51m","\033[38;5;117m","\033[38;5;228m","\033[38;5;203m","\033[38;5;255m","\033[38;5;93m"};
    else if(name=="stealth") palette_={"\033[38;5;245m","\033[38;5;250m","\033[38;5;108m","\033[38;5;180m","\033[38;5;167m","\033[38;5;252m","\033[38;5;238m"};
    else if(name=="monochrome") palette_={"\033[97m","\033[37m","\033[97m","\033[37m","\033[91m","\033[97m","\033[2m"};
    else palette_={BRIGHT_YELLOW,BRIGHT_CYAN,BRIGHT_GREEN,BRIGHT_YELLOW,BRIGHT_RED,WHITE,DIM};
    return true;
}

const char* CliDashboard::pageName(DashboardPage page)
{
    switch(page){
        case DashboardPage::Main:return "MAIN";
        case DashboardPage::SipLog:return "SIP LOG";
        case DashboardPage::Media:return "MEDIA / RTP";
        case DashboardPage::Calls:return "ACTIVE CALLS";
        case DashboardPage::SecurityAudit:return "SECURITY AUDIT";
        case DashboardPage::Profile:return "PROFILE / CONFIG";
        case DashboardPage::Help:return "HELP";
        case DashboardPage::EngineLog:return "ENGINE LOG";
        case DashboardPage::QueueActivity:return "QUEUE / ACTIVITY";
    }
    return "MAIN";
}

CliDashboard::TerminalSize CliDashboard::terminalSize() const
{
    TerminalSize size;bool measured=false;
#ifndef _WIN32
    winsize ws{};
    if(::ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws)==0 && ws.ws_col>0 && ws.ws_row>0){
        size.columns=ws.ws_col;size.rows=ws.ws_row;measured=true;
    }
#endif
    // COLUMNS/LINES are only fallbacks. Exported shell values can become stale
    // after a terminal-emulator resize, so ioctl wins whenever it is available.
    if(!measured){
        if(const char* columns=std::getenv("COLUMNS")){try{const int n=std::stoi(columns);if(n>20)size.columns=n;}catch(...){}}
        if(const char* lines=std::getenv("LINES")){try{const int n=std::stoi(lines);if(n>10)size.rows=n;}catch(...){}}
    }
    size.columns=std::max(36,size.columns);
    size.rows=std::max(14,size.rows);
    return size;
}

std::string CliDashboard::paint(const std::string& text,const char* ansi) const
{
    if(!color_) return text;
    const std::string_view role=ansi?std::string_view(ansi):std::string_view{};
    const std::string* mapped=nullptr;
    if(role==BRIGHT_YELLOW || role==YELLOW) mapped=&palette_.brand;
    else if(role==BRIGHT_CYAN) mapped=&palette_.accent;
    else if(role==BRIGHT_GREEN) mapped=&palette_.success;
    else if(role==BRIGHT_RED) mapped=&palette_.error;
    else if(role==WHITE) mapped=&palette_.text;
    else if(role==DIM) mapped=&palette_.dim;
    const std::string code=mapped?*mapped:std::string(ansi?ansi:"");
    return code+text+RESET;
}

std::size_t CliDashboard::visibleLength(const std::string& value)
{
    std::size_t length=0;
    for(std::size_t i=0;i<value.size();){
        if(value[i]=='\033' && i+1<value.size() && value[i+1]=='['){
            i+=2;
            while(i<value.size() && !(value[i]>='@' && value[i]<='~')) ++i;
            if(i<value.size()) ++i;
            continue;
        }
        const unsigned char c=static_cast<unsigned char>(value[i]);
        std::size_t bytes=1;
        if((c&0xE0u)==0xC0u) bytes=2;
        else if((c&0xF0u)==0xE0u) bytes=3;
        else if((c&0xF8u)==0xF0u) bytes=4;
        if(i+bytes>value.size()) bytes=1;
        i+=bytes;
        ++length;
    }
    return length;
}

std::string CliDashboard::padVisible(const std::string& value,std::size_t width)
{
    const auto length=visibleLength(value);
    if(length>=width) return value;
    return value+std::string(width-length,' ');
}

std::string CliDashboard::fit(const std::string& value,std::size_t width)
{
    if(width==0) return {};
    if(visibleLength(value)<=width) return value;
    const bool ellipsis=width>3;
    const std::size_t target=ellipsis?width-3:width;
    std::string out;
    std::size_t cells=0;
    bool copiedAnsi=false;
    for(std::size_t i=0;i<value.size() && cells<target;){
        if(value[i]=='\033' && i+1<value.size() && value[i+1]=='['){
            const auto begin=i;i+=2;
            while(i<value.size() && !(value[i]>='@' && value[i]<='~')) ++i;
            if(i<value.size()) ++i;
            out.append(value,begin,i-begin);copiedAnsi=true;continue;
        }
        const auto begin=i;const unsigned char c=static_cast<unsigned char>(value[i]);
        std::size_t bytes=1;
        if((c&0xE0u)==0xC0u) bytes=2;
        else if((c&0xF0u)==0xE0u) bytes=3;
        else if((c&0xF8u)==0xF0u) bytes=4;
        if(i+bytes>value.size()) bytes=1;
        i+=bytes;out.append(value,begin,bytes);++cells;
    }
    if(ellipsis) out+="...";
    if(copiedAnsi) out+=RESET;
    return out;
}

std::string CliDashboard::duration(std::uint64_t startMs,std::uint64_t endMs)
{
    if(startMs==0) return "--:--";
    if(endMs==0) endMs=nowMs();
    const std::uint64_t seconds=endMs>startMs?(endMs-startMs)/1000:0;
    const auto hours=seconds/3600;
    const auto minutes=(seconds%3600)/60;
    const auto secs=seconds%60;
    std::ostringstream out;
    if(hours) out<<std::setw(2)<<std::setfill('0')<<hours<<":";
    out<<std::setw(2)<<std::setfill('0')<<minutes<<":"<<std::setw(2)<<secs;
    return out.str();
}

std::vector<std::string> CliDashboard::splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while(std::getline(in,line)) lines.push_back(line);
    if(lines.empty()) lines.push_back({});
    return lines;
}

std::vector<std::string> CliDashboard::panelLines(const std::string& title,const std::vector<std::string>& lines,int width) const
{
    width=std::max(width,12);
    const int inner=width-4;
    std::vector<std::string> out;
    out.reserve(lines.size()+3);
    out.push_back("+"+std::string(static_cast<std::size_t>(width-2),'-')+"+");
    if(!title.empty()){
        const std::string tag="[ "+title+" ]";
        out.push_back("| "+padVisible(paint(fit(tag,static_cast<std::size_t>(inner)),BRIGHT_CYAN),static_cast<std::size_t>(inner))+" |");
    }
    for(const auto& line:lines){
        std::string clipped=line;
        if(visibleLength(clipped)>static_cast<std::size_t>(inner)){
            // Lines that need truncation are deliberately plain at call sites.
            clipped=fit(clipped,static_cast<std::size_t>(inner));
        }
        out.push_back("| "+padVisible(clipped,static_cast<std::size_t>(inner))+" |");
    }
    out.push_back("+"+std::string(static_cast<std::size_t>(width-2),'-')+"+");
    return out;
}

std::string CliDashboard::panel(const std::string& title,const std::vector<std::string>& lines,int width) const
{
    std::ostringstream out;
    for(const auto& line:panelLines(title,lines,width)) out<<line<<'\n';
    return out.str();
}

std::vector<std::string> CliDashboard::headerLines(const DashboardState&,int width,bool compact) const
{
    const std::string version=TRUNKMONKEY_VERSION;
    std::vector<std::string> lines;
    if((!compact && !consoleTty_ && width>=58) || (compact && width>=80)){
        static const char* logo=u8R"SIPHER(  ██████  ██▓ ██▓███   ██░ ██ ▓█████  ██▀███
▒██    ▒ ▓██▒▓██░  ██▒▓██░ ██▒▓█   ▀ ▓██ ▒ ██▒
░ ▓██▄   ▒██▒▓██░ ██▓▒▒██▀▀██░▒███   ▓██ ░▄█ ▒
  ▒   ██▒░██░▒██▄█▓▒ ▒░▓█ ░██ ▒▓█  ▄ ▒██▀▀█▄
▒██████▒▒░██░▒██▒ ░  ░░▓█▒░██▓░▒████▒░██▓ ▒██▒
▒ ▒▓▒ ▒ ░░▓  ▒▓▒░ ░  ░ ▒ ░░▒░▒░░ ▒░ ░░ ▒▓ ░▒▓░
░ ░▒  ░ ░ ▒ ░░▒ ░      ▒ ░▒░ ░ ░ ░  ░  ░▒ ░ ▒░
░  ░  ░   ▒ ░░░        ░  ░░ ░   ░     ░░   ░
      ░   ░            ░  ░  ░   ░  ░   ░)SIPHER";
        auto art=splitLines(logo);
        for(const auto& line:art) lines.push_back(paint(line,BRIGHT_YELLOW));
        lines.push_back(paint("S.I.P.H.E.R. By GITSC",BRIGHT_YELLOW)+"  "+paint(version,WHITE));
    } else {
        lines.push_back(paint("S.I.P.H.E.R. By GITSC",BRIGHT_YELLOW)+"  "+paint(version,WHITE));
    }
    lines.push_back(paint("SIP Inspection, Protocol Handling, Enumeration & Recon",BRIGHT_CYAN));
    lines.push_back("SIP / RTP Troubleshooting, Queue Testing & PBX Diagnostics");
    if(compact || consoleTty_){
        lines.push_back("1-9 guided workflows | /dial /hangup /hold /resume | help | 0 Exit");
        return lines;
    }
    lines.push_back("");
    lines.push_back(paint("Operator Mode",BRIGHT_GREEN)+" is the default: choose a number and answer the prompts.");
    lines.push_back("Slash commands are accepted directly: /dial, /answer, /hangup, /hold, /resume, /dtmf.");
    lines.push_back("Theme: "+paint(themeName_,BRIGHT_CYAN)+"   Platform: "+paint("Linux / FreeBSD / Windows",DIM));
    if(width>=90) lines.push_back(paint("Tip: type 'menu' anytime to reopen the guided menu.",DIM));
    return lines;
}

std::vector<std::string> CliDashboard::accountLines(const DashboardState& state,int width) const
{
    const auto& p=state.profile;
    const std::string transport=upper(toString(p.transport))+" (0.0.0.0:"+std::to_string(p.localSipPort)+")";
    std::string status=stateWord(state.registered,state.registrationText);
    if(state.registered) status=paint(status,BRIGHT_GREEN);
    else if(status=="REGISTERING") status=paint(status,BRIGHT_YELLOW);
    else status=paint(status,BRIGHT_RED);
    const std::size_t valueWidth=static_cast<std::size_t>(std::max(10,width-23));
    return {
        labelValue("User",fit(p.username,valueWidth)),
        labelValue("Display",fit(p.displayName,valueWidth)),
        labelValue("Domain",fit(p.sipDomain,valueWidth)),
        labelValue("Transport",fit(transport,valueWidth)),
        labelValue("Status",status),
        labelValue("Call Count Limit",std::to_string(state.maxCalls)),
        labelValue("User Agent","S.I.P.H.E.R./1.0.0")
    };
}

std::vector<std::string> CliDashboard::registrationLines(const DashboardState& state,int width) const
{
    const auto& p=state.profile;
    const std::size_t valueWidth=static_cast<std::size_t>(std::max(8,width-24));
    std::string stateText=state.registrationText;
    if(stateText.empty()) stateText=state.registered?"Registered":"Not registered";
    if(state.registered) stateText=paint(fit(stateText,valueWidth),BRIGHT_GREEN);
    else stateText=paint(fit(stateText,valueWidth),BRIGHT_YELLOW);
    std::vector<std::string> lines={
        labelValue("State",stateText),
        labelValue("SIP URI",fit("sip:"+p.username+"@"+p.sipDomain,valueWidth)),
        labelValue("Registrar",fit(p.registrar,valueWidth)),
        labelValue("Transport",upper(toString(p.transport))),
        labelValue("Expires",std::to_string(p.registrationExpires)+" seconds")
    };
    if(!p.outboundProxy.empty()) lines.push_back(labelValue("Outbound proxy",fit(p.outboundProxy,valueWidth)));
    return lines;
}

std::vector<std::string> CliDashboard::callLines(const DashboardState& state,int width,bool compact,int maxEntries) const
{
    std::vector<CallSnapshot> active;
    for(const auto& call:state.calls) if(!call.disconnected) active.push_back(call);
    std::vector<std::string> lines;
    if(active.empty()){
        lines.push_back(paint("No active calls",DIM));
        lines.push_back("Total active: 0   Limit: "+std::to_string(state.maxCalls));
        return lines;
    }

    if(compact || width<90){
        lines.push_back("ID  STATE        DIR  REMOTE                     TIME      CODEC");
        lines.push_back("--  -----------  ---  -------------------------  --------  --------");
        int shown=0;
        for(const auto& c:active){
            if(maxEntries>0 && shown>=maxEntries)break;
            ++shown;
            std::ostringstream row;
            const std::string remote=fit(remoteParty(c.remoteUri),25);
            const std::string elapsed=duration(c.connectedMs?c.connectedMs:c.createdMs);
            row<<std::setw(2)<<c.id<<"  "<<std::left<<std::setw(11)<<fit(c.state,11)<<"  "
               <<std::setw(3)<<(c.direction==CallDirection::Incoming?"IN":"OUT")<<"  "
               <<std::setw(25)<<remote<<"  "<<std::setw(8)<<elapsed<<"  "<<fit(c.codecName.empty()?"--":c.codecName,8);
            std::string rendered=row.str();
            if(c.connected) rendered=paint(rendered,BRIGHT_GREEN);
            lines.push_back(rendered);
        }
    }else{
        lines.push_back("ID  STATE        DIR FG  REMOTE PARTY                  TIME      CODEC      RTP IP:PORT");
        lines.push_back("--  -----------  --- --  ----------------------------  --------  ---------  ----------------------");
        int shown=0;
        for(const auto& c:active){
            if(maxEntries>0 && shown>=maxEntries)break;
            ++shown;
            std::ostringstream row;
            const std::string elapsed=duration(c.connectedMs?c.connectedMs:c.createdMs);
            row<<std::setw(2)<<c.id<<"  "<<std::left<<std::setw(11)<<fit(c.state,11)<<"  "
               <<std::setw(3)<<(c.direction==CallDirection::Incoming?"IN":"OUT")<<" "
               <<std::setw(2)<<(c.foreground?"*":"")<<"  "
               <<std::setw(28)<<fit(remoteParty(c.remoteUri),28)<<"  "<<std::setw(8)<<elapsed<<"  "
               <<std::setw(9)<<fit(c.codecName.empty()?"--":c.codecName,9)<<"  "<<fit(c.remoteRtpAddress.empty()?"--":c.remoteRtpAddress,22);
            std::string rendered=row.str();
            if(c.connected) rendered=paint(rendered,BRIGHT_GREEN);
            lines.push_back(rendered);
        }
    }
    if(maxEntries>0 && static_cast<int>(active.size())>maxEntries)lines.push_back("... "+std::to_string(active.size()-static_cast<std::size_t>(maxEntries))+" more active call(s) — Alt+4 for full call view");
    lines.push_back("Total active: "+std::to_string(active.size())+"   Limit: "+std::to_string(state.maxCalls));
    return lines;
}

std::vector<std::string> CliDashboard::diagnosticLines(const DashboardState& state,int width,int maxTraceRows) const
{
    if(state.focusCallId<0) return {paint("Use Operator Menu option 4, or select a call with 'media <id>' / 'siplog <id>'.",DIM)};
    const auto it=std::find_if(state.calls.begin(),state.calls.end(),[&](const CallSnapshot& c){return c.id==state.focusCallId;});
    if(it==state.calls.end()) return {paint("Selected call is no longer available.",DIM)};
    const auto& c=*it;
    const std::size_t valueWidth=static_cast<std::size_t>(std::max(10,width-26));
    std::vector<std::string> lines={
        "Call "+std::to_string(c.id)+"  "+(c.direction==CallDirection::Incoming?"INCOMING":"OUTGOING")+"  "+c.state,
        labelValue("SIP Call-ID",fit(c.callIdString.empty()?"--":c.callIdString,valueWidth)),
        labelValue("RTP target",fit(c.remoteRtpAddress.empty()?"--":c.remoteRtpAddress,valueWidth)),
        labelValue("RTP source seen",fit(c.sourceRtpAddress.empty()?"--":c.sourceRtpAddress,valueWidth)),
        labelValue("Local RTP",fit(c.localRtpAddress.empty()?"--":c.localRtpAddress,valueWidth)),
        labelValue("Codec",c.codecName.empty()?"--":c.codecName+(c.codecClockRate?" / "+std::to_string(c.codecClockRate)+" Hz":""))
    };
    if(!state.captureStatus.empty()) lines.push_back(labelValue("Capture",fit(state.captureStatus,valueWidth)));
    if(!state.focusTrace.empty() && maxTraceRows>0){
        lines.push_back("");
        lines.push_back(paint("Recent SIP signals",BRIGHT_CYAN));
        const std::size_t first=state.focusTrace.size()>static_cast<std::size_t>(maxTraceRows)?state.focusTrace.size()-static_cast<std::size_t>(maxTraceRows):0;
        for(std::size_t i=first;i<state.focusTrace.size();++i){
            const auto& e=state.focusTrace[i];
            std::ostringstream raw;
            raw<<"["<<std::setw(3)<<i<<"] "<<(e.direction==SipDirection::Sent?"TX --> ":"RX <-- ")<<e.label;
            if(e.cseq) raw<<"  CSeq="<<e.cseq;
            const std::string text=fit(raw.str(),static_cast<std::size_t>(std::max(10,width-6)));
            lines.push_back(paint(text,e.direction==SipDirection::Sent?BRIGHT_YELLOW:BRIGHT_GREEN));
        }
    }
    return lines;
}

std::vector<std::string> CliDashboard::securityLines(const DashboardState& state,int width) const
{
    const std::size_t w=static_cast<std::size_t>(std::max(20,width-6));
    std::vector<std::string> lines={
        "Authorized systems only. These are bounded assessment/recon probes.",
        "audit-fingerprint <host> [port] [udp|tcp]    identify PBX/SIP stack and disclosed capabilities",
        "audit-vulns <host> [port] [udp|tcp]          fingerprint + NVD CVE + Exploit-DB metadata correlation",
        "audit-probe | audit-methods | audit-auth | audit-oracle | audit-ext",
        "audit-compliance | audit-parser | audit-resilience | audit-scenario | audit-tls | audit-full",
        "NVD_API_KEY may be exported for a higher NVD request allowance. No exploit code is executed."
    };
    if(!state.auditSummary.empty()){
        lines.push_back("");
        lines.push_back("Most recent audit output:");
        auto summary=splitLines(state.auditSummary);
        const int maxLines=consoleTty_?5:10;
        for(int i=0;i<std::min<int>(maxLines,static_cast<int>(summary.size()));++i) lines.push_back(fit(summary[static_cast<std::size_t>(i)],w));
    }
    return lines;
}

std::vector<std::string> CliDashboard::activityLines(const DashboardState& state,int width,int maxRows) const
{
    if(state.notices.empty()) return {paint("Ready. Select 1-9 for a guided workflow, or type 'help' for advanced commands.",DIM)};
    std::vector<std::string> lines;
    const std::size_t first=state.notices.size()>static_cast<std::size_t>(maxRows)?state.notices.size()-static_cast<std::size_t>(maxRows):0;
    for(std::size_t i=first;i<state.notices.size();++i){
        const auto& notice=state.notices[i];
        const char* color=WHITE;
        if(notice.level==DashboardNotice::Level::Success) color=BRIGHT_GREEN;
        else if(notice.level==DashboardNotice::Level::Warning) color=BRIGHT_YELLOW;
        else if(notice.level==DashboardNotice::Level::Error) color=BRIGHT_RED;
        lines.push_back(paint(fit(notice.text,static_cast<std::size_t>(std::max(10,width-6))),color));
    }
    return lines;
}

std::vector<std::string> CliDashboard::quickCommandLines(int width,bool compact) const
{
    if(compact){
        std::vector<std::string> lines={
            "1 Call | 2 Calls | 3 Queue",
            "4 Diagnostics | 5 Security Audit | 6 Audio",
            "7 Profile | 8 Themes | 9 Logs",
            "10 Commands | 0 Exit"
        };
        for(auto& line:lines)line=paint(fit(line,static_cast<std::size_t>(std::max(10,width-6))),BRIGHT_CYAN);
        return lines;
    }
    const std::vector<std::pair<std::string,std::string>> commands={
        {"1","Place a call"},{"2","Manage active calls"},{"3","Queue / call-blast test"},{"4","Call diagnostics & PCAP"},{"5","PBX / SIP security audit"},
        {"6","Audio & registration"},{"7","SIP account / profile"},{"8","Themes & display"},{"9","Logs & capture utilities"},{"10","Advanced command reference"},{"0","Exit"}
    };
    std::vector<std::string> lines;
    for(std::size_t i=0;i<commands.size();++i){std::ostringstream row;row<<std::left<<std::setw(4)<<commands[i].first<<" "<<commands[i].second;lines.push_back(paint(fit(row.str(),static_cast<std::size_t>(std::max(10,width-6))),i<5?BRIGHT_CYAN:WHITE));}
    return lines;
}


std::vector<std::string> CliDashboard::sipPageLines(const DashboardState& state,int width,int maxRows) const
{
    if(state.focusCallId<0) return {paint("Use Operator Menu option 4, or select a phone call with 'siplog <id>' / 'media <id>'.",DIM)};
    std::vector<std::string> lines;
    lines.push_back("Focused call: "+std::to_string(state.focusCallId)+"   "+state.captureStatus);
    lines.push_back("IDX  TIME          DIR  CSEQ      SIGNAL / STATUS");
    lines.push_back("---  ------------  ---  --------  ------------------------------------------------------------");
    const std::size_t first=state.focusTrace.size()>static_cast<std::size_t>(maxRows)?state.focusTrace.size()-static_cast<std::size_t>(maxRows):0;
    for(std::size_t i=first;i<state.focusTrace.size();++i){
        const auto& e=state.focusTrace[i];
        std::ostringstream row;
        const auto ms=e.timestampMs%1000;
        std::time_t t=static_cast<std::time_t>(e.timestampMs/1000);std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm,&t);
#else
        localtime_r(&t,&tm);
#endif
        row<<std::setw(3)<<i<<"  "<<std::put_time(&tm,"%H:%M:%S")<<'.'<<std::setw(3)<<std::setfill('0')<<ms<<std::setfill(' ')<<"  "
           <<(e.direction==SipDirection::Sent?"TX ":"RX ")<<"  "<<std::setw(8)<<e.cseq<<"  "<<e.label;
        if(e.statusCode) row<<"  "<<e.statusCode<<" "<<e.reason;
        lines.push_back(paint(fit(row.str(),static_cast<std::size_t>(std::max(10,width-6))),e.direction==SipDirection::Sent?BRIGHT_YELLOW:BRIGHT_GREEN));
    }
    if(state.focusTrace.empty()) lines.push_back(paint("No SIP messages captured for this call yet.",DIM));
    lines.push_back("");
    lines.push_back("sipraw <id> <index> opens the full message.  Alt+1 returns to Main.");
    return lines;
}

std::vector<std::string> CliDashboard::engineLogPageLines(const DashboardState& state,int width,int maxRows) const
{
    std::vector<std::string> lines;
    lines.push_back("PJSIP engine log: "+(state.engineLogPath.empty()?std::string("--"):state.engineLogPath));
    lines.push_back("PgUp/PgDn or log-up/log-down scroll | log-tail | Alt+1 Main");
    lines.push_back("");
    if(state.engineLogLines.empty()){
        lines.push_back(paint("No PJSIP engine log lines are available yet.",DIM));
        return lines;
    }
    const std::size_t total=state.engineLogLines.size();
    const std::size_t offset=std::min(state.engineLogOffset,total);
    const std::size_t end=total-offset;
    const std::size_t rows=static_cast<std::size_t>(std::max(4,maxRows-4));
    const std::size_t begin=end>rows?end-rows:0;
    std::ostringstream meta;
    meta<<"Showing lines "<<(begin+1)<<"-"<<end<<" of "<<total;
    if(offset) meta<<" ("<<offset<<" line(s) above live tail)";
    else meta<<" (live tail)";
    lines.push_back(paint(meta.str(),BRIGHT_CYAN));
    for(std::size_t i=begin;i<end;++i){
        std::ostringstream row;row<<std::setw(5)<<(i+1)<<"  "<<state.engineLogLines[i];
        lines.push_back(fit(row.str(),static_cast<std::size_t>(std::max(10,width-6))));
    }
    return lines;
}

std::vector<std::string> CliDashboard::profileLines(const DashboardState& state,int width) const
{
    const auto& p=state.profile;const std::size_t vw=static_cast<std::size_t>(std::max(12,width-28));
    return {
        labelValue("Profile file",fit(state.profilePath.empty()?"--":state.profilePath,vw)),
        labelValue("Profile name",fit(p.name,vw)),labelValue("SIP domain",fit(p.sipDomain,vw)),
        labelValue("Registrar",fit(p.registrar,vw)),labelValue("Username",fit(p.username,vw)),
        labelValue("Auth username",fit(p.authUsername,vw)),labelValue("Password",p.password.empty()?"<empty>":"<saved>"),
        labelValue("Display name",fit(p.displayName,vw)),labelValue("Outbound proxy",fit(p.outboundProxy.empty()?"--":p.outboundProxy,vw)),
        labelValue("Caller-ID domain",fit(p.callerIdDomain.empty()?"--":p.callerIdDomain,vw)),labelValue("Transport",upper(toString(p.transport))),
        labelValue("Local SIP port",std::to_string(p.localSipPort)),labelValue("Registration expires",std::to_string(p.registrationExpires)+" sec"),
        labelValue("Identity mode",toString(p.identityMode)),labelValue("STUN",fit(p.stunServer.empty()?"--":p.stunServer,vw)),
        labelValue("ICE",p.useIce?"enabled":"disabled"),labelValue("SRTP",p.enableSrtp?"enabled":"disabled"),
        "",paint("Commands: profile-edit | profile-reload | profile-show",BRIGHT_CYAN)
    };
}

std::vector<std::string> CliDashboard::pageBarLines(DashboardPage page,int width) const
{
    const std::vector<std::string> labels={"Alt+1 Main","Alt+2 SIP","Alt+3 Media","Alt+4 Calls","Alt+5 Security","Alt+6 Profile","Alt+7 Help","Alt+8 Engine","Alt+9 Queue"};
    const int available=std::max(16,width-4);std::vector<std::string> rows;
    std::string row=paint("▓█ S.I.P.H.E.R. █▓",BRIGHT_YELLOW);
    for(std::size_t i=0;i<labels.size();++i){
        const bool current=static_cast<int>(page)==static_cast<int>(i+1);
        const std::string item=current?paint("["+labels[i]+"]",BRIGHT_GREEN):paint(labels[i],BRIGHT_CYAN);
        const std::size_t extra=(row.empty()?0:3)+visibleLength(item);
        if(!row.empty() && static_cast<int>(visibleLength(row)+extra)>available){rows.push_back(row);row.clear();}
        if(!row.empty())row+=" | ";
        row+=item;
    }
    if(!row.empty())rows.push_back(row);
    return rows;
}

std::vector<std::string> CliDashboard::mergeColumns(const std::vector<std::string>& left,
                                                     const std::vector<std::string>& right,
                                                     int leftWidth,int gap)
{
    const std::size_t count=std::max(left.size(),right.size());
    std::vector<std::string> lines;
    lines.reserve(count);
    for(std::size_t i=0;i<count;++i){
        const std::string l=i<left.size()?left[i]:std::string{};
        const std::string r=i<right.size()?right[i]:std::string{};
        lines.push_back(padVisible(l,static_cast<std::size_t>(leftWidth))+std::string(static_cast<std::size_t>(gap),' ')+r);
    }
    return lines;
}

void CliDashboard::clear(std::ostream& out) const
{
    if(enabled_) out<<"\033[2J\033[H";
}

void CliDashboard::render(const DashboardState& state,std::ostream& out) const
{
    if(!enabled_) return;
    const auto size=terminalSize();
    const bool wide=!consoleTty_ && size.columns>=118;
    const bool compact=consoleTty_ || size.rows<48 || size.columns<84;
    const bool tiny=consoleTty_ || size.rows<30;
    const int gap=2;
    const int rightWidth=wide?std::min(52,std::max(40,size.columns/3)):size.columns;
    const int leftWidth=wide?size.columns-rightWidth-gap:size.columns;
    clear(out);
    out<<"\033]0;S.I.P.H.E.R. "<<TRUNKMONKEY_VERSION<<" — "<<pageName(state.page)<<"\007";

    for(const auto& line:panelLines("",pageBarLines(state.page,size.columns),size.columns)) out<<line<<'\n';

    if(state.page==DashboardPage::Main){
        auto header=panelLines("",headerLines(state,leftWidth,compact),leftWidth);
        if(compact){
            for(const auto& line:header)out<<line<<'\n';
            const bool logoPriority=leftWidth>=80;
            const auto& p=state.profile;
            std::vector<std::string> status={
                "SIP: "+state.registrationText,
                "Account: "+fit(p.username+"@"+p.sipDomain,static_cast<std::size_t>(std::max(12,leftWidth-15))),
                "Calls: "+std::to_string(std::count_if(state.calls.begin(),state.calls.end(),[](const CallSnapshot& c){return !c.disconnected;}))+" / "+std::to_string(state.maxCalls),
                "Theme: "+themeName_+"   /commands and /hangup are available"
            };
            // In a classic 80x25-ish TTY, preserve the full block logo and shed
            // lower-priority panels instead of scrolling the dashboard off-screen.
            if(!(logoPriority && size.rows<36))
                for(const auto& line:panelLines("STATUS",status,leftWidth))out<<line<<'\n';
            const bool anyActive=std::any_of(state.calls.begin(),state.calls.end(),[](const CallSnapshot& c){return !c.disconnected;});
            if(anyActive && !(logoPriority && size.rows<36)){for(const auto& line:panelLines("ACTIVE CALLS",callLines(state,leftWidth,true,tiny?1:3),leftWidth))out<<line<<'\n';}
            if(!(logoPriority && size.rows<28))
                for(const auto& line:panelLines("OPERATOR MENU",quickCommandLines(leftWidth,true),leftWidth))out<<line<<'\n';
            if(!tiny && !(logoPriority && size.rows<42)){for(const auto& line:panelLines("ACTIVITY",activityLines(state,leftWidth,2),leftWidth))out<<line<<'\n';}
        }else{
            if(wide){auto account=panelLines("ACCOUNT",accountLines(state,rightWidth),rightWidth);for(const auto& line:mergeColumns(header,account,leftWidth,gap))out<<line<<'\n';}
            else{for(const auto& line:header)out<<line<<'\n';for(const auto& line:panelLines("ACCOUNT",accountLines(state,leftWidth),leftWidth))out<<line<<'\n';}
            std::vector<std::string> left;
            auto reg=panelLines("REGISTRATION",registrationLines(state,leftWidth),leftWidth);left.insert(left.end(),reg.begin(),reg.end());
            auto calls=panelLines("ACTIVE CALLS",callLines(state,leftWidth,true,6),leftWidth);left.insert(left.end(),calls.begin(),calls.end());
            auto activity=panelLines("ACTIVITY",activityLines(state,leftWidth,8),leftWidth);left.insert(left.end(),activity.begin(),activity.end());
            if(wide){auto quick=panelLines("OPERATOR MENU",quickCommandLines(rightWidth,false),rightWidth);for(const auto& line:mergeColumns(left,quick,leftWidth,gap))out<<line<<'\n';}
            else{for(const auto& line:left)out<<line<<'\n';for(const auto& line:panelLines("OPERATOR MENU",quickCommandLines(leftWidth,false),leftWidth))out<<line<<'\n';}
        }
    }else if(state.page==DashboardPage::SipLog){
        for(const auto& line:panelLines("SIP LOG — CALL "+(state.focusCallId>=0?std::to_string(state.focusCallId):std::string("--")),sipPageLines(state,size.columns,std::max(8,size.rows-12)),size.columns))out<<line<<'\n';
    }else if(state.page==DashboardPage::Media){
        for(const auto& line:panelLines("MEDIA / RTP DIAGNOSTICS",diagnosticLines(state,size.columns,0),size.columns))out<<line<<'\n';
        for(const auto& line:panelLines("CAPTURE",{state.captureStatus,"capture-ifaces | sipcap-start <id> <file> [iface] | rtpcap-start <id> <file> [iface] | capture-stop all"},size.columns))out<<line<<'\n';
    }else if(state.page==DashboardPage::Calls){
        for(const auto& line:panelLines("ACTIVE CALLS",callLines(state,size.columns,size.columns<90,std::max(2,size.rows-12)),size.columns))out<<line<<'\n';
        for(const auto& line:panelLines("CALL CONTROL",{"Operator Mode: select 2 from Main for guided call control.","Advanced: foreground <id> | answer <id> | hold <id> | resume <id> | dtmf <id> <digits> | hangup <id>"},size.columns))out<<line<<'\n';
    }else if(state.page==DashboardPage::SecurityAudit){
        for(const auto& line:panelLines("PBX SECURITY / VULNERABILITY AUDIT",securityLines(state,size.columns),size.columns))out<<line<<'\n';
    }else if(state.page==DashboardPage::Profile){
        for(const auto& line:panelLines("SIP PROFILE / CONFIG",profileLines(state,size.columns),size.columns))out<<line<<'\n';
    }else if(state.page==DashboardPage::Help){
        for(const auto& line:panelLines("OPERATOR MENU",quickCommandLines(size.columns,compact),size.columns))out<<line<<'\n';
        for(const auto& line:panelLines("NAVIGATION",{"Alt+1..Alt+9 switches pages immediately. Select 1-9 on Main for guided workflows; type 'help' for advanced commands.","Blank Enter refreshes the current page."},size.columns))out<<line<<'\n';
    }else if(state.page==DashboardPage::EngineLog){
        for(const auto& line:panelLines("PJSIP ENGINE LOG",engineLogPageLines(state,size.columns,std::max(8,size.rows-10)),size.columns))out<<line<<'\n';
    }else if(state.page==DashboardPage::QueueActivity){
        for(const auto& line:panelLines("QUEUE TEST / ACTIVITY",activityLines(state,size.columns,std::max(8,size.rows-15)),size.columns))out<<line<<'\n';
        for(const auto& line:panelLines("QUEUE COMMANDS",{"Operator Mode: select 3 from Main for a guided queue/call-blast test.","Advanced commands remain available: blast, blast-audio, blast-file, blast-file-audio, cancel-launch."},size.columns))out<<line<<'\n';
    }
    prepareInteractivePrompt(out);
}

void CliDashboard::prepareInteractivePrompt(std::ostream& out) const
{
    if(enabled_) out<<paint("select> ",BRIGHT_GREEN)<<std::flush;
}

void CliDashboard::showOverlay(const std::string& title,const std::string& body,std::ostream& out) const
{
    if(!enabled_){out<<body; if(body.empty()||body.back()!='\n')out<<'\n'; return;}
    clear(out);
    const auto size=terminalSize();
    const int width=std::max(32,std::min(size.columns,120));
    auto lines=splitLines(body);
    for(auto& line:lines) line=fit(line,static_cast<std::size_t>(std::max(10,width-6)));
    out<<panel(title,lines,width);
}

void CliDashboard::pauseForEnter(std::istream& in,std::ostream& out) const
{
    if(!enabled_) return;
    out<<"\n"<<paint("Press Enter to return to S.I.P.H.E.R....",DIM)<<std::flush;
    std::string ignored;
    std::getline(in,ignored);
}

} // namespace trunkmonkey::cli
