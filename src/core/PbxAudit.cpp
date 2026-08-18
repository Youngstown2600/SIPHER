#include "trunkmonkey/PbxAudit.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#ifndef _WIN32
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

#ifndef _WIN32
std::string runProgramCapture(const std::vector<std::string>& args,unsigned timeoutMs,int* exitCode=nullptr)
{
    if(args.empty())return{};
    int pipefd[2];if(pipe(pipefd)!=0)throw std::runtime_error("pipe() failed: "+std::string(std::strerror(errno)));
    posix_spawn_file_actions_t a;posix_spawn_file_actions_init(&a);posix_spawn_file_actions_addopen(&a,STDIN_FILENO,"/dev/null",O_RDONLY,0);posix_spawn_file_actions_adddup2(&a,pipefd[1],STDOUT_FILENO);posix_spawn_file_actions_adddup2(&a,pipefd[1],STDERR_FILENO);posix_spawn_file_actions_addclose(&a,pipefd[0]);
    std::vector<std::string> local=args;std::vector<char*> argv;for(auto&x:local)argv.push_back(x.data());argv.push_back(nullptr);
    pid_t pid=-1;const int rc=posix_spawnp(&pid,local[0].c_str(),&a,nullptr,argv.data(),environ);posix_spawn_file_actions_destroy(&a);close(pipefd[1]);
    if(rc!=0){close(pipefd[0]);if(exitCode)*exitCode=rc;return{};}
    int flags=fcntl(pipefd[0],F_GETFL,0);if(flags>=0)(void)fcntl(pipefd[0],F_SETFL,flags|O_NONBLOCK);
    std::string out;std::array<char,4096>b{};const auto start=std::chrono::steady_clock::now();int status=0;bool done=false;
    while(!done){for(;;){const auto n=read(pipefd[0],b.data(),b.size());if(n>0)out.append(b.data(),static_cast<std::size_t>(n));else break;}const auto w=waitpid(pid,&status,WNOHANG);if(w==pid)done=true;else if(w<0)done=true;else if(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()>timeoutMs){kill(pid,SIGTERM);std::this_thread::sleep_for(std::chrono::milliseconds(50));kill(pid,SIGKILL);waitpid(pid,&status,0);status=124<<8;done=true;}else std::this_thread::sleep_for(std::chrono::milliseconds(25));}
    for(;;){const auto n=read(pipefd[0],b.data(),b.size());if(n>0)out.append(b.data(),static_cast<std::size_t>(n));else break;}close(pipefd[0]);if(exitCode)*exitCode=(WIFEXITED(status)?WEXITSTATUS(status):128);return out;
}
#endif

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

#ifndef _WIN32
struct AddrList{addrinfo*p{nullptr};~AddrList(){if(p)freeaddrinfo(p);}};

AddrList resolve(const std::string&host,std::uint16_t port,int socktype)
{
    addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=socktype;hints.ai_protocol=(socktype==SOCK_DGRAM?IPPROTO_UDP:IPPROTO_TCP);
    AddrList out;const auto ps=std::to_string(port);const int rc=getaddrinfo(host.c_str(),ps.c_str(),&hints,&out.p);
    if(rc!=0)throw std::runtime_error("Unable to resolve "+host+": "+gai_strerror(rc));
    return out;
}

bool connectTimed(int fd,const sockaddr*sa,socklen_t len,unsigned timeoutMs)
{
    const int old=fcntl(fd,F_GETFL,0);if(old<0)return false;if(fcntl(fd,F_SETFL,old|O_NONBLOCK)<0)return false;
    int rc=::connect(fd,sa,len);if(rc==0){fcntl(fd,F_SETFL,old);return true;}if(errno!=EINPROGRESS){fcntl(fd,F_SETFL,old);return false;}
    fd_set wf;FD_ZERO(&wf);FD_SET(fd,&wf);timeval tv{static_cast<long>(timeoutMs/1000),static_cast<long>((timeoutMs%1000)*1000)};
    rc=select(fd+1,nullptr,&wf,nullptr,&tv);if(rc<=0){fcntl(fd,F_SETFL,old);return false;}int err=0;socklen_t el=sizeof(err);getsockopt(fd,SOL_SOCKET,SO_ERROR,&err,&el);fcntl(fd,F_SETFL,old);return err==0;
}

