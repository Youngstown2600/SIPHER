#include "trunkmonkey/CaptureManager.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
int main(){try{
    const auto interfaces=trunkmonkey::CaptureManager::availableInterfaces();
    const auto hint=trunkmonkey::CaptureManager::permissionHint();
    if(hint.empty())throw std::runtime_error("capture permission hint is empty");
    trunkmonkey::CallSnapshot c;c.localRtpAddress="10.0.0.10:4000";c.remoteRtpAddress="10.0.0.20:5000";c.localRtcpAddress="10.0.0.10:4001";c.remoteRtcpAddress="10.0.0.20:5001";
    const auto args=trunkmonkey::CaptureManager::wiresharkDecodeArguments("call.pcapng",c);
    const auto has=[&](const std::string&v){return std::find(args.begin(),args.end(),v)!=args.end();};
    if(!has("udp.port==4000,rtp")||!has("udp.port==5000,rtp")||!has("udp.port==4001,rtcp")||!has("udp.port==5001,rtcp"))throw std::runtime_error("Wireshark RTP/RTCP Decode As arguments are incomplete");
    const auto sipArgs=trunkmonkey::CaptureManager::wiresharkSipDecodeArguments("sip.pcapng",5060);
    const auto sipHas=[&](const std::string&v){return std::find(sipArgs.begin(),sipArgs.end(),v)!=sipArgs.end();};
    if(!sipHas("udp.port==5060,sip")||!sipHas("tcp.port==5060,sip"))throw std::runtime_error("Wireshark SIP Decode As arguments are incomplete");
    std::cout<<"capture tool="<<trunkmonkey::CaptureManager::captureTool()<<" wireshark="<<trunkmonkey::CaptureManager::wiresharkTool()<<" interfaces="<<interfaces.size()<<" auto-rtp-args=ok auto-sip-args=ok\n";
    return 0;
}catch(const std::exception&e){std::cerr<<"capture manager test failed: "<<e.what()<<'\n';return 1;}}
