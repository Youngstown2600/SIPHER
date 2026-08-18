#pragma once
#include <cstdint>
#include <string>

namespace trunkmonkey {
enum class Transport { Udp, Tcp, Tls };
enum class IdentityMode { From, Pai, Rpid, FromAndPai };

struct SipProfile {
    std::string name{"Default"};
    std::string sipDomain;
    std::string registrar;
    std::string username;
    std::string authUsername;
    std::string password;
    std::string displayName{"S.I.P.H.E.R."};
    std::string outboundProxy;
    std::string callerIdDomain;
    std::string stunServer;
    Transport transport{Transport::Udp};
    IdentityMode identityMode{IdentityMode::From};
    std::uint16_t localSipPort{5060};
    unsigned registrationExpires{300};
    bool useIce{false};
    bool enableSrtp{false};
};

std::string toString(Transport v);
std::string toString(IdentityMode v);
Transport transportFromString(const std::string& v);
IdentityMode identityModeFromString(const std::string& v);

class ProfileStore {
public:
    static SipProfile defaults();
    static SipProfile loadDraft(const std::string& path);
    static SipProfile load(const std::string& path);
    static void validate(const SipProfile& profile);
    static bool isConfigured(const SipProfile& profile) noexcept;
    static bool createDefaultIfMissing(const std::string& path);
    static void save(const SipProfile& profile, const std::string& path);
};
} // namespace trunkmonkey
