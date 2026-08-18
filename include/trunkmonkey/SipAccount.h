#pragma once
#include <pjsua2.hpp>
namespace trunkmonkey {
class SipEngine; class Logger;
class SipAccount final:public pj::Account {
public:
    SipAccount(SipEngine& engine,Logger& logger);
    ~SipAccount()override;
    void onRegState(pj::OnRegStateParam& prm)override;
    void onIncomingCall(pj::OnIncomingCallParam& prm)override;
private:
    SipEngine& engine_; Logger& logger_;
};
}
