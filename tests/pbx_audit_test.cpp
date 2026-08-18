#include "trunkmonkey/PbxAudit.h"
#include <cstring>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>
#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
using namespace trunkmonkey;

namespace {
void require(bool ok, const char* message)
{
    if(!ok) throw std::runtime_error(message);
}
}

int main(){
#ifndef _WIN32
    int fd=::socket(AF_INET,SOCK_DGRAM,0);require(fd>=0,"socket() failed");
    sockaddr_in sa{};sa.sin_family=AF_INET;sa.sin_addr.s_addr=htonl(INADDR_LOOPBACK);sa.sin_port=0;require(::bind(fd,(sockaddr*)&sa,sizeof(sa))==0,"bind() failed");
    socklen_t sl=sizeof(sa);require(::getsockname(fd,(sockaddr*)&sa,&sl)==0,"getsockname() failed");const auto port=ntohs(sa.sin_port);
    std::thread server([&](){char b[8192];sockaddr_in peer{};socklen_t pl=sizeof(peer);auto n=recvfrom(fd,b,sizeof(b),0,(sockaddr*)&peer,&pl);require(n>0,"recvfrom() failed");std::string req(b,(size_t)n);const char*resp="SIP/2.0 200 OK\r\nServer: TestPBX/1\r\nAllow: INVITE, ACK, OPTIONS, REGISTER, REFER\r\nSupported: timer\r\nContent-Length: 0\r\n\r\n";sendto(fd,resp,std::strlen(resp),0,(sockaddr*)&peer,pl);});
    auto r=PbxAudit::serviceProbe("127.0.0.1",port,AuditTransport::Udp,1000);server.join();close(fd);
    require(r.statusCode==200,"service probe did not receive 200");require(r.server=="TestPBX/1","service probe banner mismatch");require(r.allow.find("REGISTER")!=std::string::npos,"REGISTER missing from Allow header");require(!r.findings.empty(),"service probe produced no findings");
    auto text=PbxAudit::report("test",{r});require(text.find("AUTHORIZED SYSTEMS ONLY")!=std::string::npos,"audit warning missing from report");require(text.find("TestPBX/1")!=std::string::npos,"banner missing from report");

    // Bounded discovery supports only small explicit IPv4 CIDRs. /32 sends one probe.
    int dfd=::socket(AF_INET,SOCK_DGRAM,0);require(dfd>=0,"discovery socket() failed");sockaddr_in dsa{};dsa.sin_family=AF_INET;dsa.sin_addr.s_addr=htonl(INADDR_LOOPBACK);dsa.sin_port=0;require(::bind(dfd,(sockaddr*)&dsa,sizeof(dsa))==0,"discovery bind() failed");sl=sizeof(dsa);require(::getsockname(dfd,(sockaddr*)&dsa,&sl)==0,"discovery getsockname() failed");const auto dport=ntohs(dsa.sin_port);
    std::thread discoveryServer([&](){char b[8192];sockaddr_in peer{};socklen_t pl=sizeof(peer);auto n=recvfrom(dfd,b,sizeof(b),0,(sockaddr*)&peer,&pl);require(n>0,"recvfrom() failed");const char*resp="SIP/2.0 200 OK\r\nServer: DiscoveryPBX\r\nContent-Length: 0\r\n\r\n";sendto(dfd,resp,std::strlen(resp),0,(sockaddr*)&peer,pl);});
    auto discovered=PbxAudit::discoverIpv4Cidr("127.0.0.1/32",dport,AuditTransport::Udp,100,1000);discoveryServer.join();close(dfd);require(discovered.size()==1,"bounded discovery result count mismatch");require(discovered[0].server=="DiscoveryPBX","bounded discovery banner mismatch");
    auto discoveryReport=PbxAudit::report("discover",{}, {}, {},discovered);require(discoveryReport.find("127.0.0.1")!=std::string::npos,"discovery report missing host");

    // Fingerprint a disclosed Asterisk banner and exposed SIP capability.
    int ffd=::socket(AF_INET,SOCK_DGRAM,0);require(ffd>=0,"fingerprint socket() failed");sockaddr_in fsa{};fsa.sin_family=AF_INET;fsa.sin_addr.s_addr=htonl(INADDR_LOOPBACK);fsa.sin_port=0;require(::bind(ffd,(sockaddr*)&fsa,sizeof(fsa))==0,"fingerprint bind() failed");sl=sizeof(fsa);require(::getsockname(ffd,(sockaddr*)&fsa,&sl)==0,"fingerprint getsockname() failed");const auto fport=ntohs(fsa.sin_port);
    std::thread fingerprintServer([&](){char b[8192];sockaddr_in peer{};socklen_t pl=sizeof(peer);auto n=recvfrom(ffd,b,sizeof(b),0,(sockaddr*)&peer,&pl);require(n>0,"fingerprint recvfrom() failed");const char*resp="SIP/2.0 200 OK\r\nServer: Asterisk/20.5.0\r\nAllow: INVITE, ACK, OPTIONS, REGISTER\r\nSupported: timer, replaces\r\nContent-Length: 0\r\n\r\n";sendto(ffd,resp,std::strlen(resp),0,(sockaddr*)&peer,pl);});
    auto fp=PbxAudit::fingerprint("127.0.0.1",fport,AuditTransport::Udp,1000);fingerprintServer.join();close(ffd);require(fp.product=="Asterisk","fingerprint product mismatch");require(fp.version=="20.5.0","fingerprint version mismatch");require(fp.toText().find("Supported: timer")!=std::string::npos,"fingerprint capability missing");
    PbxFingerprint unknown;unknown.product="Unknown SIP/PBX platform";require(PbxAudit::vulnerabilityLookupReport(unknown).find("skipped")!=std::string::npos,"unknown product should skip broad CVE lookup");

    // Offline correlation test: fake curl returns one NVD CVE and writes one
    // Exploit-DB metadata row. This validates parsing without external network access.
    namespace fs=std::filesystem;const auto vbase=fs::temp_directory_path()/"sipher-vuln-lookup-test";fs::remove_all(vbase);fs::create_directories(vbase/"bin");fs::create_directories(vbase/"home");
    const auto curlPath=vbase/"bin"/"curl";{std::ofstream sh(curlPath);sh<<"#!/bin/sh\n"
        <<"out=''\nprev=''\nnvd=0\nfor a in \"$@\"; do\n  if [ \"$prev\" = '-o' ]; then out=\"$a\"; fi\n  case \"$a\" in *services.nvd.nist.gov*) nvd=1;; esac\n  prev=\"$a\"\ndone\n"
        <<"if [ -n \"$out\" ]; then mkdir -p \"$(dirname \"$out\")\"; cat >\"$out\" <<'CSV'\nid,file,description,date,author,type,platform,port,codes\n99999,exploits/linux/remote/99999.txt,Asterisk 20.5.0 test issue,2026-01-01,Tester,remote,linux,5060,CVE-2026-9999\nCSV\nexit 0\nfi\n"
        <<"if [ \"$nvd\" = 1 ]; then printf '%s\\n' '{\"vulnerabilities\":[{\"cve\":{\"id\":\"CVE-2026-9999\",\"descriptions\":[{\"lang\":\"en\",\"value\":\"Asterisk 20.5.0 test vulnerability\"}],\"metrics\":{\"cvssMetricV31\":[{\"cvssData\":{\"baseSeverity\":\"HIGH\"}}]}}}]}'; exit 0; fi\nexit 1\n";}
    require(::chmod(curlPath.c_str(),0700)==0,"chmod fake curl failed");const char*oldPath=std::getenv("PATH");const char*oldHome=std::getenv("HOME");const std::string savedPath=oldPath?oldPath:"";const std::string savedHome=oldHome?oldHome:"";const std::string testPath=(vbase/"bin").string()+":"+savedPath;require(setenv("PATH",testPath.c_str(),1)==0,"setenv PATH failed");require(setenv("HOME",(vbase/"home").c_str(),1)==0,"setenv HOME failed");
    auto correlation=PbxAudit::vulnerabilityLookupReport(fp,5);require(correlation.find("CVE-2026-9999")!=std::string::npos,"NVD CVE correlation missing");require(correlation.find("EDB-99999")!=std::string::npos,"Exploit-DB metadata correlation missing");
    if(savedPath.empty())unsetenv("PATH");else setenv("PATH",savedPath.c_str(),1);if(savedHome.empty())unsetenv("HOME");else setenv("HOME",savedHome.c_str(),1);fs::remove_all(vbase);

    // Method policy audit sends a fixed four-request set and never places a call.
    int mfd=::socket(AF_INET,SOCK_DGRAM,0);require(mfd>=0,"method socket() failed");sockaddr_in msa{};msa.sin_family=AF_INET;msa.sin_addr.s_addr=htonl(INADDR_LOOPBACK);msa.sin_port=0;require(::bind(mfd,(sockaddr*)&msa,sizeof(msa))==0,"method bind() failed");sl=sizeof(msa);require(::getsockname(mfd,(sockaddr*)&msa,&sl)==0,"method getsockname() failed");const auto mport=ntohs(msa.sin_port);
    std::thread methodServer([&](){for(int i=0;i<4;++i){char b[8192];sockaddr_in peer{};socklen_t pl=sizeof(peer);auto n=recvfrom(mfd,b,sizeof(b),0,(sockaddr*)&peer,&pl);require(n>0,"recvfrom() failed");const char*resp="SIP/2.0 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n";sendto(mfd,resp,std::strlen(resp),0,(sockaddr*)&peer,pl);}});
    auto methods=PbxAudit::methodAudit("127.0.0.1",mport,AuditTransport::Udp,1000);methodServer.join();close(mfd);require(methods.size()==4,"method audit result count mismatch");for(const auto&m:methods)require(m.statusCode==405,"method audit did not receive 405");

    // Digest policy/oracle check: same nonce across repeated challenges should
    // be reported for review, without attempting any credential recovery.
    int afd=::socket(AF_INET,SOCK_DGRAM,0);require(afd>=0,"auth socket() failed");
    sockaddr_in asa{};asa.sin_family=AF_INET;asa.sin_addr.s_addr=htonl(INADDR_LOOPBACK);asa.sin_port=0;require(::bind(afd,(sockaddr*)&asa,sizeof(asa))==0,"auth bind() failed");
    sl=sizeof(asa);require(::getsockname(afd,(sockaddr*)&asa,&sl)==0,"auth getsockname() failed");const auto aport=ntohs(asa.sin_port);
    std::thread authServer([&](){for(int i=0;i<3;++i){char b[8192];sockaddr_in peer{};socklen_t pl=sizeof(peer);auto n=recvfrom(afd,b,sizeof(b),0,(sockaddr*)&peer,&pl);require(n>0,"recvfrom() failed");const char*resp="SIP/2.0 401 Unauthorized\r\nWWW-Authenticate: Digest realm=\"lab\", nonce=\"same-nonce\", algorithm=MD5, qop=\"auth\"\r\nContent-Length: 0\r\n\r\n";sendto(afd,resp,std::strlen(resp),0,(sockaddr*)&peer,pl);}});
    auto oracle=PbxAudit::digestOracleAudit("127.0.0.1","1000",aport,AuditTransport::Udp,1000);authServer.join();close(afd);
    require(oracle.size()==3,"digest oracle result count mismatch");bool nonceReview=false;for(const auto&f:oracle[1].findings)if(f.title=="Digest nonce reused")nonceReview=true;require(nonceReview,"digest nonce reuse finding missing");

    // Parser-abuse simulation is intentionally tiny and bounded. A healthy
    // test server can reject each edge case without being flooded.
    int pfd=::socket(AF_INET,SOCK_DGRAM,0);require(pfd>=0,"parser socket() failed");sockaddr_in psa{};psa.sin_family=AF_INET;psa.sin_addr.s_addr=htonl(INADDR_LOOPBACK);psa.sin_port=0;require(::bind(pfd,(sockaddr*)&psa,sizeof(psa))==0,"parser bind() failed");sl=sizeof(psa);require(::getsockname(pfd,(sockaddr*)&psa,&sl)==0,"parser getsockname() failed");const auto pport=ntohs(psa.sin_port);
    std::thread parserServer([&](){for(int i=0;i<5;++i){char b[8192];sockaddr_in peer{};socklen_t pl=sizeof(peer);auto n=recvfrom(pfd,b,sizeof(b),0,(sockaddr*)&peer,&pl);require(n>0,"recvfrom() failed");const char*resp="SIP/2.0 400 Bad Request\r\nContent-Length: 0\r\n\r\n";sendto(pfd,resp,std::strlen(resp),0,(sockaddr*)&peer,pl);}});
    auto parser=PbxAudit::parserAbuseAudit("127.0.0.1",pport,AuditTransport::Udp,1000);parserServer.join();close(pfd);require(parser.size()==5,"parser audit result count mismatch");for(const auto&x:parser)require(x.statusCode==400,"parser audit did not receive 400");

    // Low-volume resilience test remains capped and sequential.
    int rfd=::socket(AF_INET,SOCK_DGRAM,0);require(rfd>=0,"resilience socket() failed");sockaddr_in rsa{};rsa.sin_family=AF_INET;rsa.sin_addr.s_addr=htonl(INADDR_LOOPBACK);rsa.sin_port=0;require(::bind(rfd,(sockaddr*)&rsa,sizeof(rsa))==0,"resilience bind() failed");sl=sizeof(rsa);require(::getsockname(rfd,(sockaddr*)&rsa,&sl)==0,"resilience getsockname() failed");const auto rport=ntohs(rsa.sin_port);
    std::thread rateServer([&](){for(int i=0;i<3;++i){char b[8192];sockaddr_in peer{};socklen_t pl=sizeof(peer);auto n=recvfrom(rfd,b,sizeof(b),0,(sockaddr*)&peer,&pl);require(n>0,"recvfrom() failed");const char*resp="SIP/2.0 200 OK\r\nContent-Length: 0\r\n\r\n";sendto(rfd,resp,std::strlen(resp),0,(sockaddr*)&peer,pl);}});
    auto rate=PbxAudit::resilienceAudit("127.0.0.1",rport,AuditTransport::Udp,3,100,1000);rateServer.join();close(rfd);require(rate.size()==3,"resilience audit result count mismatch");for(const auto&x:rate)require(x.statusCode==200,"resilience audit did not receive 200");
#endif
    std::cout<<"pbx-audit-test ok\n";return 0;
}
