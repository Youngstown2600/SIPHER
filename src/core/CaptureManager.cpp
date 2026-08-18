#include "trunkmonkey/CaptureManager.h"
#include "trunkmonkey/Logger.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifndef _WIN32
extern char **environ;
#endif
namespace trunkmonkey {
namespace {
int portFromAddress(const std::string&a){if(a.empty())return 0;auto p=a.rfind(':');if(p==std::string::npos||p+1>=a.size())return 0;try{auto v=std::stoi(a.substr(p+1));return v>0&&v<=65535?v:0;}catch(...){return 0;}}
#ifdef _WIN32
std::vector<int> portsFromFilter(const std::string&filter){std::vector<int> out;std::regex r(R"(port\s+([0-9]{1,5}))",std::regex::icase);for(std::sregex_iterator i(filter.begin(),filter.end(),r),e;i!=e;++i){try{int p=std::stoi((*i)[1].str());if(p>0&&p<=65535&&std::find(out.begin(),out.end(),p)==out.end())out.push_back(p);}catch(...){}}return out;}
std::string quoteWin(const std::string&s){std::string o="\"";for(char c:s){if(c=='\"')o+="\\\"";else o+=c;}o+='\"';return o;}
bool fileExists(const std::string&p){std::error_code ec;return std::filesystem::exists(std::filesystem::u8path(p),ec);}
std::string locateExe(const char*name){const char*portableRoot=std::getenv("SIPHER_PORTABLE_ROOT");if(!portableRoot)portableRoot=std::getenv("SAK_PORTABLE_ROOT");if(portableRoot){auto p=(std::filesystem::path(portableRoot)/"tools"/name).string();if(fileExists(p))return p;}char buf[MAX_PATH];DWORD n=SearchPathA(nullptr,name,nullptr,MAX_PATH,buf,nullptr);if(n>0&&n<MAX_PATH)return std::string(buf,n);const char*windir=std::getenv("WINDIR");if(windir){auto p=std::string(windir)+"\\System32\\"+name;if(fileExists(p))return p;}for(const char*base:{"C:\\Program Files\\Wireshark\\","C:\\Program Files (x86)\\Wireshark\\"}){auto p=std::string(base)+name;if(fileExists(p))return p;}return{};}
int runHidden(const std::string&command){std::vector<char> cmd(command.begin(),command.end());cmd.push_back('\0');STARTUPINFOA si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};if(!CreateProcessA(nullptr,cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return -1;WaitForSingleObject(pi.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return static_cast<int>(code);}
bool startHiddenProcess(const std::string&command,HANDLE&handle,DWORD&pid){std::vector<char> cmd(command.begin(),command.end());cmd.push_back('\0');STARTUPINFOA si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};if(!CreateProcessA(nullptr,cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return false;CloseHandle(pi.hThread);handle=pi.hProcess;pid=pi.dwProcessId;return true;}
bool startDesktopProcess(const std::string&command){std::vector<char> cmd(command.begin(),command.end());cmd.push_back('\0');STARTUPINFOA si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};if(!CreateProcessA(nullptr,cmd.data(),nullptr,nullptr,FALSE,CREATE_NEW_PROCESS_GROUP,nullptr,nullptr,&si,&pi))return false;CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return true;}
std::string captureHidden(const std::string&command){
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE r=nullptr,w=nullptr;if(!CreatePipe(&r,&w,&sa,0))return{};SetHandleInformation(r,HANDLE_FLAG_INHERIT,0);
    STARTUPINFOA si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);si.hStdOutput=w;si.hStdError=w;
    PROCESS_INFORMATION pi{};std::vector<char> cmd(command.begin(),command.end());cmd.push_back('\0');if(!CreateProcessA(nullptr,cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){CloseHandle(r);CloseHandle(w);return{};}CloseHandle(w);
    std::string out;char buf[2048];for(;;){DWORD got=0;if(!ReadFile(r,buf,sizeof(buf),&got,nullptr)||!got)break;out.append(buf,got);}WaitForSingleObject(pi.hProcess,5000);CloseHandle(r);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return out;
}

#else
bool executable(const std::string& path){return ::access(path.c_str(), X_OK) == 0;}
std::string locate(const char* name){const char* path=std::getenv("PATH");if(path){std::istringstream in(path);std::string dir;while(std::getline(in,dir,':')){if(dir.empty())dir=".";const auto full=dir+"/"+name;if(executable(full))return full;}}for(const char* dir:{"/usr/bin","/usr/sbin","/usr/local/bin","/usr/local/sbin"}){const auto full=std::string(dir)+"/"+name;if(executable(full))return full;}return{};}
void prepareCaptureFile(const std::string& path){const std::filesystem::path filePath(path);if(filePath.has_parent_path()){std::error_code ec;std::filesystem::create_directories(filePath.parent_path(),ec);if(ec&&!std::filesystem::is_directory(filePath.parent_path()))throw std::runtime_error("Unable to create capture directory: "+filePath.parent_path().string());}{std::ofstream out(path,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Unable to create capture file: "+path);} (void)::chmod(path.c_str(),S_IRUSR|S_IWUSR);}
std::string readTextFile(const std::string& path){std::ifstream in(path);std::ostringstream out;out<<in.rdbuf();auto s=out.str();while(!s.empty()&&(s.back()=='\n'||s.back()=='\r'))s.pop_back();return s;}
pid_t spawnCapture(const std::string& tool,const std::vector<std::string>& args,const std::string& errorPath){std::vector<char*> argv;argv.reserve(args.size()+2);argv.push_back(const_cast<char*>(tool.c_str()));for(const auto& arg:args)argv.push_back(const_cast<char*>(arg.c_str()));argv.push_back(nullptr);posix_spawn_file_actions_t actions;if(posix_spawn_file_actions_init(&actions)!=0)throw std::runtime_error("Unable to initialize packet capture process actions");const int openRc=posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,errorPath.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);if(openRc!=0){posix_spawn_file_actions_destroy(&actions);throw std::runtime_error("Unable to prepare packet capture error log: "+std::string(std::strerror(openRc)));}pid_t pid=-1;const int rc=::posix_spawn(&pid,tool.c_str(),&actions,nullptr,argv.data(),environ);posix_spawn_file_actions_destroy(&actions);if(rc!=0)throw std::runtime_error("posix_spawn() failed while starting packet capture: "+std::string(std::strerror(rc)));return pid;}
#endif
}
CaptureManager::CaptureManager(Logger&l):logger_(l){}
CaptureManager::~CaptureManager(){stopAll();}
std::string CaptureManager::findTool(){
#ifdef _WIN32
    auto p=locateExe("pktmon.exe");if(!p.empty())return p;return locateExe("dumpcap.exe");
#else
    auto d=locate("dumpcap");if(!d.empty())return d;return locate("tcpdump");
#endif
}
std::string CaptureManager::captureTool(){return findTool();}
std::string CaptureManager::wiresharkTool(){
#ifdef _WIN32
    return locateExe("Wireshark.exe");
#else
    return locate("wireshark");
#endif
}
std::vector<std::string> CaptureManager::wiresharkDecodeArguments(const std::string&path,const CallSnapshot&c){
    if(path.empty())throw std::runtime_error("PCAP path is empty");
    std::set<int>rtp,rtcp;
    for(auto*a:{&c.localRtpAddress,&c.remoteRtpAddress,&c.sourceRtpAddress}){const int p=portFromAddress(*a);if(p)rtp.insert(p);}
    for(auto*a:{&c.localRtcpAddress,&c.remoteRtcpAddress,&c.sourceRtcpAddress}){const int p=portFromAddress(*a);if(p)rtcp.insert(p);}
    for(int p:rtp)rtcp.erase(p); // RTP/RTCP mux: prefer RTP when both share one port.
    std::vector<std::string>args{"-r",path};
    for(int p:rtp){args.push_back("-d");args.push_back("udp.port=="+std::to_string(p)+",rtp");}
    for(int p:rtcp){args.push_back("-d");args.push_back("udp.port=="+std::to_string(p)+",rtcp");}
    if(rtp.empty()&&rtcp.empty()){args.push_back("--enable-heuristic");args.push_back("rtp_udp");}
    return args;
}
void CaptureManager::openInWireshark(const std::string&path,const CallSnapshot&c){
    std::error_code ec;if(!std::filesystem::exists(std::filesystem::u8path(path),ec))throw std::runtime_error("PCAP file was not found: "+path);
    const auto tool=wiresharkTool();if(tool.empty())throw std::runtime_error("Wireshark was not found. Install Wireshark or add it to PATH, then try again.");
    const auto args=wiresharkDecodeArguments(path,c);
#ifdef _WIN32
    std::ostringstream command;command<<quoteWin(tool);for(const auto&a:args)command<<" "<<quoteWin(a);if(!startDesktopProcess(command.str()))throw std::runtime_error("Unable to launch Wireshark.exe");
#else
    std::vector<char*>argv;argv.reserve(args.size()+2);argv.push_back(const_cast<char*>(tool.c_str()));for(const auto&a:args)argv.push_back(const_cast<char*>(a.c_str()));argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;if(posix_spawn_file_actions_init(&actions)!=0)throw std::runtime_error("Unable to initialize Wireshark process actions");
    (void)posix_spawn_file_actions_addopen(&actions,STDOUT_FILENO,"/dev/null",O_WRONLY,0600);(void)posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0600);
    pid_t pid=-1;const int rc=::posix_spawn(&pid,tool.c_str(),&actions,nullptr,argv.data(),environ);posix_spawn_file_actions_destroy(&actions);if(rc!=0)throw std::runtime_error("Unable to launch Wireshark: "+std::string(std::strerror(rc)));
    std::thread([pid](){int st=0;while(::waitpid(pid,&st,0)<0&&errno==EINTR){}}).detach();
#endif
}
std::vector<std::string> CaptureManager::availableInterfaces(){
#ifdef _WIN32
    const auto dumpcap=locateExe("dumpcap.exe");
    if(dumpcap.empty())return {"any"};
    const auto output=captureHidden(quoteWin(dumpcap)+" -D");std::vector<std::string> ids;std::istringstream in(output);std::string line;
    while(std::getline(in,line)){const auto dot=line.find('.');if(dot==std::string::npos)continue;const auto id=line.substr(0,dot);if(!id.empty()&&std::all_of(id.begin(),id.end(),[](unsigned char c){return std::isdigit(c)!=0;}))ids.push_back(id);}
    if(ids.empty())ids.push_back("any");return ids;
#else
    std::set<std::string> names;
#if defined(__linux__)
    names.insert("any");
#endif
    ifaddrs* list=nullptr;
    if(::getifaddrs(&list)==0){for(auto* p=list;p;p=p->ifa_next){if(p->ifa_name&&*p->ifa_name)names.insert(p->ifa_name);}::freeifaddrs(list);}
    return {names.begin(),names.end()};
#endif
}
std::string CaptureManager::permissionHint(){
#ifdef _WIN32
    return "Windows 10/11 can use built-in pktmon (Administrator may be required). Windows 7 uses dumpcap and requires a compatible packet-capture driver such as Npcap/WinPcap installed on the host.";
#elif defined(__FreeBSD__)
    return "FreeBSD capture requires user access to /dev/bpf*. Re-run ./build.sh --configure-capture to install the persistent S.I.P.H.E.R. devfs rule for your user.";
#else
    return "Linux capture requires CAP_NET_RAW/CAP_NET_ADMIN on dumpcap/tcpdump. Re-run ./build.sh --configure-capture to configure the capture helper without running S.I.P.H.E.R. as root.";
#endif
}
void CaptureManager::start(Proc&p,const std::string&path,const std::string&filter,const std::string&iface){
    if(p.running)throw std::runtime_error("Capture is already running");
    auto tool=findTool();
#ifdef _WIN32
    if(tool.empty())throw std::runtime_error("No packet capture tool found. Windows 10/11 normally provides pktmon.exe. Windows 7 requires dumpcap.exe plus an installed packet-capture driver.");
    auto lower=tool;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(lower.find("pktmon.exe")!=std::string::npos){if(sip_.running||rtp_.running||call_.running)throw std::runtime_error("Windows pktmon fallback supports one diagnostic PCAP at a time. Stop the current PCAP first.");auto ports=portsFromFilter(filter);if(ports.empty())throw std::runtime_error("Could not derive packet ports for pktmon capture.");runHidden(quoteWin(tool)+" stop");runHidden(quoteWin(tool)+" filter remove");for(std::size_t i=0;i<ports.size();++i){std::ostringstream c;c<<quoteWin(tool)<<" filter add TM"<<i<<" -p "<<ports[i];if(runHidden(c.str())!=0)throw std::runtime_error("pktmon filter setup failed. Run S.I.P.H.E.R. as Administrator for Windows PCAP capture.");}p.etlPath=path+".etl";std::error_code ec;std::filesystem::remove(std::filesystem::u8path(p.etlPath),ec);std::filesystem::remove(std::filesystem::u8path(path),ec);std::ostringstream c;c<<quoteWin(tool)<<" start --capture --comp nics --pkt-size 0 --file-name "<<quoteWin(p.etlPath);if(runHidden(c.str())!=0){runHidden(quoteWin(tool)+" filter remove");throw std::runtime_error("pktmon capture could not start. Run S.I.P.H.E.R. as Administrator.");}p.path=path;p.tool=tool;p.filter=filter;p.pktmon=true;p.running=true;logger_.info("Windows pktmon capture started file="+path+" filter="+filter);return;}
    std::string ifn=iface.empty()?"any":iface;if(ifn=="any")throw std::runtime_error("dumpcap on Windows requires a capture interface. Enter an interface name/index, or use the built-in pktmon fallback.");std::ostringstream c;c<<quoteWin(tool)<<" -q -i "<<quoteWin(ifn)<<" -f "<<quoteWin(filter)<<" -w "<<quoteWin(path);if(!startHiddenProcess(c.str(),p.process,p.pid))throw std::runtime_error("CreateProcess failed while starting dumpcap");p.path=path;p.tool=tool;p.filter=filter;p.running=true;std::this_thread::sleep_for(std::chrono::milliseconds(250));DWORD code=STILL_ACTIVE;if(!GetExitCodeProcess(p.process,&code)||code!=STILL_ACTIVE){CloseHandle(p.process);p=Proc{};throw std::runtime_error("dumpcap exited immediately. Check interface and capture permissions.");}logger_.info("Packet capture started pid="+std::to_string(p.pid)+" file="+path+" filter="+filter);
#else
    if(tool.empty())throw std::runtime_error("No packet capture tool found. Install dumpcap/Wireshark or tcpdump. "+permissionHint());
    std::string ifn=iface;
#if defined(__FreeBSD__)
    const bool specifyInterface=!ifn.empty()&&ifn!="any";
#else
    if(ifn.empty()) ifn="any";
    const bool specifyInterface=true;
#endif
    if(specifyInterface&&ifn!="any"){
        const auto interfaces=availableInterfaces();
        if(std::find(interfaces.begin(),interfaces.end(),ifn)==interfaces.end()){
            std::ostringstream msg;msg<<"Capture interface '"<<ifn<<"' was not found. Available interfaces:";for(const auto& n:interfaces)msg<<" "<<n;throw std::runtime_error(msg.str());
        }
    }
    prepareCaptureFile(path);
    std::vector<std::string> args;
    if(tool.find("dumpcap")!=std::string::npos){args.push_back("-q");if(specifyInterface){args.push_back("-i");args.push_back(ifn);}args.push_back("-f");args.push_back(filter);args.push_back("-w");args.push_back(path);}else{args.push_back("-U");args.push_back("-n");if(specifyInterface){args.push_back("-i");args.push_back(ifn);}args.push_back("-w");args.push_back(path);args.push_back(filter);}
    const std::string errorPath=path+".capture-error";
    std::error_code ec;std::filesystem::remove(errorPath,ec);
    const pid_t pid=spawnCapture(tool,args,errorPath);
    p.pid=pid;p.path=path;p.tool=tool;p.filter=filter;p.errorPath=errorPath;p.running=true;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    int st=0;const auto w=waitpid(pid,&st,WNOHANG);
    if(w==pid){const auto detail=readTextFile(errorPath);std::filesystem::remove(errorPath,ec);p=Proc{};std::string message="Packet capture exited immediately";if(!detail.empty())message+=": "+detail;message+="\n"+permissionHint();throw std::runtime_error(message);}
    logger_.info("Packet capture started pid="+std::to_string(pid)+" tool="+tool+" interface="+(specifyInterface?ifn:"default")+" file="+path+" filter="+filter);
#endif
}
void CaptureManager::stopProc(Proc&p){if(!p.running)return;
#ifdef _WIN32
    if(p.pktmon){runHidden(quoteWin(p.tool)+" stop");std::ostringstream c;c<<quoteWin(p.tool)<<" etl2pcap "<<quoteWin(p.etlPath)<<" --out "<<quoteWin(p.path);int rc=runHidden(c.str());runHidden(quoteWin(p.tool)+" filter remove");std::error_code ec;std::filesystem::remove(std::filesystem::u8path(p.etlPath),ec);if(rc!=0)logger_.error("pktmon stopped but PCAPNG conversion failed for "+p.path);else logger_.info("Windows PCAPNG capture saved: "+p.path);p=Proc{};return;}if(p.process){GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT,p.pid);if(WaitForSingleObject(p.process,1500)==WAIT_TIMEOUT)TerminateProcess(p.process,0);CloseHandle(p.process);}p=Proc{};
#else
    if(p.pid>0){::kill(p.pid,SIGINT);for(int i=0;i<15;++i){int st=0;auto w=waitpid(p.pid,&st,WNOHANG);if(w==p.pid){p.pid=-1;break;}std::this_thread::sleep_for(std::chrono::milliseconds(100));}if(p.pid>0){::kill(p.pid,SIGTERM);std::this_thread::sleep_for(std::chrono::milliseconds(250));int st=0;if(waitpid(p.pid,&st,WNOHANG)==0){::kill(p.pid,SIGKILL);waitpid(p.pid,&st,0);}}}
    std::error_code ec;if(!p.errorPath.empty())std::filesystem::remove(p.errorPath,ec);p=Proc{};
#endif
}
void CaptureManager::startSip(const std::string&path,unsigned port,const std::string&iface){if(port==0)throw std::runtime_error("Invalid local SIP port");start(sip_,path,"((udp or tcp) and port "+std::to_string(port)+")",iface);}
std::string CaptureManager::rtpFilter(const CallSnapshot&c){std::set<int>ports;for(auto*a:{&c.localRtpAddress,&c.localRtcpAddress,&c.remoteRtpAddress,&c.remoteRtcpAddress,&c.sourceRtpAddress,&c.sourceRtcpAddress}){auto p=portFromAddress(*a);if(p)ports.insert(p);}if(ports.empty())throw std::runtime_error("RTP endpoints are not negotiated yet; place/answer the call first.");std::ostringstream f;f<<"udp and (";bool first=true;for(int p:ports){if(!first)f<<" or ";f<<"port "<<p;first=false;}f<<")";return f.str();}
void CaptureManager::startRtp(const std::string&path,const CallSnapshot&c,const std::string&iface){start(rtp_,path,rtpFilter(c),iface);}
void CaptureManager::startCall(const std::string&path,unsigned port,const CallSnapshot&c,const std::string&iface){if(port==0)throw std::runtime_error("Invalid local SIP port");const auto media=rtpFilter(c);start(call_,path,"(((udp or tcp) and port "+std::to_string(port)+") or ("+media+"))",iface);}
void CaptureManager::stop(CaptureKind k){if(k==CaptureKind::Sip)stopProc(sip_);else if(k==CaptureKind::Rtp)stopProc(rtp_);else stopProc(call_);}
void CaptureManager::stopAll(){stopProc(sip_);stopProc(rtp_);stopProc(call_);}
bool CaptureManager::active(CaptureKind k)const{return k==CaptureKind::Sip?sip_.running:(k==CaptureKind::Rtp?rtp_.running:call_.running);}
std::string CaptureManager::status()const{std::ostringstream s;s<<"SIP PCAP: "<<(sip_.running?sip_.path:"stopped")<<" | RTP PCAP: "<<(rtp_.running?rtp_.path:"stopped")<<" | CALL PCAP: "<<(call_.running?call_.path:"stopped");return s.str();}
}
