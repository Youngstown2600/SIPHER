# S.I.P.H.E.R. 2.0 PBX Security Audit

> **AUTHORIZED SYSTEMS ONLY — USE AT YOUR OWN RISK.** S.I.P.H.E.R. transmits active SIP probes, including bounded parser-abuse and rate-resilience simulations. Only test systems you own or have explicit authorization to assess. Tests can trigger IDS/IPS, rate limits, alarms, or service protections.

## Real-world attack-scenario simulation

`audit-scenario` / **ATTACK SCENARIO** chains the same early phases commonly seen in a real PBX assessment: service reconnaissance, SIP method-policy discovery, Digest/account-response oracle checks, parser-edge behavior, and a bounded availability/rate-resilience test. The implementation intentionally stops short of password cracking, destructive denial-of-service, persistent registration takeover, exploit payload execution, or automated compromise.

The parser corpus contains only five small edge-case requests (CSeq/request mismatch, non-standard Via branch, duplicate zero Content-Length, unknown URI scheme, and a modest 512-byte benign extension header). The rate-resilience test is sequential, defaults to 10 OPTIONS requests at a 150 ms interval, and is hard-capped at 20 requests per run.

> **AUTHORIZED SYSTEMS ONLY — USE AT YOUR OWN RISK.** PBX Audit transmits active SIP probes. Only test systems you own or have explicit authorization to assess.

PBX Audit is intended for VoIP engineering, configuration review, and bounded security validation. It does not include password cracking, destructive crash payloads, denial-of-service flooding, or automated exploit/takeover chains.

## Tests

- **Service Probe** — sends one SIP `OPTIONS` request and records status, latency, `Server`, `User-Agent`, `Allow`, `Supported`, and response/request size ratio.
- **Auth Policy** — sends an unauthenticated `REGISTER` using a non-matching audit Contact and `Expires: 0`. It checks whether authentication is challenged and reviews Digest realm, qop, and algorithm signals.
- **Digest Oracle / Nonce** — repeats the bounded auth-policy challenge and compares it with a random-invalid control account. It reports response-code differences that may disclose account validity and flags repeated nonce values for review. It does not attempt credential recovery.
- **Extension Differential Audit** — sends rate-limited `OPTIONS` requests to an explicit numeric range. A run is capped at 100 entries and a minimum 100 ms inter-probe delay. Results are hints, not proof of account existence.
- **Compliance Audit** — sends a small fixed set of bounded SIP requests covering unknown-method handling, `Max-Forwards: 0`, an unsupported `Require` option, and a registration-policy control.
- **TLS Audit** — invokes the system OpenSSL client for a short SIP-TLS handshake summary.
- **Full Safe Audit** — combines capability checks on UDP/TCP, Digest/oracle analysis, bounded compliance probes, and a short TLS check.

## CLI

```text
audit-warning
audit-probe <host> [port] [udp|tcp]
audit-auth <host> <user> [port] [udp|tcp]
audit-oracle <host> <user> [port] [udp|tcp]
audit-ext <host> <first> <last> [port] [udp|tcp]
audit-compliance <host> [port] [udp|tcp]
audit-tls <host> [port]
audit-full <host> [port] [udp|tcp]
audit-save <file>
```

## Interpretation

A warning is an engineering lead, not automatic proof of a vulnerability. SIP stacks legitimately vary in authentication challenges, error codes, nonce lifetime, exposed methods, and transport policy. Confirm findings against the PBX/SBC configuration and vendor guidance before remediation.

## Fingerprint and public vulnerability correlation

`audit-fingerprint` performs a best-effort SIP banner/capability fingerprint. `audit-vulns` correlates the detected product/version with NIST NVD CVE metadata and the official Exploit-DB metadata index. These features do not execute exploits, recover credentials, or automatically compromise a target. Banner/version matches are hints and must be verified against the actual installed PBX/module versions.
