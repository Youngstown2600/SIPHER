#include "trunkmonkey/PbxAudit.h"
#include "trunkmonkey/RuntimePaths.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <spawn.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace trunkmonkey {
namespace {

std::string lower(std::string s){for(char&c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;}
std::string trim(std::string s){while(!s.empty()&&std::isspace(static_cast<unsigned char>(s.front())))s.erase(s.begin());while(!s.empty()&&std::isspace(static_cast<unsigned char>(s.back())))s.pop_back();return s;}

bool isPrivateIpv4(const std::string& text)
{
    unsigned a=0,b=0,c=0,d=0;char tail=0;
    if(std::sscanf(text.c_str(),"%u.%u.%u.%u%c",&a,&b,&c,&d,&tail)!=4||a>255||b>255||c>255||d>255)return false;
    return a==10 || (a==172&&b>=16&&b<=31) || (a==192&&b==168);
}

std::vector<std::string> privateIpv4Addresses(const std::string& text)
{
    static const std::regex ip(R"((?:^|[^0-9])((?:[0-9]{1,3}\.){3}[0-9]{1,3})(?=$|[^0-9]))");
    std::vector<std::string> out;
    for(std::sregex_iterator it(text.begin(),text.end(),ip),end;it!=end;++it){
        const auto value=(*it)[1].str();
        if(isPrivateIpv4(value)&&std::find(out.begin(),out.end(),value)==out.end()){out.push_back(value);if(out.size()>=5)break;}
    }
    return out;
}

bool looksLikeIpv4Literal(const std::string& host)
{
    unsigned a=0,b=0,c=0,d=0;char tail=0;
    return std::sscanf(host.c_str(),"%u.%u.%u.%u%c",&a,&b,&c,&d,&tail)==4&&a<=255&&b<=255&&c<=255&&d<=255;
}



std::string urlEncode(const std::string& input)
{
    static constexpr char hex[]="0123456789ABCDEF";
    std::string out;out.reserve(input.size()*3);
    for(unsigned char c:input){
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out.push_back(static_cast<char>(c));
        else{out.push_back('%');out.push_back(hex[(c>>4)&0xf]);out.push_back(hex[c&0xf]);}
    }
    return out;
}

std::string jsonUnescape(std::string value)
{
    std::string out;out.reserve(value.size());
    for(std::size_t i=0;i<value.size();++i){
        if(value[i]!='\\' || i+1>=value.size()){out.push_back(value[i]);continue;}
        const char n=value[++i];
        if(n=='n')out.push_back(' ');else if(n=='r'){}else if(n=='t')out.push_back(' ');else if(n=='"')out.push_back('"');else if(n=='\\')out.push_back('\\');else{out.push_back('\\');out.push_back(n);}
    }
    return out;
}

std::string jsonStringAfter(const std::string& text,const std::string& key,std::size_t start,std::size_t limit=std::string::npos)
{
    const auto p=text.find(key,start);if(p==std::string::npos || (limit!=std::string::npos&&p>=limit))return{};
    std::size_t i=p+key.size();std::string raw;
    bool escaped=false;
    for(;i<text.size() && (limit==std::string::npos||i<limit);++i){const char c=text[i];if(!escaped&&c=='"')break;raw.push_back(c);if(c=='\\'&&!escaped)escaped=true;else escaped=false;}
    return jsonUnescape(raw);
}

std::vector<std::string> csvFields(const std::string& line)
{
    std::vector<std::string> fields;std::string cur;bool quoted=false;
    for(std::size_t i=0;i<line.size();++i){const char c=line[i];if(c=='"'){if(quoted&&i+1<line.size()&&line[i+1]=='"'){cur.push_back('"');++i;}else quoted=!quoted;}else if(c==','&&!quoted){fields.push_back(cur);cur.clear();}else cur.push_back(c);}fields.push_back(cur);return fields;
}

std::string runProgramCapture(const std::vector<std::string>& args,unsigned timeoutMs,int* exitCode=nullptr)
{
    if(args.empty())return{};
#ifdef _WIN32
    auto quote=[](const std::string& arg){
        if(arg.find_first_of(" \t\"")==std::string::npos)return arg;
        std::string out="\"";unsigned slashes=0;
        for(char c:arg){
            if(c=='\\'){++slashes;continue;}
            if(c=='\"'){out.append(slashes*2+1,'\\');out.push_back('\"');slashes=0;continue;}
            out.append(slashes,'\\');slashes=0;out.push_back(c);
        }
        out.append(slashes*2,'\\');out.push_back('\"');return out;
    };
    std::string command;
    for(const auto& a:args){if(!command.empty())command.push_back(' ');command+=quote(a);}
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE readPipe=nullptr,writePipe=nullptr;
    if(!CreatePipe(&readPipe,&writePipe,&sa,0))throw std::runtime_error("CreatePipe failed: "+std::to_string(GetLastError()));
    SetHandleInformation(readPipe,HANDLE_FLAG_INHERIT,0);
    STARTUPINFOA si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);si.hStdOutput=writePipe;si.hStdError=writePipe;
    PROCESS_INFORMATION pi{};std::vector<char> cmd(command.begin(),command.end());cmd.push_back('\0');
    if(!CreateProcessA(nullptr,cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){
        const auto err=GetLastError();CloseHandle(readPipe);CloseHandle(writePipe);if(exitCode)*exitCode=static_cast<int>(err);return{};
    }
    CloseHandle(writePipe);std::string out;std::array<char,4096> b{};const auto start=std::chrono::steady_clock::now();DWORD processCode=STILL_ACTIVE;
    for(;;){
        DWORD avail=0;if(PeekNamedPipe(readPipe,nullptr,0,nullptr,&avail,nullptr)&&avail){DWORD got=0;const DWORD want=static_cast<DWORD>(std::min<std::size_t>(b.size(),avail));if(ReadFile(readPipe,b.data(),want,&got,nullptr)&&got)out.append(b.data(),got);}
        if(GetExitCodeProcess(pi.hProcess,&processCode)&&processCode!=STILL_ACTIVE)break;
        if(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()>timeoutMs){TerminateProcess(pi.hProcess,124);WaitForSingleObject(pi.hProcess,1000);processCode=124;break;}
        Sleep(20);
    }
    for(;;){DWORD got=0;if(!ReadFile(readPipe,b.data(),static_cast<DWORD>(b.size()),&got,nullptr)||!got)break;out.append(b.data(),got);}
    CloseHandle(readPipe);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);if(exitCode)*exitCode=static_cast<int>(processCode);return out;
#else
    int pipefd[2];if(pipe(pipefd)!=0)throw std::runtime_error("pipe() failed: "+std::string(std::strerror(errno)));
    posix_spawn_file_actions_t a;posix_spawn_file_actions_init(&a);posix_spawn_file_actions_addopen(&a,STDIN_FILENO,"/dev/null",O_RDONLY,0);posix_spawn_file_actions_adddup2(&a,pipefd[1],STDOUT_FILENO);posix_spawn_file_actions_adddup2(&a,pipefd[1],STDERR_FILENO);posix_spawn_file_actions_addclose(&a,pipefd[0]);
    std::vector<std::string> local=args;std::vector<char*> argv;for(auto&x:local)argv.push_back(x.data());argv.push_back(nullptr);
    pid_t pid=-1;const int rc=posix_spawnp(&pid,local[0].c_str(),&a,nullptr,argv.data(),environ);posix_spawn_file_actions_destroy(&a);close(pipefd[1]);
    if(rc!=0){close(pipefd[0]);if(exitCode)*exitCode=rc;return{};}
    int flags=fcntl(pipefd[0],F_GETFL,0);if(flags>=0)(void)fcntl(pipefd[0],F_SETFL,flags|O_NONBLOCK);
    std::string out;std::array<char,4096>b{};const auto start=std::chrono::steady_clock::now();int status=0;bool done=false;
    while(!done){for(;;){const auto n=read(pipefd[0],b.data(),b.size());if(n>0)out.append(b.data(),static_cast<std::size_t>(n));else break;}const auto w=waitpid(pid,&status,WNOHANG);if(w==pid)done=true;else if(w<0)done=true;else if(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()>timeoutMs){kill(pid,SIGTERM);std::this_thread::sleep_for(std::chrono::milliseconds(50));kill(pid,SIGKILL);waitpid(pid,&status,0);status=124<<8;done=true;}else std::this_thread::sleep_for(std::chrono::milliseconds(25));}
    for(;;){const auto n=read(pipefd[0],b.data(),b.size());if(n>0)out.append(b.data(),static_cast<std::size_t>(n));else break;}close(pipefd[0]);if(exitCode)*exitCode=(WIFEXITED(status)?WEXITSTATUS(status):128);return out;
#endif
}

std::string extractVersion(const std::string& banner,const std::string& needle)
{
    const auto lb=lower(banner),ln=lower(needle);auto p=lb.find(ln);if(p==std::string::npos)p=0;else p+=ln.size();
    while(p<banner.size()&&!std::isdigit(static_cast<unsigned char>(banner[p])))++p;
    if(p>=banner.size())return{};
    std::string v;for(;p<banner.size();++p){const char c=banner[p];if(std::isalnum(static_cast<unsigned char>(c))||c=='.'||c=='-'||c=='_'||c=='+')v.push_back(c);else break;}return v;
}

std::vector<std::string> splitTokens(const std::string& text)
{
    std::vector<std::string> out;std::string cur;for(char c:text){if(c==','||std::isspace(static_cast<unsigned char>(c))){cur=trim(cur);if(!cur.empty()){out.push_back(cur);cur.clear();}}else cur.push_back(c);}cur=trim(cur);if(!cur.empty())out.push_back(cur);return out;
}

std::string token(std::size_t n=12)
{
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static constexpr char alphabet[]="abcdefghijklmnopqrstuvwxyz0123456789";
    std::string out;out.reserve(n);
    for(std::size_t i=0;i<n;++i)out.push_back(alphabet[rng()%(sizeof(alphabet)-1)]);
    return out;
}

std::map<std::string,std::string> headers(const std::string& raw)
{
    std::map<std::string,std::string> out;
    std::istringstream in(raw);std::string line;
    std::getline(in,line);
    while(std::getline(in,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty())break;
        const auto p=line.find(':');if(p==std::string::npos)continue;
        auto key=lower(trim(line.substr(0,p)));auto val=trim(line.substr(p+1));
        if(auto it=out.find(key);it!=out.end())it->second+=", "+val;else out.emplace(std::move(key),std::move(val));
    }
    return out;
}

std::string authParam(const std::string& challenge,const std::string& key)
{
    const auto wanted=lower(key);
    std::size_t pos=0;
    while(pos<challenge.size()){
        while(pos<challenge.size()&&(std::isspace(static_cast<unsigned char>(challenge[pos]))||challenge[pos]==','))++pos;
        const auto eq=challenge.find('=',pos);if(eq==std::string::npos)break;
        auto name=lower(trim(challenge.substr(pos,eq-pos)));
        std::size_t valueStart=eq+1;while(valueStart<challenge.size()&&std::isspace(static_cast<unsigned char>(challenge[valueStart])))++valueStart;
        std::string value;
        if(valueStart<challenge.size()&&challenge[valueStart]=='"'){
            const auto end=challenge.find('"',valueStart+1);
            value=challenge.substr(valueStart+1,end==std::string::npos?std::string::npos:end-valueStart-1);
            pos=end==std::string::npos?challenge.size():end+1;
        }else{
            const auto end=challenge.find(',',valueStart);
            value=trim(challenge.substr(valueStart,end==std::string::npos?std::string::npos:end-valueStart));
            pos=end==std::string::npos?challenge.size():end+1;
        }
        if(name==wanted)return value;
    }
    return {};
}

void parseStatus(AuditResponse&r)
{
    std::istringstream in(r.rawResponse);std::string first;std::getline(in,first);if(!first.empty()&&first.back()=='\r')first.pop_back();
    std::istringstream ls(first);std::string proto;ls>>proto>>r.statusCode;std::getline(ls,r.reason);r.reason=trim(r.reason);
    const auto h=headers(r.rawResponse);
    auto get=[&](const char*k){auto it=h.find(k);return it==h.end()?std::string{}:it->second;};
    r.server=get("server");r.userAgent=get("user-agent");r.allow=get("allow");r.supported=get("supported");
    r.authenticate=get("www-authenticate");if(r.authenticate.empty())r.authenticate=get("proxy-authenticate");
}

std::string sipRequest(const std::string& method,const std::string& uri,const std::string& host,AuditTransport transport,
                       const std::string& toUser={},const std::vector<std::string>& extra={})
{
    const auto branch="z9hG4bK-tm-"+token();const auto tag="tm-"+token(8);const auto cid=token(16)+"@sipher";
    std::ostringstream s;
    s<<method<<" "<<uri<<" SIP/2.0\r\n"
     <<"Via: SIP/2.0/"<<(transport==AuditTransport::Udp?"UDP":"TCP")<<" 0.0.0.0:5060;branch="<<branch<<";rport\r\n"
     <<"Max-Forwards: 70\r\n"
     <<"From: \"S.I.P.H.E.R. Audit\" <sip:sipher-audit@"<<host<<">;tag="<<tag<<"\r\n"
     <<"To: <sip:"<<(toUser.empty()?host:toUser+"@"+host)<<">\r\n"
     <<"Call-ID: "<<cid<<"\r\n"
     <<"CSeq: 1 "<<method<<"\r\n"
     <<"Contact: <sip:sipher-audit@127.0.0.1>\r\n"
     <<"User-Agent: S.I.P.H.E.R./1.0.0\r\n";
    for(const auto&x:extra)s<<x<<"\r\n";
    s<<"Content-Length: 0\r\n\r\n";
    return s.str();
}

#ifdef _WIN32
struct WsaScope{
    WsaScope(){WSADATA data{};const int rc=WSAStartup(MAKEWORD(2,2),&data);if(rc!=0)throw std::runtime_error("WSAStartup failed: "+std::to_string(rc));}
    ~WsaScope(){WSACleanup();}
};
using SocketHandle=SOCKET;
constexpr SocketHandle invalidSocket=INVALID_SOCKET;
void closeSocket(SocketHandle s){if(s!=INVALID_SOCKET)closesocket(s);}
std::string socketError(){return "Winsock error "+std::to_string(WSAGetLastError());}
#else
using SocketHandle=int;
constexpr SocketHandle invalidSocket=-1;
void closeSocket(SocketHandle s){if(s>=0)::close(s);}
std::string socketError(){return std::strerror(errno);}
#endif

struct AddrList{addrinfo*p{nullptr};~AddrList(){if(p)freeaddrinfo(p);}};

AddrList resolve(const std::string&host,std::uint16_t port,int socktype)
{
#ifdef _WIN32
    WsaScope wsa;
#endif
    addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=socktype;hints.ai_protocol=(socktype==SOCK_DGRAM?IPPROTO_UDP:IPPROTO_TCP);
    AddrList out;const auto ps=std::to_string(port);const int rc=getaddrinfo(host.c_str(),ps.c_str(),&hints,&out.p);
    if(rc!=0)throw std::runtime_error("Unable to resolve "+host+" (getaddrinfo "+std::to_string(rc)+")");
    return out;
}

bool connectTimed(SocketHandle fd,const sockaddr*sa,socklen_t len,unsigned timeoutMs)
{
#ifdef _WIN32
    u_long nonBlocking=1;if(ioctlsocket(fd,FIONBIO,&nonBlocking)!=0)return false;
    int rc=::connect(fd,sa,len);if(rc==0){nonBlocking=0;ioctlsocket(fd,FIONBIO,&nonBlocking);return true;}
    if(WSAGetLastError()!=WSAEWOULDBLOCK){nonBlocking=0;ioctlsocket(fd,FIONBIO,&nonBlocking);return false;}
    fd_set wf;FD_ZERO(&wf);FD_SET(fd,&wf);timeval tv{static_cast<long>(timeoutMs/1000),static_cast<long>((timeoutMs%1000)*1000)};
    rc=select(0,nullptr,&wf,nullptr,&tv);if(rc<=0){nonBlocking=0;ioctlsocket(fd,FIONBIO,&nonBlocking);return false;}int err=0;int el=sizeof(err);getsockopt(fd,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&err),&el);nonBlocking=0;ioctlsocket(fd,FIONBIO,&nonBlocking);return err==0;
#else
    const int old=fcntl(fd,F_GETFL,0);if(old<0)return false;if(fcntl(fd,F_SETFL,old|O_NONBLOCK)<0)return false;
    int rc=::connect(fd,sa,len);if(rc==0){fcntl(fd,F_SETFL,old);return true;}if(errno!=EINPROGRESS){fcntl(fd,F_SETFL,old);return false;}
    fd_set wf;FD_ZERO(&wf);FD_SET(fd,&wf);timeval tv{static_cast<long>(timeoutMs/1000),static_cast<long>((timeoutMs%1000)*1000)};
    rc=select(fd+1,nullptr,&wf,nullptr,&tv);if(rc<=0){fcntl(fd,F_SETFL,old);return false;}int err=0;socklen_t el=sizeof(err);getsockopt(fd,SOL_SOCKET,SO_ERROR,&err,&el);fcntl(fd,F_SETFL,old);return err==0;
#endif
}

