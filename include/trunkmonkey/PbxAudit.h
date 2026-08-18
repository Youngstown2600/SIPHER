#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace trunkmonkey {

enum class AuditTransport { Udp, Tcp };

struct AuditFinding {
    std::string severity;
    std::string title;
    std::string detail;
};

struct AuditResponse {
    std::string target;
    std::uint16_t port{5060};
    AuditTransport transport{AuditTransport::Udp};
    std::string testName;
    int statusCode{0};
    std::string reason;
    std::string server;
    std::string userAgent;
    std::string allow;
    std::string supported;
    std::string authenticate;
    double latencyMs{0.0};
    std::size_t requestBytes{0};
    std::size_t responseBytes{0};
    std::string rawRequest;
    std::string rawResponse;
    std::vector<AuditFinding> findings;
    std::string toText(bool includeRaw=false) const;
};

struct DiscoveryEntry {
    std::string host;
    std::uint16_t port{5060};
    AuditTransport transport{AuditTransport::Udp};
    int statusCode{0};
    std::string reason;
    std::string server;
    double latencyMs{0.0};
};


struct PbxComponent {
    std::string name;
    std::string version;
    std::string evidence;
};

struct PbxFingerprint {
    std::string host;
    std::string vendor;
    std::string product;
    std::string version;
    std::string confidence;
    std::string serverBanner;
    std::string userAgentBanner;
    std::vector<std::string> capabilities;
    std::vector<PbxComponent> components;
    std::vector<std::string> notes;
    std::string toText() const;
};

struct ExtensionAuditEntry {
    std::string extension;
    int statusCode{0};
    std::string reason;
    double latencyMs{0.0};
    std::string assessment;
};

class PbxAudit {
public:
    static constexpr const char* warningText() {
        return "AUTHORIZED SYSTEMS ONLY — USE AT YOUR OWN RISK. S.I.P.H.E.R. transmits active SIP probes, including bounded parser-abuse and rate-resilience simulations. Only test systems you own or have explicit authorization to assess. Tests can trigger IDS/IPS, rate limits, alarms, or service protections.";
    }

    static AuditTransport transportFromString(const std::string& value);
    static std::string transportName(AuditTransport value);

    static AuditResponse serviceProbe(const std::string& host,
                                      std::uint16_t port=5060,
                                      AuditTransport transport=AuditTransport::Udp,
                                      unsigned timeoutMs=1800);

    // Best-effort remote fingerprinting from SIP banners, methods and Supported tags.
    // It only reports components/capabilities actually disclosed by the target.
    static PbxFingerprint fingerprint(const std::string& host,
                                      std::uint16_t port=5060,
                                      AuditTransport transport=AuditTransport::Udp,
                                      unsigned timeoutMs=1800);

    // Correlates a fingerprint with public vulnerability metadata. NVD is queried
    // through its CVE 2.0 API; Exploit-DB correlation uses the official Exploit-DB CSV metadata cache. No exploit code is executed.
    static std::string vulnerabilityLookupReport(const PbxFingerprint& fingerprint,
                                                 unsigned maxResults=12);

    // Bounded IPv4 CIDR discovery. To keep audit traffic deliberate, one run
    // is limited to at most 32 addresses (/27 or narrower) and rate-limited.
    static std::vector<DiscoveryEntry> discoverIpv4Cidr(const std::string& cidr,
                                                        std::uint16_t port=5060,
                                                        AuditTransport transport=AuditTransport::Udp,
                                                        unsigned delayMs=200,
                                                        unsigned timeoutMs=900);

    // Reviews policy for a small fixed set of non-destructive SIP methods.
    // REGISTER/SUBSCRIBE probes use Expires: 0; no call is placed.
    static std::vector<AuditResponse> methodAudit(const std::string& host,
                                                  std::uint16_t port=5060,
                                                  AuditTransport transport=AuditTransport::Udp,
                                                  unsigned timeoutMs=1500);

    // Sends a REGISTER with Expires: 0 and no Authorization. This audits the
    // challenge/policy without creating a persistent registration binding.
    static AuditResponse authenticationAudit(const std::string& host,
                                             const std::string& username,
                                             std::uint16_t port=5060,
                                             AuditTransport transport=AuditTransport::Udp,
                                             unsigned timeoutMs=1800);

    // Compares repeated Digest challenges plus a random-invalid control account.
    // This is a policy/oracle test only; it does not attempt to recover credentials.
    static std::vector<AuditResponse> digestOracleAudit(const std::string& host,
                                                        const std::string& username,
                                                        std::uint16_t port=5060,
                                                        AuditTransport transport=AuditTransport::Udp,
                                                        unsigned timeoutMs=1800);

    // Rate-limited differential extension audit. Range is intentionally capped.
    static std::vector<ExtensionAuditEntry> extensionAudit(const std::string& host,
                                                           unsigned first,
                                                           unsigned last,
                                                           std::uint16_t port=5060,
                                                           AuditTransport transport=AuditTransport::Udp,
                                                           unsigned delayMs=250,
                                                           unsigned timeoutMs=1200);

    // Bounded standards/compliance probes only: no crash payloads or exploit chains.
    static std::vector<AuditResponse> complianceAudit(const std::string& host,
                                                      std::uint16_t port=5060,
                                                      AuditTransport transport=AuditTransport::Udp,
                                                      unsigned timeoutMs=1500);

    // Small malformed/edge-case SIP corpus that mimics parser-abuse reconnaissance
    // without crash payloads, shellcode, or exploit chains.
    static std::vector<AuditResponse> parserAbuseAudit(const std::string& host,
                                                       std::uint16_t port=5060,
                                                       AuditTransport transport=AuditTransport::Udp,
                                                       unsigned timeoutMs=1500);

    // Sequential, rate-capped OPTIONS burst used to observe throttling, loss,
    // latency growth, and availability protections. Hard-capped at 20 requests.
    static std::vector<AuditResponse> resilienceAudit(const std::string& host,
                                                      std::uint16_t port=5060,
                                                      AuditTransport transport=AuditTransport::Udp,
                                                      unsigned requests=10,
                                                      unsigned delayMs=150,
                                                      unsigned timeoutMs=1000);

    // Real-world-style scenario chain: reconnaissance, method policy, auth
    // oracle behavior, parser edge cases, and bounded rate resilience.
    // Deliberately excludes password cracking, destructive DoS, and takeover.
    static std::vector<AuditResponse> attackScenarioAudit(const std::string& host,
                                                          const std::string& username,
                                                          std::uint16_t port=5060,
                                                          AuditTransport transport=AuditTransport::Udp,
                                                          unsigned timeoutMs=1500);

    // Uses the system OpenSSL client for a short TLS handshake summary.
    static std::string tlsAudit(const std::string& host,std::uint16_t port=5061,unsigned timeoutMs=5000);

    static std::string report(const std::string& title,
                              const std::vector<AuditResponse>& responses,
                              const std::vector<ExtensionAuditEntry>& extensions={},
                              const std::string& tlsSummary={},
                              const std::vector<DiscoveryEntry>& discovery={});
    static void saveReport(const std::string& path,const std::string& text);
};

} // namespace trunkmonkey