std::string transact(const std::string&host,std::uint16_t port,AuditTransport transport,const std::string&request,unsigned timeoutMs,double&latency)
{
    const int st=transport==AuditTransport::Udp?SOCK_DGRAM:SOCK_STREAM;auto addrs=resolve(host,port,st);std::string lastError="No usable address";
    for(auto*ai=addrs.p;ai;ai=ai->ai_next){
        int fd=::socket(ai->ai_family,ai->ai_socktype,ai->ai_protocol);if(fd<0){lastError=std::strerror(errno);continue;}
        timeval tv{static_cast<long>(timeoutMs/1000),static_cast<long>((timeoutMs%1000)*1000)};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
        const auto started=std::chrono::steady_clock::now();
        ssize_t sent=-1;
        if(transport==AuditTransport::Udp) sent=sendto(fd,request.data(),request.size(),0,ai->ai_addr,static_cast<socklen_t>(ai->ai_addrlen));
        else if(connectTimed(fd,ai->ai_addr,static_cast<socklen_t>(ai->ai_addrlen),timeoutMs)) sent=send(fd,request.data(),request.size(),0);
        if(sent<0){lastError=std::strerror(errno);::close(fd);continue;}
        std::array<char,65536>buf{};std::string raw;
        for(;;){const ssize_t n=recv(fd,buf.data(),buf.size(),0);if(n>0){raw.append(buf.data(),static_cast<std::size_t>(n));if(raw.find("\r\n\r\n")!=std::string::npos||transport==AuditTransport::Udp)break;continue;}break;}
        latency=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();::close(fd);
        if(!raw.empty())return raw;
        lastError="No SIP response before timeout";
    }
    throw std::runtime_error(lastError);
}
#endif