std::string transact(const std::string&host,std::uint16_t port,AuditTransport transport,const std::string&request,unsigned timeoutMs,double&latency)
{
#ifdef _WIN32
    WsaScope wsa;
#endif
    const int st=transport==AuditTransport::Udp?SOCK_DGRAM:SOCK_STREAM;auto addrs=resolve(host,port,st);std::string lastError="No usable address";
    for(auto*ai=addrs.p;ai;ai=ai->ai_next){
        SocketHandle fd=::socket(ai->ai_family,ai->ai_socktype,ai->ai_protocol);if(fd==invalidSocket){lastError=socketError();continue;}
#ifdef _WIN32
        DWORD timeout=timeoutMs;setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
#else
        timeval tv{static_cast<long>(timeoutMs/1000),static_cast<long>((timeoutMs%1000)*1000)};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
#endif
        const auto started=std::chrono::steady_clock::now();int sent=-1;
        if(transport==AuditTransport::Udp) sent=static_cast<int>(sendto(fd,request.data(),static_cast<int>(request.size()),0,ai->ai_addr,static_cast<socklen_t>(ai->ai_addrlen)));
        else if(connectTimed(fd,ai->ai_addr,static_cast<socklen_t>(ai->ai_addrlen),timeoutMs)) sent=send(fd,request.data(),static_cast<int>(request.size()),0);
        if(sent<0){lastError=socketError();closeSocket(fd);continue;}
        std::array<char,65536>buf{};std::string raw;
        for(;;){const int n=recv(fd,buf.data(),static_cast<int>(buf.size()),0);if(n>0){raw.append(buf.data(),static_cast<std::size_t>(n));if(raw.find("\r\n\r\n")!=std::string::npos||transport==AuditTransport::Udp)break;continue;}break;}
        latency=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();closeSocket(fd);
        if(!raw.empty()) return raw;
        lastError="No SIP response before timeout";
    }
    throw std::runtime_error(lastError);
}

