# S.I.P.H.E.R. 1.0.0 r8 PBX / SIP Security Audit

> **AUTHORIZED SYSTEMS ONLY — USE AT YOUR OWN RISK.** S.I.P.H.E.R. transmits active SIP probes, including bounded parser-normalization and rate-resilience simulations. Only assess systems you own or have explicit authorization to test. These checks can trigger IDS/IPS, rate limits, alarms, or service protections.

## Recommended: Automated Chained Audit

r8 adds a guided **Automated Chained Audit**. Instead of running unrelated probes and leaving the operator to correlate them manually, each phase reuses evidence from the previous phase wherever possible and produces one prioritized report.

Pipeline:

1. **Primary service probe** — records SIP status, timing, banners, advertised methods/capabilities, and amplification ratio.
2. **Fingerprint from probe output** — extracts product/version/capability hints without repeating the initial probe.
3. **Method-policy review** — identifies enabled SIP methods and exposed optional feature surface.
4. **Authentication policy** — checks REGISTER challenge behavior and Digest parameters.
5. **Digest/account oracle control** — when a 401/407 challenge is observed, compares the original auth result with a repeated challenge and a random-invalid account control; no password guessing is performed.
6. **Protocol compliance** — evaluates unknown-method handling, Max-Forwards behavior, unsupported Require handling, and registration-policy controls.
7. **Alternate cleartext transport check** — compares UDP/TCP reachability to expose unnecessary transport surface.
8. **Parser normalization** — optional bounded malformed/edge-case corpus intended to identify inconsistent parsing, not crash the target.
9. **Rate resilience** — optional low-volume sequential OPTIONS sample with hard request limits.
10. **Extension differential check** — optional and disabled by default; only runs when the operator explicitly supplies a numeric range, capped at 100 entries.
11. **SIP-TLS posture** — optional handshake summary covering certificate/protocol/cipher indicators.
12. **Public vulnerability metadata correlation** — optional product/version correlation against public metadata; this is a lead for verification, not proof of exploitability.
13. **Risk prioritization** — combines all collected evidence into HIGH/WARN/PASS/INFO counts and deduplicated remediation notes.

### CLI

Use the guided menu:

```text
audit
```

Or run the chained audit directly:

```text
audit-auto <host> [user|-] [port] [udp|tcp] [ext-first ext-last]
```

`audit-full <host> [port] [udp|tcp]` remains as a compatibility alias and now uses the automated pipeline.

### GUI

Open **PBX Audit** and use **RUN AUTOMATED CHAINED AUDIT**. The GUI exposes checkboxes for CVE metadata, parser normalization, bounded rate resilience, SIP-TLS posture, and the optional extension range. Phase progress is shown while the audit runs. Individual audit buttons remain available for focused troubleshooting.

## Security audit features

- **Service/capability probe** — SIP OPTIONS status, latency, banners, Allow/Supported, response/request amplification ratio.
- **Product/version disclosure review** — warns when detailed server/version banners make fingerprinting easier.
- **Private topology disclosure** — reports RFC1918 addresses exposed in an unauthenticated response when the audited target is not itself an RFC1918 literal; useful for reviewing SBC topology hiding/header normalization.
- **Expanded method-surface review** — calls out optional methods such as REFER, MESSAGE, SUBSCRIBE, PUBLISH, and NOTIFY when advertised.
- **Authentication policy** — verifies that REGISTER is challenged and reports Digest realm, qop, and algorithm signals, including explicit SHA-2-class Digest support and legacy/unspecified algorithm warnings.
- **Digest/account-response oracle analysis** — compares bounded challenge/control responses for account-enumeration clues and repeated nonces. No credential recovery.
- **Cleartext transport exposure** — compares UDP and TCP SIP reachability and highlights unnecessarily broad exposure.
- **UDP amplification indicator** — reports unusually large response/request ratios as a defensive anti-abuse lead.
- **Extension differential audit** — opt-in, rate-limited numeric range test, maximum 100 entries; results are hints, not proof of extension existence.
- **Protocol compliance audit** — unknown methods, Max-Forwards: 0, unsupported Require, and registration-policy controls.
- **Parser-normalization audit** — five small edge cases: CSeq/request mismatch, non-standard Via branch, duplicate zero Content-Length, unknown URI scheme, and a modest benign extension header.
- **Bounded rate-resilience audit** — sequential low-volume OPTIONS sampling; no flooding or destructive denial-of-service behavior.
- **SIP-TLS posture** — handshake summary with TLS 1.2/1.3 indicators, legacy TLS/cipher detection, certificate trust + hostname/IP identity verification, and explicit expired/mismatched certificate findings when OpenSSL reports them.
- **Fingerprint + public vulnerability correlation** — product/version metadata correlation for analyst verification; no exploit execution.
- **Small-CIDR discovery** — available as a separate explicitly scoped discovery function so the automated host audit does not silently expand its target scope.
- **Unified prioritized report** — executive posture, evidence, counts, and remediation guidance are combined into one saveable report.
- **Automatic RTP/RTCP Wireshark decoding** — diagnostic capture usability feature retained from r7; negotiated media ports can be opened already mapped to RTP/RTCP dissectors.

## Individual CLI audit commands

```text
audit-warning
audit-probe <host> [port] [udp|tcp]
audit-fingerprint <host> [port] [udp|tcp]
audit-vulns <host> [port] [udp|tcp]
audit-methods <host> [port] [udp|tcp]
audit-auth <host> <user> [port] [udp|tcp]
audit-oracle <host> <user> [port] [udp|tcp]
audit-ext <host> <first> <last> [port] [udp|tcp]
audit-compliance <host> [port] [udp|tcp]
audit-parser <host> [port] [udp|tcp]
audit-rate <host> [port] [udp|tcp]
audit-scenario <host> [user|-] [port] [udp|tcp]
audit-tls <host> [port]
audit-auto <host> [user|-] [port] [udp|tcp] [ext-first ext-last]
audit-full <host> [port] [udp|tcp]
audit-save <file>
```

## Real-world attack-scenario simulation

`audit-scenario` / **ATTACK SCENARIO** retains the deliberately bounded scenario runner for authorized validation. It chains service reconnaissance, method-policy discovery, Digest/account-response checks, parser-edge behavior, and low-volume resilience sampling. It intentionally stops short of password cracking, destructive denial-of-service, persistent registration takeover, exploit payload execution, or automated compromise.

The parser corpus contains only five small requests. The rate-resilience test is sequential, defaults to 10 OPTIONS requests at a 150 ms interval, and is hard-capped at 20 requests per run.

## Interpretation

A warning is an engineering lead, not automatic proof of a vulnerability. SIP stacks legitimately vary in authentication challenges, error codes, nonce lifetime, exposed methods, transport policy, TLS presentation, and response behavior. Confirm findings against the PBX/SBC configuration and vendor guidance before remediation.
