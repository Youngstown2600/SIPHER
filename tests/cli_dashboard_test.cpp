#include "CliDashboard.h"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
namespace { void check(bool c,const char* e){if(!c)throw std::runtime_error(std::string("CHECK failed: ")+e);} }
int main(){try{
#if defined(_WIN32)
 _putenv_s("TRUNKMONKEY_FORCE_DASHBOARD","1");_putenv_s("NO_COLOR","1");_putenv_s("COLUMNS","150");_putenv_s("LINES","50");
#else
 check(setenv("TRUNKMONKEY_FORCE_DASHBOARD","1",1)==0,"force dashboard");check(setenv("NO_COLOR","1",1)==0,"no color");check(setenv("COLUMNS","150",1)==0,"columns");check(setenv("LINES","50",1)==0,"lines");
#endif
 trunkmonkey::cli::CliDashboard d;check(d.enabled(),"dashboard enabled");trunkmonkey::cli::DashboardState s;s.profile.name="Test";s.profile.username="1001";s.profile.displayName="TrunkMonkey User";s.profile.sipDomain="pbx.example.net";s.profile.registrar="sip:pbx.example.net";s.profilePath="/home/test/.config/trunkmonkey/profile.conf";s.registrationText="Registered (200 OK)";s.registered=true;
 trunkmonkey::CallSnapshot c;c.id=0;c.state="CONFIRMED";c.connected=true;c.remoteUri="sip:3305551212@pbx.example.net";c.remoteRtpAddress="192.0.2.20:40002";c.sourceRtpAddress=c.remoteRtpAddress;c.localRtpAddress="192.0.2.10:40000";c.codecName="PCMU";c.codecClockRate=8000;s.calls.push_back(c);s.focusCallId=0;
 trunkmonkey::SipTraceEntry e;e.direction=trunkmonkey::SipDirection::Sent;e.label="INVITE";e.cseq=1;s.focusTrace.push_back(e);s.notices.push_back({"Dashboard test ready",trunkmonkey::cli::DashboardNotice::Level::Success});s.engineLogPath="/tmp/trunkmonkey-test/pjsip-engine.log";s.engineLogLines={"PJSIP test line 1","PJSIP test line 2"};
 for(int page=1;page<=9;++page){s.page=static_cast<trunkmonkey::cli::DashboardPage>(page);std::ostringstream o;d.render(s,o);const auto t=o.str();check(t.find("Alt+1 Main")!=std::string::npos,"page bar");check(t.find("select> ")!=std::string::npos,"prompt");if(page==1){check(t.find("S.I.P.H.E.R.")!=std::string::npos,"brand");check(t.find("[ REGISTRATION ]")!=std::string::npos,"registration");check(t.find("[ ACTIVITY ]")!=std::string::npos,"activity");}if(page==2){check(t.find("[ SIP LOG")!=std::string::npos,"sip page");check(t.find("INVITE")!=std::string::npos,"invite");}if(page==3){check(t.find("MEDIA / RTP DIAGNOSTICS")!=std::string::npos,"media page");check(t.find("192.0.2.20:40002")!=std::string::npos,"rtp");}if(page==4)check(t.find("[ ACTIVE CALLS ]")!=std::string::npos,"calls page");if(page==5)check(t.find("PBX SECURITY / VULNERABILITY AUDIT")!=std::string::npos,"security page");if(page==6)check(t.find("SIP PROFILE / CONFIG")!=std::string::npos,"profile page");if(page==7)check(t.find("[ OPERATOR MENU ]")!=std::string::npos,"help page");if(page==8){check(t.find("PJSIP ENGINE LOG")!=std::string::npos,"engine page");check(t.find("PJSIP test line 2")!=std::string::npos,"engine log line");}if(page==9)check(t.find("QUEUE TEST / ACTIVITY")!=std::string::npos,"queue page");}
 check(trunkmonkey::cli::CliDashboard::themeNames().size()==26,"all 26 themes retained");
#if defined(_WIN32)
 _putenv_s("COLUMNS","80");
#else
 check(setenv("COLUMNS","80",1)==0,"narrow columns");
#endif
 s.page=trunkmonkey::cli::DashboardPage::Main;std::ostringstream narrow;d.render(s,narrow);check(narrow.str().find("[ OPERATOR MENU ]")!=std::string::npos,"narrow operator menu visible");
#if defined(_WIN32)
 _putenv_s("LINES","25");
#else
 check(setenv("LINES","25",1)==0,"classic tty lines");
#endif
 std::ostringstream tty80;d.render(s,tty80);check(tty80.str().find("██████")!=std::string::npos,"80-column block logo visible");check(tty80.str().find("▓█ S.I.P.H.E.R. █▓")!=std::string::npos,"top-left brand badge visible");
 std::cout<<"CLI paged dashboard tests passed\n";return 0;
}catch(const std::exception& x){std::cerr<<"CLI dashboard test failed: "<<x.what()<<'\n';return 1;}}