AuditResponse run(const std::string&name,const std::string&host,std::uint16_t port,AuditTransport transport,const std::string&req,unsigned timeoutMs)
{
    AuditResponse r;r.target=host;r.port=port;r.transport=transport;r.testName=name;r.rawRequest=req;r.requestBytes=req.size();
    try{r.rawResponse=transact(host,port,transport,req,timeoutMs,r.latencyMs);r.responseBytes=r.rawResponse.size();parseStatus(r);}catch(const std::exception&e){r.reason=e.what();r.findings.push_back({"INFO","No SIP response",e.what()});return r;}
    return r;
}

void commonFindings(AuditResponse&r)
{
    if(!r.server.empty()||!r.userAgent.empty())r.findings.push_back({"INFO","Banner disclosed",!r.server.empty()?"Server: "+r.server:"User-Agent: "+r.userAgent});
    const auto la=lower(r.allow);
    if(la.find("register")!=std::string::npos)r.findings.push_back({"INFO","REGISTER advertised","Target advertises REGISTER support."});
    if(la.find("refer")!=std::string::npos)r.findings.push_back({"INFO","REFER advertised","Target advertises REFER; verify transfer policy and authorization."});
    if(la.find("message")!=std::string::npos)r.findings.push_back({"INFO","MESSAGE advertised","Target advertises SIP MESSAGE; verify policy if not required."});
    if(r.transport==AuditTransport::Udp&&r.requestBytes&&r.responseBytes>r.requestBytes*3)r.findings.push_back({"WARN","Large UDP response ratio","Response is more than 3x the request size; review exposure to spoofed-source amplification."});
}

std::string extensionAssessment(int code)
{
    if(code==200)return "Endpoint responded positively; extension may be provisioned or routable.";
    if(code==401||code==407)return "Authentication challenge; extension/account may be recognized or policy is globally challenged.";
    if(code==403)return "Forbidden; compare with known-invalid extensions for differential behavior.";
    if(code==404)return "Not found.";
    if(code==480||code==486)return "Temporarily unavailable/busy; extension is likely routable.";
    if(code==0)return "No response.";
    return "Response differs from a clean not-found result; compare against a known-invalid control.";
}

std::vector<std::string> cidrHosts(const std::string& cidr)
{
    const auto slash=cidr.find('/');
    if(slash==std::string::npos) throw std::runtime_error("audit discovery requires IPv4 CIDR notation, for example 192.0.2.0/28");
    const auto ip=cidr.substr(0,slash);
    const auto prefixText=cidr.substr(slash+1);
    char* end=nullptr;const long prefix=std::strtol(prefixText.c_str(),&end,10);
    if(end==prefixText.c_str()||*end!='\0'||prefix<27||prefix>32)
        throw std::runtime_error("audit discovery is limited to /27 through /32 (maximum 32 addresses per run)");
#ifdef _WIN32
    WsaScope wsa;
#endif
    in_addr a{};if(inet_pton(AF_INET,ip.c_str(),&a)!=1)throw std::runtime_error("audit discovery currently supports IPv4 CIDR only");
    const std::uint32_t host=ntohl(a.s_addr);
    const std::uint32_t mask=prefix==0?0u:(0xffffffffu<<(32-prefix));
    const std::uint32_t network=host&mask;
    const std::uint32_t count=1u<<(32-prefix);
    std::vector<std::string> out;out.reserve(count);
    for(std::uint32_t i=0;i<count;++i){
        // For ordinary subnets, do not probe the network/broadcast addresses.
        if(prefix<=30 && (i==0 || i==count-1)) continue;
        in_addr x{};x.s_addr=htonl(network+i);char text[INET_ADDRSTRLEN]{};
        if(inet_ntop(AF_INET,&x,text,sizeof(text)))out.emplace_back(text);
    }
    return out;
}

} // namespace

AuditTransport PbxAudit::transportFromString(const std::string&value){const auto v=lower(value);if(v=="udp")return AuditTransport::Udp;if(v=="tcp")return AuditTransport::Tcp;throw std::runtime_error("transport must be udp or tcp");}
std::string PbxAudit::transportName(AuditTransport v){return v==AuditTransport::Udp?"UDP":"TCP";}

AuditResponse PbxAudit::serviceProbe(const std::string&host,std::uint16_t port,AuditTransport transport,unsigned timeoutMs)
{
    auto r=run("SIP service / capability probe",host,port,transport,sipRequest("OPTIONS","sip:"+host,host,transport),timeoutMs);commonFindings(r);
    if(r.statusCode>=200&&r.statusCode<500)r.findings.push_back({"INFO","SIP service reachable","A SIP response was received in "+std::to_string(static_cast<unsigned>(r.latencyMs))+" ms."});
    if(!r.allow.empty())r.findings.push_back({"INFO","Advertised methods",r.allow});
    if(!r.supported.empty())r.findings.push_back({"INFO","Advertised extensions",r.supported});
    return r;
}

