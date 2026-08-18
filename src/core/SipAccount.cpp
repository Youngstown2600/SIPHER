#include "trunkmonkey/SipAccount.h"
#include "trunkmonkey/Logger.h"
#include "trunkmonkey/SipEngine.h"

namespace trunkmonkey {
SipAccount::SipAccount(SipEngine& engine, Logger& logger)
    : engine_(engine), logger_(logger)
{
}

SipAccount::~SipAccount()
{
    // PJSUA2 recommends that a derived Account shuts down at the beginning of
    // its destructor so callbacks cannot race the derived object teardown.
    if (isValid()) {
        shutdown();
    }
}

void SipAccount::onRegState(pj::OnRegStateParam& param)
{
    try {
        const auto info = getInfo();
        engine_.onRegistrationState(info.regIsActive, static_cast<int>(param.code), param.reason);
    } catch (const pj::Error& error) {
        logger_.warn("Registration callback: " + error.info());
    }
}

void SipAccount::onIncomingCall(pj::OnIncomingCallParam& param)
{
    logger_.info("Incoming SIP call id=" + std::to_string(param.callId));
    engine_.onIncomingCall(param.callId);
}
} // namespace trunkmonkey