AuditResponse run(const std::string&name,const std::string&host,std::uint16_t port,AuditTransport transport,const std::string&req,unsigned timeoutMs)
{
    AuditResponse r;r.target=host;r.port=port;r.transport=transport;r.testName=name;r.rawRequest=req;r.requestBytes=req.size();
#ifndef _WIN32
    try{r.rawResponse=transact(host,port,transport,req,timeoutMs,r.latencyMs);r.responseBytes=r.rawResponse.size();parseStatus(r);}catch(const std::exception&e){r.reason=e.what();r.findings.push_back({"INFO","No SIP response",e.what()});return r;}
#else
    r.reason="PBX audit networking is implemented for Linux/FreeBSD builds";
#endif
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

#ifndef _WIN32
std::vector<std::string> cidrHosts(const std::string& cidr)
{
    const auto slash=cidr.find('/');
    if(slash==std::string::npos) throw std::runtime_error("audit discovery requires IPv4 CIDR notation, for example 192.0.2.0/28");
    const auto ip=cidr.substr(0,slash);
    const auto prefixText=cidr.substr(slash+1);
    char* end=nullptr;const long prefix=std::strtol(prefixText.c_str(),&end,10);
    if(end==prefixText.c_str()||*end!='\0'||prefix<27||prefix>32)
        throw std::runtime_error("audit discovery is limited to /27 through /32 (maximum 32 addresses per run)");
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
#endif

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
#ifndef _WIN32
    if(delayMs<100)delayMs=100;
    const auto hosts=cidrHosts(cidr);std::vector<DiscoveryEntry> out;
    for(std::size_t i=0;i<hosts.size();++i){
        auto r=serviceProbe(hosts[i],port,transport,timeoutMs);
        if(r.statusCode!=0)out.push_back({hosts[i],port,transport,r.statusCode,r.reason,r.server.empty()?r.userAgent:r.server,r.latencyMs});
        if(i+1<hosts.size())std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
    return out;
#else
    (void)cidr;(void)port;(void)transport;(void)delayMs;(void)timeoutMs;
    throw std::runtime_error("PBX discovery is implemented for Linux/FreeBSD builds");
#endif
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
        if(a.find("algorithm=md5")!=std::string::npos||a.find("algorithm=\"md5\"")!=std::string::npos)r.findings.push_back({"WARN","Digest MD5 in use","Digest challenge advertises MD5; prefer stronger algorithms when endpoint support allows."});
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


PbxFingerprint PbxAudit::fingerprint(const std::string&host,std::uint16_t port,AuditTransport transport,unsigned timeoutMs)
{
    const auto probe=serviceProbe(host,port,transport,timeoutMs);PbxFingerprint fp;fp.host=host;fp.serverBanner=probe.server;fp.userAgentBanner=probe.userAgent;
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
#ifndef _WIN32
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
    const char*home=std::getenv("HOME");std::filesystem::path cache=(home&&*home)?std::filesystem::path(home)/".cache"/"sipher"/"files_exploits.csv":std::filesystem::temp_directory_path()/"sipher-files_exploits.csv";
    std::error_code ec;bool refresh=!std::filesystem::exists(cache,ec);if(!refresh){auto age=std::filesystem::file_time_type::clock::now()-std::filesystem::last_write_time(cache,ec);if(!ec&&age>std::chrono::hours(24*7))refresh=true;}
    if(refresh){std::filesystem::create_directories(cache.parent_path(),ec);const std::string tmp=cache.string()+".tmp";int rc=0;runProgramCapture({"curl","-fsSL","--connect-timeout","6","--max-time","30","-o",tmp,"https://gitlab.com/exploit-database/exploitdb/-/raw/main/files_exploits.csv"},35000,&rc);if(rc==0&&std::filesystem::exists(tmp)){std::filesystem::rename(tmp,cache,ec);if(ec){std::filesystem::remove(cache,ec);ec.clear();std::filesystem::rename(tmp,cache,ec);}}else std::filesystem::remove(tmp,ec);}
    std::ifstream csv(cache);const auto productNeedle=lower(fp.product),versionNeedle=lower(fp.version);shown=0;
    if(csv){std::string line;std::getline(csv,line);while(shown<maxResults&&std::getline(csv,line)){const auto ll=lower(line);if(ll.find(productNeedle)==std::string::npos)continue;if(!versionNeedle.empty()&&ll.find(versionNeedle)==std::string::npos)continue;const auto fields=csvFields(line);if(fields.size()<3)continue;o<<" - EDB-"<<fields[0]<<": "<<fields[2]<<" (https://www.exploit-db.com/exploits/"<<fields[0]<<")\n";++shown;}if(shown==0)o<<" - No matching Exploit-DB metadata entries found in the current cache.\n";}
    else o<<" - Exploit-DB metadata cache unavailable. curl is required to retrieve the official metadata index.\n";
    o<<"\nCorrelation note: banner/version matches are not proof that a CVE is exploitable on this host. Confirm exact package/module versions and vendor advisories before remediation or testing.\n";return o.str();
#else
    o<<"NVD/Exploit-DB correlation is currently implemented for Linux/FreeBSD builds.\n";return o.str();
#endif
}

std::string PbxAudit::tlsAudit(const std::string&host,std::uint16_t port,unsigned timeoutMs)
{
#ifndef _WIN32
    if(host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-:_")!=std::string::npos)throw std::runtime_error("TLS audit host contains unsupported characters");
    int pipefd[2];if(pipe(pipefd)!=0)throw std::runtime_error("pipe() failed: "+std::string(std::strerror(errno)));
    posix_spawn_file_actions_t a;posix_spawn_file_actions_init(&a);posix_spawn_file_actions_addopen(&a,STDIN_FILENO,"/dev/null",O_RDONLY,0);posix_spawn_file_actions_adddup2(&a,pipefd[1],STDOUT_FILENO);posix_spawn_file_actions_adddup2(&a,pipefd[1],STDERR_FILENO);posix_spawn_file_actions_addclose(&a,pipefd[0]);
    const std::string endpoint=host+":"+std::to_string(port);std::vector<std::string> args={"openssl","s_client","-connect",endpoint,"-servername",host,"-brief","-no_ign_eof"};std::vector<char*> argv;for(auto&s:args)argv.push_back(s.data());argv.push_back(nullptr);
    pid_t pid=-1;int rc=posix_spawnp(&pid,"openssl",&a,nullptr,argv.data(),environ);posix_spawn_file_actions_destroy(&a);close(pipefd[1]);if(rc!=0){close(pipefd[0]);throw std::runtime_error("Unable to launch openssl: "+std::string(std::strerror(rc)));}
    const auto start=std::chrono::steady_clock::now();int status=0;for(;;){const auto w=waitpid(pid,&status,WNOHANG);if(w==pid)break;if(w<0)break;if(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()>timeoutMs){kill(pid,SIGTERM);std::this_thread::sleep_for(std::chrono::milliseconds(100));kill(pid,SIGKILL);waitpid(pid,&status,0);break;}std::this_thread::sleep_for(std::chrono::milliseconds(50));}
    std::string out;std::array<char,4096>b{};for(;;){const auto n=read(pipefd[0],b.data(),b.size());if(n>0)out.append(b.data(),static_cast<std::size_t>(n));else break;}close(pipefd[0]);if(out.empty())out="No TLS handshake output received.";return out;
#else
    (void)host;(void)port;(void)timeoutMs;return "TLS audit is implemented for Unix builds.";
#endif
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