std::vector<DiscoveryEntry> PbxAudit::discoverIpv4Cidr(const std::string&cidr,std::uint16_t port,AuditTransport transport,unsigned delayMs,unsigned timeoutMs)
{
    if(delayMs<100)delayMs=100;
    const auto hosts=cidrHosts(cidr);std::vector<DiscoveryEntry> out;
    for(std::size_t i=0;i<hosts.size();++i){
        auto r=serviceProbe(hosts[i],port,transport,timeoutMs);
        if(r.statusCode!=0)out.push_back({hosts[i],port,transport,r.statusCode,r.reason,r.server.empty()?r.userAgent:r.server,r.latencyMs});
        if(i+1<hosts.size())std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
    return out;
}

std::vector<AuditResponse> PbxAudit::methodAudit(const std::string&host,std::uint16_t port,AuditTransport transport,unsigned timeoutMs)
{
    std::vector<AuditResponse> out;
    auto add=[&](const std::string&name,const std::string&req){auto r=run(name,host,port,transport,req,timeoutMs);commonFindings(r);out.push_back(std::move(r));};
    add("OPTIONS policy",sipRequest("OPTIONS","sip:"+host,host,transport));
    add("REGISTER policy",sipRequest("REGISTER","sip:"+host,host,transport,"sipher-audit",{"Expires: 0"}));
    add("SUBSCRIBE policy",sipRequest("SUBSCRIBE","sip:"+host,host,transport,"sipher-audit",{"Event: presence","Expires: 0"}));
    add("MESSAGE policy",sipRequest("MESSAGE","sip:"+host,host,transport,"sipher-audit",{"Content-Type: text/plain"}));
    for(auto&r:out){
        if((r.testName=="REGISTER policy"||r.testName=="SUBSCRIBE policy")&&r.statusCode>=200&&r.statusCode<300)
            r.findings.push_back({"WARN","Unauthenticated state-changing method accepted",r.testName+" returned 2xx without Authorization. The probe used Expires: 0 to avoid a persistent binding/subscription; review policy."});
        if(r.testName=="MESSAGE policy"&&r.statusCode>=200&&r.statusCode<300)
            r.findings.push_back({"WARN","Unauthenticated MESSAGE accepted","Target returned 2xx to an empty unauthenticated SIP MESSAGE. Review whether MESSAGE is required and authenticated."});
        if(r.statusCode==405)r.findings.push_back({"PASS","Method rejected","Target explicitly rejected this method with 405 Method Not Allowed."});
    }
    return out;
}

AuditResponse PbxAudit::authenticationAudit(const std::string&host,const std::string&username,std::uint16_t port,AuditTransport transport,unsigned timeoutMs)
{
    const auto user=username.empty()?"sipher-audit":username;
    auto req=sipRequest("REGISTER","sip:"+host,host,transport,user,{"Expires: 0"});
    auto r=run("Authentication / registration policy",host,port,transport,req,timeoutMs);commonFindings(r);
    if(r.statusCode>=200&&r.statusCode<300){
        r.findings.push_back({"HIGH","Unauthenticated REGISTER accepted","A REGISTER without Authorization received a 2xx response. Expires: 0 was used to avoid creating a persistent binding; review registration authentication policy immediately."});
    }else if(r.statusCode==401||r.statusCode==407){
        r.findings.push_back({"PASS","Authentication challenge present","Target challenged unauthenticated registration."});
        const auto a=lower(r.authenticate);
        if(a.find("basic")!=std::string::npos)r.findings.push_back({"HIGH","Basic authentication advertised","SIP authentication challenge appears to use Basic rather than Digest."});
        if(a.find("algorithm=md5")!=std::string::npos||a.find("algorithm=\"md5\"")!=std::string::npos)r.findings.push_back({"WARN","Digest MD5 in use","Digest challenge advertises MD5; prefer SHA-256-class SIP Digest algorithms when endpoint support allows."});
        else if(a.find("algorithm=")==std::string::npos&&a.find("digest")!=std::string::npos)r.findings.push_back({"WARN","Digest algorithm not declared","The Digest challenge omits an algorithm token. Verify the PBX is not relying on the legacy MD5 default and prefer an explicitly configured stronger algorithm where supported."});
        else if(a.find("sha-256")!=std::string::npos||a.find("sha-512-256")!=std::string::npos)r.findings.push_back({"PASS","Stronger Digest algorithm advertised","The authentication challenge advertises a SHA-2-class SIP Digest algorithm."});
        if(a.find("qop=")==std::string::npos)r.findings.push_back({"WARN","Digest qop missing","Digest challenge does not advertise qop; review authentication hardening."});
        if(a.find("realm=")==std::string::npos)r.findings.push_back({"WARN","Digest realm missing","Authentication challenge did not expose a realm parameter."});
    }else if(r.statusCode==403){r.findings.push_back({"INFO","Registration forbidden","Unauthenticated registration was rejected with 403."});}
    if(!r.authenticate.empty())r.findings.push_back({"INFO","Authentication challenge",r.authenticate});
    return r;
}

std::vector<AuditResponse> PbxAudit::digestOracleAudit(const std::string&host,const std::string&username,std::uint16_t port,AuditTransport transport,unsigned timeoutMs)
{
    const auto user=username.empty()?"sipher-audit":username;
    const auto invalid="tm-invalid-"+token(10);
    std::vector<AuditResponse> out;out.reserve(3);
    out.push_back(authenticationAudit(host,user,port,transport,timeoutMs));
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    out.push_back(authenticationAudit(host,user,port,transport,timeoutMs));
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    out.push_back(authenticationAudit(host,invalid,port,transport,timeoutMs));

    const auto n1=authParam(out[0].authenticate,"nonce");
    const auto n2=authParam(out[1].authenticate,"nonce");
    if(!n1.empty()&&!n2.empty()){
        if(n1==n2)out[1].findings.push_back({"WARN","Digest nonce reused","Two separate unauthenticated challenges returned the same nonce. This can be legitimate for a short-lived server nonce, but review nonce lifetime/replay protections."});
        else out[1].findings.push_back({"PASS","Digest nonce changed","Repeated unauthenticated challenges returned different nonce values."});
    }
    if(out[0].statusCode!=out[2].statusCode){
        out[2].findings.push_back({"WARN","Account-response oracle","The supplied test account and a random-invalid control received different SIP status codes ("+std::to_string(out[0].statusCode)+" vs "+std::to_string(out[2].statusCode)+"). This may permit account enumeration."});
    }else if(out[0].statusCode){
        out[2].findings.push_back({"PASS","Consistent account response","The supplied test account and random-invalid control received the same SIP status code."});
    }
    return out;
}

std::vector<ExtensionAuditEntry> PbxAudit::extensionAudit(const std::string&host,unsigned first,unsigned last,std::uint16_t port,AuditTransport transport,unsigned delayMs,unsigned timeoutMs)
{
    if(first>last)throw std::runtime_error("extension range start must be <= end");
    if(last-first+1>100)throw std::runtime_error("extension audit is capped at 100 entries per run");
    if(delayMs<100)delayMs=100;
    std::vector<ExtensionAuditEntry> out;out.reserve(last-first+1);
    for(unsigned n=first;n<=last;++n){
        const auto ext=std::to_string(n);const auto req=sipRequest("OPTIONS","sip:"+ext+"@"+host,host,transport,ext);auto r=run("Extension differential probe",host,port,transport,req,timeoutMs);
        out.push_back({ext,r.statusCode,r.reason,r.latencyMs,extensionAssessment(r.statusCode)});
        if(n!=last)std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
    return out;
}

std::vector<AuditResponse> PbxAudit::complianceAudit(const std::string&host,std::uint16_t port,AuditTransport transport,unsigned timeoutMs)
{
    std::vector<AuditResponse> out;
    auto add=[&](std::string name,std::string req){auto r=run(name,host,port,transport,req,timeoutMs);commonFindings(r);out.push_back(std::move(r));};
    add("Unknown method handling",sipRequest("SIPHER","sip:"+host,host,transport));
    auto exhausted=sipRequest("OPTIONS","sip:"+host,host,transport);
    if(const auto pos=exhausted.find("Max-Forwards: 70");pos!=std::string::npos)exhausted.replace(pos,std::strlen("Max-Forwards: 70"),"Max-Forwards: 0");
    add("Max-Forwards exhaustion",std::move(exhausted));
    add("Unsupported required option",sipRequest("OPTIONS","sip:"+host,host,transport,{}, {"Require: sipher-audit-feature"}));
    add("REGISTER policy control",sipRequest("REGISTER","sip:"+host,host,transport,"sipher-audit", {"Expires: 0"}));
    for(auto&r:out){
        if(r.testName=="Unknown method handling"&&r.statusCode>=200&&r.statusCode<300)r.findings.push_back({"WARN","Unknown SIP method accepted","Target returned success to an intentionally unknown SIP method."});
        if(r.testName=="Max-Forwards exhaustion"&&r.statusCode>=200&&r.statusCode<300)r.findings.push_back({"WARN","Max-Forwards: 0 accepted","Target did not reject a request whose Max-Forwards value was exhausted."});
        if(r.testName=="Unsupported required option"&&r.statusCode>=200&&r.statusCode<300)r.findings.push_back({"WARN","Unknown Require option accepted","Target returned success despite an unsupported required option tag."});
    }
    return out;
}

std::vector<AuditResponse> PbxAudit::parserAbuseAudit(const std::string&host,std::uint16_t port,AuditTransport transport,unsigned timeoutMs)
{
    std::vector<AuditResponse> out;out.reserve(5);
    auto add=[&](const std::string&name,std::string req){auto r=run(name,host,port,transport,req,timeoutMs);commonFindings(r);out.push_back(std::move(r));};

    auto cseq=sipRequest("OPTIONS","sip:"+host,host,transport);
    if(const auto p=cseq.find("CSeq: 1 OPTIONS");p!=std::string::npos)cseq.replace(p,std::strlen("CSeq: 1 OPTIONS"),"CSeq: 1 REGISTER");
    add("Parser edge: CSeq/request mismatch",std::move(cseq));

    auto branch=sipRequest("OPTIONS","sip:"+host,host,transport);
    if(const auto p=branch.find("branch=z9hG4bK-");p!=std::string::npos)branch.replace(p,std::strlen("branch=z9hG4bK-"),"branch=tm-");
    add("Parser edge: non-RFC Via branch",std::move(branch));

    auto duplicate=sipRequest("OPTIONS","sip:"+host,host,transport);
    if(const auto p=duplicate.rfind("Content-Length: 0");p!=std::string::npos)duplicate.insert(p,"Content-Length: 0\r\n");
    add("Parser edge: duplicate Content-Length",std::move(duplicate));

    add("Parser edge: unknown URI scheme",sipRequest("OPTIONS","sipher-audit://"+host,host,transport));
    add("Parser edge: long benign header",sipRequest("OPTIONS","sip:"+host,host,transport,{}, {"X-SIPHER-Fill: "+std::string(512,'A')}));

    for(auto&r:out){
        if(r.statusCode>=200&&r.statusCode<300)
            r.findings.push_back({"WARN","Parser edge accepted","Target returned success to a deliberately unusual SIP request. Review parser normalization, proxy policy, and downstream handling."});
        else if(r.statusCode>=400&&r.statusCode<700)
            r.findings.push_back({"PASS","Parser edge rejected","Target rejected the deliberately unusual request without requiring a destructive payload."});
    }
    return out;
}

std::vector<AuditResponse> PbxAudit::resilienceAudit(const std::string&host,std::uint16_t port,AuditTransport transport,unsigned requests,unsigned delayMs,unsigned timeoutMs)
{
    if(requests<3)requests=3;
    if(requests>20)throw std::runtime_error("resilience audit is hard-capped at 20 requests per run");
    if(delayMs<100)delayMs=100;
    std::vector<AuditResponse> out;out.reserve(requests);
    unsigned responses=0;double firstLatency=0.0,lastLatency=0.0,sum=0.0;
    for(unsigned i=0;i<requests;++i){
        auto r=run("Rate resilience OPTIONS "+std::to_string(i+1)+"/"+std::to_string(requests),host,port,transport,sipRequest("OPTIONS","sip:"+host,host,transport),timeoutMs);
        commonFindings(r);if(r.statusCode){++responses;sum+=r.latencyMs;if(firstLatency==0.0)firstLatency=r.latencyMs;lastLatency=r.latencyMs;}out.push_back(std::move(r));
        if(i+1<requests)std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
    if(!out.empty()){
        const double ratio=100.0*static_cast<double>(responses)/static_cast<double>(requests);
        std::ostringstream detail;detail<<responses<<"/"<<requests<<" probes answered ("<<std::fixed<<std::setprecision(1)<<ratio<<"%) at approximately "<<(1000.0/delayMs)<<" requests/sec";
        if(responses)detail<<", average response latency "<<(sum/responses)<<" ms";
        const bool severe=responses<requests/2;
        out.back().findings.push_back({severe?"WARN":"INFO","Bounded rate-resilience summary",detail.str()+". This is a low-volume resilience check, not a denial-of-service test."});
        if(firstLatency>0.0&&lastLatency>firstLatency*3.0)out.back().findings.push_back({"INFO","Latency growth observed","Response latency increased materially during the bounded burst; this may indicate throttling or load protection."});
    }
    return out;
}

std::vector<AuditResponse> PbxAudit::attackScenarioAudit(const std::string&host,const std::string&username,std::uint16_t port,AuditTransport transport,unsigned timeoutMs)
{
    std::vector<AuditResponse> out;
    out.push_back(serviceProbe(host,port,transport,timeoutMs));
    auto methods=methodAudit(host,port,transport,timeoutMs);out.insert(out.end(),methods.begin(),methods.end());
    auto oracle=digestOracleAudit(host,username.empty()?"sipher-audit":username,port,transport,timeoutMs);out.insert(out.end(),oracle.begin(),oracle.end());
    auto parser=parserAbuseAudit(host,port,transport,timeoutMs);out.insert(out.end(),parser.begin(),parser.end());
    auto resilience=resilienceAudit(host,port,transport,10,150,std::min(timeoutMs,1200u));out.insert(out.end(),resilience.begin(),resilience.end());
    return out;
}


PbxFingerprint PbxAudit::fingerprintFromProbe(const AuditResponse& probe)
{
    PbxFingerprint fp;fp.host=probe.target;fp.serverBanner=probe.server;fp.userAgentBanner=probe.userAgent;
    const std::string banner=probe.server+" "+probe.userAgent;const auto lb=lower(banner);
    struct Sig{const char* needle;const char* vendor;const char* product;};
    static const Sig sigs[]={
        {"freepbx","Sangoma","FreePBX"},{"asterisk","Sangoma/Digium","Asterisk"},{"freeswitch","SignalWire","FreeSWITCH"},
        {"kamailio","Kamailio Project","Kamailio"},{"opensips","OpenSIPS Project","OpenSIPS"},{"3cx","3CX","3CX Phone System"},
        {"metaswitch","Microsoft/Metaswitch","Metaswitch"},{"perimeta","Microsoft/Metaswitch","Perimeta SBC"},{"audiocodes","AudioCodes","AudioCodes SBC"},
        {"acme packet","Oracle","Oracle/Acme Packet SBC"},{"oracle communications","Oracle","Oracle Communications SBC"},{"sonus","Ribbon","Sonus/Ribbon SBC"},
        {"ribbon","Ribbon","Ribbon SBC"},{"genband","Ribbon/GENBAND","GENBAND"},{"cisco","Cisco","Cisco SIP/Unified Communications"},
        {"avaya","Avaya","Avaya SIP"},{"grandstream","Grandstream","Grandstream UCM"}
    };
    for(const auto&sig:sigs){if(lb.find(sig.needle)!=std::string::npos){fp.vendor=sig.vendor;fp.product=sig.product;fp.version=extractVersion(banner,sig.needle);fp.confidence="high (banner match)";break;}}
    if(fp.product.empty()){fp.product="Unknown SIP/PBX platform";fp.confidence=probe.statusCode?"low (responsive SIP service, no recognized banner)":"unknown";}
    std::set<std::string> seen;
    for(const auto&t:splitTokens(probe.supported))if(seen.insert("supported:"+t).second){fp.capabilities.push_back("Supported: "+t);fp.components.push_back({"SIP extension "+t,"", "Supported header"});}
    for(const auto&t:splitTokens(probe.allow))if(seen.insert("allow:"+t).second)fp.capabilities.push_back("Method: "+t);
    if(!probe.server.empty())fp.components.insert(fp.components.begin(),{fp.product,fp.version,"Server: "+probe.server});
    else if(!probe.userAgent.empty())fp.components.insert(fp.components.begin(),{fp.product,fp.version,"User-Agent: "+probe.userAgent});
    fp.notes.push_back("Fingerprinting is best-effort and based on information remotely disclosed by SIP responses.");
    fp.notes.push_back("Authenticated PBX plugin/module inventories are not remotely enumerated; S.I.P.H.E.R. lists only banners and disclosed SIP capabilities/modules.");
    return fp;
}

PbxFingerprint PbxAudit::fingerprint(const std::string&host,std::uint16_t port,AuditTransport transport,unsigned timeoutMs)
{
    return fingerprintFromProbe(serviceProbe(host,port,transport,timeoutMs));
}

AutomatedAuditResult PbxAudit::automatedAudit(const AutomatedAuditOptions& options,const AuditProgressCallback& progress)
{
    if(options.host.empty())throw std::runtime_error("automated audit requires a target host");
    if(options.includeExtensionAudit){
        if(options.extensionFirst>options.extensionLast)throw std::runtime_error("extension range start must be <= end");
        if(options.extensionLast-options.extensionFirst+1>100)throw std::runtime_error("automated extension audit is capped at 100 entries");
    }

    AutomatedAuditResult result;result.options=options;
    const unsigned total=7u+(options.includeParserAudit?1u:0u)+(options.includeResilienceAudit?1u:0u)+(options.includeTlsAudit?1u:0u)+(options.includeExtensionAudit?1u:0u)+(options.includeVulnerabilityLookup?1u:0u);
    unsigned phase=0;
    auto step=[&](const std::string& label){++phase;if(progress)progress(phase,total,label);};
    auto append=[&](std::vector<AuditResponse> values){result.responses.insert(result.responses.end(),std::make_move_iterator(values.begin()),std::make_move_iterator(values.end()));};
    auto posture=[&](std::string severity,std::string title,std::string detail){result.postureFindings.push_back({std::move(severity),std::move(title),std::move(detail)});};

    step("Primary SIP service and capability probe");
    auto primary=serviceProbe(options.host,options.port,options.transport,options.timeoutMs);
    result.responses.push_back(primary);

    step("Fingerprint from service-probe output");
    result.fingerprint=fingerprintFromProbe(primary);
    if(!result.fingerprint.version.empty())
        posture("WARN","Detailed product/version disclosure","The target disclosed "+result.fingerprint.product+" "+result.fingerprint.version+" in unauthenticated SIP signaling. Minimize version banners where practical.");
    else if(!primary.server.empty()||!primary.userAgent.empty())
        posture("INFO","Product banner disclosure","Unauthenticated SIP signaling exposed a Server/User-Agent banner. Review whether this disclosure is necessary.");
    const auto leakedPrivate=privateIpv4Addresses(primary.rawResponse);
    if(!leakedPrivate.empty()&&!isPrivateIpv4(options.host)){
        std::ostringstream leak;for(std::size_t i=0;i<leakedPrivate.size();++i){if(i)leak<<", ";leak<<leakedPrivate[i];}
        posture("INFO","Private topology address disclosed","The unauthenticated SIP response exposed RFC1918 address(es): "+leak.str()+". If this is an Internet-facing SBC/PBX interface, review topology hiding and header normalization.");
    }

    step("SIP method-policy audit");
    auto methods=methodAudit(options.host,options.port,options.transport,options.timeoutMs);append(std::move(methods));
    const auto allow=lower(primary.allow);
    std::vector<std::string> expanded;
    for(const auto* method:{"refer","message","subscribe","publish","notify"})if(allow.find(method)!=std::string::npos)expanded.emplace_back(method);
    if(!expanded.empty()){
        std::ostringstream d;d<<"Advertised optional methods:";for(const auto&m:expanded)d<<" "<<m;
        d<<". Disable methods that are not operationally required and enforce authentication/authorization on those that remain.";
        posture("INFO","Expanded SIP method surface",d.str());
    }

    step("Authentication and Digest-oracle chain");
    const std::string user=options.username.empty()?"sipher-audit":options.username;
    auto auth=authenticationAudit(options.host,user,options.port,options.transport,options.timeoutMs);
    result.responses.push_back(auth);
    if(!options.username.empty()&&(auth.statusCode==401||auth.statusCode==407)){
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        auto repeat=authenticationAudit(options.host,user,options.port,options.transport,options.timeoutMs);repeat.testName="Digest challenge repeat";
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        auto control=authenticationAudit(options.host,"tm-invalid-"+token(10),options.port,options.transport,options.timeoutMs);control.testName="Digest invalid-account control";
        const auto n1=authParam(auth.authenticate,"nonce"),n2=authParam(repeat.authenticate,"nonce");
        if(!n1.empty()&&!n2.empty()){
            if(n1==n2)repeat.findings.push_back({"WARN","Digest nonce reused","The first authentication-stage output and repeated challenge returned the same nonce. Review nonce lifetime and replay protections."});
            else repeat.findings.push_back({"PASS","Digest nonce changed","The repeated challenge returned a different nonce from the pipeline's initial authentication stage."});
        }
        if(auth.statusCode!=control.statusCode)
            control.findings.push_back({"WARN","Account-response oracle","The supplied test account and a random-invalid control received different SIP status codes ("+std::to_string(auth.statusCode)+" vs "+std::to_string(control.statusCode)+"). Normalize externally visible authentication failures where practical."});
        else if(auth.statusCode)control.findings.push_back({"PASS","Consistent account response","The supplied test account and random-invalid control received the same SIP status code."});
        result.responses.push_back(std::move(repeat));result.responses.push_back(std::move(control));
    }else if(options.username.empty()){
        posture("INFO","Digest oracle not run","No known test account was supplied, so account-differential checks were skipped rather than guessing usernames.");
    }else{
        posture("INFO","Digest oracle condition not met","The authentication stage did not return a Digest challenge, so the account-differential stage was not expanded.");
    }

    step("Standards and protocol-compliance checks");
    append(complianceAudit(options.host,options.port,options.transport,options.timeoutMs));

    step("Alternate cleartext SIP transport check");
    const auto alternate=options.transport==AuditTransport::Udp?AuditTransport::Tcp:AuditTransport::Udp;
    auto alt=serviceProbe(options.host,options.port,alternate,std::min(options.timeoutMs,1200u));
    alt.testName="Alternate transport exposure probe";result.responses.push_back(alt);
    if(primary.statusCode&&alt.statusCode)
        posture("INFO","UDP and TCP SIP both reachable","The same SIP service responded over both cleartext UDP and TCP. Verify that both transports are required and equally protected by ACLs/rate limits.");
    else if(primary.statusCode&&options.transport==AuditTransport::Udp&&!alt.statusCode)
        posture("INFO","UDP-only cleartext exposure observed","The selected SIP service responded over UDP while the alternate TCP probe did not. Review UDP ACLs and anti-spoofing/rate controls.");

    if(options.includeParserAudit){step("Bounded parser-normalization checks");append(parserAbuseAudit(options.host,options.port,options.transport,options.timeoutMs));}
    if(options.includeResilienceAudit){step("Bounded rate/resilience checks");append(resilienceAudit(options.host,options.port,options.transport,6,200,std::min(options.timeoutMs,1100u)));}
    if(options.includeExtensionAudit){step("Opt-in extension differential range");result.extensions=extensionAudit(options.host,options.extensionFirst,options.extensionLast,options.port,options.transport,250,std::min(options.timeoutMs,1200u));}

    if(options.includeTlsAudit){
        step("SIP TLS posture");
        try{result.tlsSummary=tlsAudit(options.host,options.tlsPort,4000);}catch(const std::exception&e){result.tlsSummary=std::string("TLS audit unavailable: ")+e.what();}
        const auto tlsLower=lower(result.tlsSummary);
        if(tlsLower.find("tlsv1.3")!=std::string::npos||tlsLower.find("tlsv1.2")!=std::string::npos)
            posture("PASS","Modern SIP TLS negotiated","The TLS helper reported TLS 1.2 or TLS 1.3 on the configured SIP-TLS port.");
        else if(tlsLower.find("tlsv1.1")!=std::string::npos||tlsLower.find("tlsv1.0")!=std::string::npos||tlsLower.find("protocol version: tlsv1")!=std::string::npos)
            posture("HIGH","Legacy TLS protocol observed","The SIP-TLS endpoint appears to negotiate TLS 1.0/1.1. Disable legacy protocol versions and require modern TLS where endpoint compatibility allows.");
        else if(tlsLower.find("failed")!=std::string::npos||tlsLower.find("unavailable")!=std::string::npos||tlsLower.find("no tls")!=std::string::npos)
            posture("WARN","SIP TLS not confirmed","The automated audit could not confirm a SIP-TLS handshake on port "+std::to_string(options.tlsPort)+". If signaling crosses untrusted networks, validate TLS availability and certificate policy.");
        if(tlsLower.find("rc4")!=std::string::npos||tlsLower.find("3des")!=std::string::npos||tlsLower.find("des-cbc3")!=std::string::npos)
            posture("HIGH","Legacy TLS cipher observed","The TLS handshake output references RC4/3DES-class ciphers. Remove legacy cipher suites.");
        if(tlsLower.find("certificate verification exit code: 0")!=std::string::npos)
            posture("PASS","SIP TLS certificate verified","The local OpenSSL trust and hostname/IP verification completed successfully for the configured SIP-TLS endpoint.");
        else if(tlsLower.find("certificate verification exit code:")!=std::string::npos)
            posture("WARN","SIP TLS certificate verification failed","The TLS endpoint negotiated, but the local trust/identity verification did not succeed. Review the certificate chain, trust anchor, SAN/hostname or IP identity, and expiration.");
        if(tlsLower.find("certificate has expired")!=std::string::npos||tlsLower.find("certificate expired")!=std::string::npos)
            posture("HIGH","Expired SIP TLS certificate observed","The OpenSSL verification output indicates an expired certificate. Replace it and confirm endpoints trust the renewed chain.");
        if(tlsLower.find("hostname mismatch")!=std::string::npos||tlsLower.find("ip address mismatch")!=std::string::npos)
            posture("WARN","SIP TLS identity mismatch","The certificate identity does not match the audited hostname/IP. Review SAN entries and endpoint certificate validation policy.");
    }

    if(options.includeVulnerabilityLookup){
        step("Public CVE / Exploit-DB metadata correlation");
        result.vulnerabilityReport=vulnerabilityLookupReport(result.fingerprint,12);
        if(result.vulnerabilityReport.find("CVE-")!=std::string::npos)
            posture("WARN","Public vulnerability metadata matched fingerprint","Public CVE metadata matched the remotely disclosed product/version. This is correlation, not proof of exploitability; verify the exact installed build and vendor advisories.");
    }

    step("Prioritize findings and remediation");
    auto count=[&](const AuditFinding& f){const auto sev=lower(f.severity);if(sev=="high"||sev=="critical")++result.highCount;else if(sev=="warn"||sev=="warning"||sev=="medium")++result.warnCount;else if(sev=="pass")++result.passCount;else ++result.infoCount;};
    for(const auto& r:result.responses){for(const auto& f:r.findings)count(f);}
    for(const auto& f:result.postureFindings)count(f);
    return result;
}

std::string AutomatedAuditResult::toText() const
{
    std::ostringstream o;
    o<<"S.I.P.H.E.R. 1.0.0 r8 — AUTOMATED CHAINED PBX / SIP SECURITY AUDIT\n";
    o<<PbxAudit::warningText()<<"\n\n";
    o<<"Target: "<<options.host<<":"<<options.port<<"/"<<PbxAudit::transportName(options.transport)<<"\n";
    if(!options.username.empty())o<<"Authorized test account: "<<options.username<<"\n";
    o<<"Risk summary: "<<highCount<<" HIGH, "<<warnCount<<" WARN, "<<passCount<<" PASS, "<<infoCount<<" INFO\n";
    o<<"Pipeline: service -> fingerprint -> methods -> authentication/oracle -> compliance -> alternate transport";
    if(options.includeParserAudit)o<<" -> parser";
    if(options.includeResilienceAudit)o<<" -> resilience";
    if(options.includeExtensionAudit)o<<" -> extension range";
    if(options.includeTlsAudit)o<<" -> TLS";
    if(options.includeVulnerabilityLookup)o<<" -> CVE metadata";
    o<<" -> prioritized report\n\n";

    if(!postureFindings.empty()){
        o<<"EXECUTIVE SECURITY POSTURE\n--------------------------\n";
        for(const auto&f:postureFindings)o<<"["<<f.severity<<"] "<<f.title<<": "<<f.detail<<"\n";
        o<<"\n";
    }

    o<<fingerprint.toText()<<"\n";
    o<<"CHAINED PROBE DETAILS\n---------------------\n";
    for(const auto&r:responses)o<<r.toText(false)<<"\n";

    if(!extensions.empty()){
        o<<"OPT-IN EXTENSION DIFFERENTIAL AUDIT\n-----------------------------------\n";
        o<<"EXT       CODE  LATENCY   ASSESSMENT\n";
        for(const auto&e:extensions)o<<std::setw(9)<<std::left<<e.extension<<std::setw(6)<<e.statusCode<<std::setw(10)<<std::fixed<<std::setprecision(1)<<e.latencyMs<<e.assessment<<"\n";
        o<<"\n";
    }
    if(!tlsSummary.empty())o<<"TLS HANDSHAKE SUMMARY\n---------------------\n"<<tlsSummary<<"\n\n";
    if(!vulnerabilityReport.empty())o<<vulnerabilityReport<<"\n";

    std::set<std::string> remediations;
    auto consider=[&](const AuditFinding&f){const auto title=lower(f.title);if(title.find("unauthenticated register")!=std::string::npos)remediations.insert("Require authentication and authorization for REGISTER; confirm SBC/PBX ACLs prevent untrusted registration attempts.");if(title.find("account-response oracle")!=std::string::npos)remediations.insert("Normalize externally visible authentication failures so valid and invalid accounts are harder to distinguish.");if(title.find("digest md5")!=std::string::npos)remediations.insert("Prefer stronger SIP Digest algorithms where supported and protect authentication exchanges with TLS.");if(title.find("large udp response")!=std::string::npos)remediations.insert("Rate-limit and ACL unauthenticated UDP SIP; apply anti-spoofing controls to reduce reflection/amplification risk.");if(title.find("parser edge accepted")!=std::string::npos||title.find("unknown sip method")!=std::string::npos)remediations.insert("Tighten SIP normalization and reject malformed/unknown protocol elements before they reach downstream systems.");if(title.find("version disclosure")!=std::string::npos||title.find("banner disclosure")!=std::string::npos)remediations.insert("Minimize unauthenticated Server/User-Agent version disclosure where operationally practical.");if(title.find("topology address")!=std::string::npos)remediations.insert("Review SBC topology hiding and SIP header normalization so private routing addresses are not unnecessarily disclosed to untrusted peers.");if(title.find("digest algorithm not declared")!=std::string::npos)remediations.insert("Explicitly configure a modern SIP Digest algorithm where endpoint interoperability allows instead of relying on a legacy default.");if(title.find("tls")!=std::string::npos)remediations.insert("Validate SIP-TLS on 5061, certificate trust/hostname policy, and modern TLS protocols/ciphers.");if(title.find("vulnerability metadata")!=std::string::npos)remediations.insert("Verify exact PBX/SBC/module versions against vendor advisories and patch unsupported or vulnerable builds.");};
    for(const auto& r:responses){for(const auto& f:r.findings)consider(f);}
    for(const auto& f:postureFindings)consider(f);
    if(!remediations.empty()){
        o<<"PRIORITIZED REMEDIATION NOTES\n----------------------------\n";
        unsigned n=1;for(const auto&x:remediations)o<<n++<<". "<<x<<"\n";
        o<<"\n";
    }
    o<<"Analyst note: warnings are engineering leads, not automatic proof of compromise or exploitability. Confirm findings against the actual PBX/SBC configuration, network policy, and vendor guidance.\n";
    return o.str();
}

std::string PbxFingerprint::toText() const
{
    std::ostringstream o;o<<"S.I.P.H.E.R. By GITSC — PBX / SIP FINGERPRINT\n"<<"Target: "<<host<<"\nProduct: "<<product<<"\n";
    if(!vendor.empty())o<<"Vendor: "<<vendor<<"\n";
    if(!version.empty())o<<"Version: "<<version<<"\n";
    o<<"Confidence: "<<confidence<<"\n";
    if(!serverBanner.empty())o<<"Server banner: "<<serverBanner<<"\n";
    if(!userAgentBanner.empty())o<<"User-Agent banner: "<<userAgentBanner<<"\n";
    if(!components.empty()){o<<"\nDisclosed components / modules / capabilities:\n";for(const auto&c:components)o<<" - "<<c.name<<(c.version.empty()?"":" "+c.version)<<" — "<<c.evidence<<"\n";}
    if(!capabilities.empty()){o<<"\nSIP capabilities:\n";for(const auto&c:capabilities)o<<" - "<<c<<"\n";}
    if(!notes.empty()){o<<"\nNotes:\n";for(const auto&n:notes)o<<" - "<<n<<"\n";}return o.str();
}

std::string PbxAudit::vulnerabilityLookupReport(const PbxFingerprint&fp,unsigned maxResults)
{
    if(maxResults<1)maxResults=1;
    if(maxResults>25)maxResults=25;
    std::ostringstream o;o<<"PUBLIC VULNERABILITY CORRELATION\n"<<"No exploit code is executed. Results require analyst verification.\n\n";
    if(fp.product.empty()||fp.product.find("Unknown")!=std::string::npos){o<<"No recognized product fingerprint is available; CVE correlation skipped to avoid broad/noisy matching.\n";return o.str();}
    const std::string query=fp.product+(fp.version.empty()?std::string{}:" "+fp.version);
    const std::string url="https://services.nvd.nist.gov/rest/json/cves/2.0?keywordSearch="+urlEncode(query)+"&resultsPerPage="+std::to_string(maxResults);
    std::vector<std::string> curl={"curl","-fsSL","--connect-timeout","6","--max-time","15","-A","S.I.P.H.E.R./1.0.0",url};
    if(const char*key=std::getenv("NVD_API_KEY");key&&*key){curl.insert(curl.end()-1,"-H");curl.insert(curl.end()-1,std::string("apiKey:")+key);}
    int nvdRc=0;const auto json=runProgramCapture(curl,18000,&nvdRc);o<<"NIST NVD CVE 2.0 — query: "<<query<<"\n";
    unsigned shown=0;std::size_t pos=0;
    if(nvdRc==0&&!json.empty()){
        while(shown<maxResults){const auto idp=json.find("\"id\":\"CVE-",pos);if(idp==std::string::npos)break;const auto next=json.find("\"id\":\"CVE-",idp+8);const auto id=jsonStringAfter(json,"\"id\":\"",idp,next);const auto desc=jsonStringAfter(json,"\"value\":\"",idp,next);auto sev=jsonStringAfter(json,"\"baseSeverity\":\"",idp,next);if(sev.empty())sev="UNRATED";o<<" - "<<id<<" ["<<sev<<"] "<<trim(desc)<<"\n";++shown;pos=(next==std::string::npos?json.size():next);}
        if(shown==0)o<<" - No matching CVE records returned.\n";
    }else o<<" - NVD lookup unavailable (curl/API error). Check connectivity, rate limits, or NVD_API_KEY.\n";

    o<<"\nExploit-DB metadata — query: "<<query<<"\n";
    const std::filesystem::path cache=runtime::stateDir()/"cache"/"files_exploits.csv";
    std::error_code ec;bool refresh=!std::filesystem::exists(cache,ec);if(!refresh){auto age=std::filesystem::file_time_type::clock::now()-std::filesystem::last_write_time(cache,ec);if(!ec&&age>std::chrono::hours(24*7))refresh=true;}
    if(refresh){std::filesystem::create_directories(cache.parent_path(),ec);const std::string tmp=cache.string()+".tmp";int rc=0;runProgramCapture({"curl","-fsSL","--connect-timeout","6","--max-time","30","-o",tmp,"https://gitlab.com/exploit-database/exploitdb/-/raw/main/files_exploits.csv"},35000,&rc);if(rc==0&&std::filesystem::exists(tmp)){std::filesystem::rename(tmp,cache,ec);if(ec){std::filesystem::remove(cache,ec);ec.clear();std::filesystem::rename(tmp,cache,ec);}}else std::filesystem::remove(tmp,ec);}
    std::ifstream csv(cache);const auto productNeedle=lower(fp.product),versionNeedle=lower(fp.version);shown=0;
    if(csv){std::string line;std::getline(csv,line);while(shown<maxResults&&std::getline(csv,line)){const auto ll=lower(line);if(ll.find(productNeedle)==std::string::npos)continue;if(!versionNeedle.empty()&&ll.find(versionNeedle)==std::string::npos)continue;const auto fields=csvFields(line);if(fields.size()<3)continue;o<<" - EDB-"<<fields[0]<<": "<<fields[2]<<" (https://www.exploit-db.com/exploits/"<<fields[0]<<")\n";++shown;}if(shown==0)o<<" - No matching Exploit-DB metadata entries found in the current cache.\n";}
    else o<<" - Exploit-DB metadata cache unavailable. curl is required to retrieve the official metadata index.\n";
    o<<"\nCorrelation note: banner/version matches are not proof that a CVE is exploitable on this host. Confirm exact package/module versions and vendor advisories before remediation or testing.\n";return o.str();
}

std::string PbxAudit::tlsAudit(const std::string&host,std::uint16_t port,unsigned timeoutMs)
{
    if(host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:_")!=std::string::npos)throw std::runtime_error("TLS audit host contains unsupported characters");
    const std::string endpoint=host+":"+std::to_string(port);
    int rc=0;auto out=runProgramCapture({"openssl","s_client","-connect",endpoint,"-servername",host,"-brief","-no_ign_eof"},timeoutMs+2000,&rc);
    if(out.empty()){
        if(rc!=0)return "TLS audit helper unavailable or handshake failed. Portable Windows builds expect openssl.exe beside S.I.P.H.E.R. or on PATH.";
        return "No TLS handshake output received.";
    }
    std::vector<std::string> verify={"openssl","s_client","-connect",endpoint,"-servername",host,"-brief","-verify_return_error","-no_ign_eof"};
    if(looksLikeIpv4Literal(host)){verify.insert(verify.end()-1,"-verify_ip");verify.insert(verify.end()-1,host);}
    else{verify.insert(verify.end()-1,"-verify_hostname");verify.insert(verify.end()-1,host);}
    int verifyRc=0;const auto verified=runProgramCapture(verify,timeoutMs+2000,&verifyRc);
    out+="\n--- Certificate trust / identity verification ---\n";
    if(!verified.empty())out+=verified;else out+="No additional verification output received.\n";
    out+="Certificate verification exit code: "+std::to_string(verifyRc)+"\n";
    return out;
}

std::string AuditResponse::toText(bool includeRaw) const
{
    std::ostringstream o;o<<"["<<testName<<"] "<<target<<":"<<port<<"/"<<PbxAudit::transportName(transport)<<"\n";
    if(statusCode)o<<"  Response: "<<statusCode<<" "<<reason<<"\n";else o<<"  Response: "<<(reason.empty()?"none":reason)<<"\n";
    o<<"  Latency: "<<std::fixed<<std::setprecision(1)<<latencyMs<<" ms\n";
    if(!server.empty())o<<"  Server: "<<server<<"\n";
    if(!userAgent.empty())o<<"  User-Agent: "<<userAgent<<"\n";
    if(!allow.empty())o<<"  Allow: "<<allow<<"\n";
    if(!supported.empty())o<<"  Supported: "<<supported<<"\n";
    if(!authenticate.empty())o<<"  Authenticate: "<<authenticate<<"\n";
    if(requestBytes)o<<"  Size ratio: "<<requestBytes<<" request bytes -> "<<responseBytes<<" response bytes\n";
    for(const auto&f:findings)o<<"  ["<<f.severity<<"] "<<f.title<<": "<<f.detail<<"\n";
    if(includeRaw){o<<"\n--- REQUEST ---\n"<<rawRequest<<"\n--- RESPONSE ---\n"<<rawResponse<<"\n";}return o.str();
}

std::string PbxAudit::report(const std::string&title,const std::vector<AuditResponse>&responses,const std::vector<ExtensionAuditEntry>&extensions,const std::string&tls,const std::vector<DiscoveryEntry>&discovery)
{
    std::ostringstream o;o<<"S.I.P.H.E.R. 1.0.0 — SIP Inspection, Protocol Handling, Enumeration & Recon\n"<<title<<"\n"<<warningText()<<"\n\n";
    if(!discovery.empty()){
        o<<"Bounded SIP discovery results\n";
        o<<"HOST                 PORT  TRANSPORT  CODE  LATENCY   BANNER\n";
        for(const auto&e:discovery)o<<std::setw(21)<<std::left<<e.host<<std::setw(6)<<e.port<<std::setw(11)<<transportName(e.transport)<<std::setw(6)<<e.statusCode<<std::setw(10)<<std::fixed<<std::setprecision(1)<<e.latencyMs<<e.server<<"\n";
        o<<"\n";
    }
    for(const auto&r:responses)o<<r.toText(false)<<"\n";
    if(!extensions.empty()){
        o<<"Extension differential audit (rate-limited; responses are hints, not proof of account existence)\n";
        o<<"EXT       CODE  LATENCY   ASSESSMENT\n";
        for(const auto&e:extensions)o<<std::setw(9)<<std::left<<e.extension<<std::setw(6)<<e.statusCode<<std::setw(10)<<std::fixed<<std::setprecision(1)<<e.latencyMs<<e.assessment<<"\n";
        o<<"\n";
    }
    if(!tls.empty())o<<"TLS handshake summary\n---------------------\n"<<tls<<"\n";
    return o.str();
}

void PbxAudit::saveReport(const std::string&path,const std::string&text)
{
    const std::filesystem::path p(path);if(p.has_parent_path()){std::error_code ec;std::filesystem::create_directories(p.parent_path(),ec);if(ec&&!std::filesystem::is_directory(p.parent_path()))throw std::runtime_error("Unable to create report directory: "+ec.message());}
    std::ofstream out(path,std::ios::trunc);if(!out)throw std::runtime_error("Unable to create audit report: "+path);out<<text;out.close();
#ifndef _WIN32
    (void)chmod(path.c_str(),S_IRUSR|S_IWUSR);
#endif
}

} // namespace trunkmonkey
