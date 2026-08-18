# S.I.P.H.E.R. Windows Feature Parity

Both portable Windows targets are built from the same core as S.I.P.H.E.R. 1.0.0 and include:

- SIP registration and outbound/inbound call handling
- Answer, hang up, hold/resume, mute/unmute, DTMF and active-call controls
- CLI `/command` call-control interface and guided Operator Mode
- Responsive CLI pages and resize handling
- Security Audit page/tab
- PBX/SBC service discovery and SIP fingerprinting
- PBX product/version/capability/component findings from remotely disclosed SIP information
- NIST NVD CVE correlation for identified products/versions
- Exploit-DB metadata correlation/cache
- SIP method/authentication policy checks
- Digest challenge/oracle checks
- bounded parser/compliance probes and bounded rate-resilience tests
- TLS inspection via the portable OpenSSL helper
- RTP endpoint/statistics diagnostics, codec data, jitter/loss/MOS/R-factor reporting where supplied by the media stack
- SIP, RTP, and combined-call packet capture paths
- SIP ladder/call-flow view and diagnostic report export
- queue/call-blast testing, destination/caller-ID lists, WAV/MP3 prompt injection via portable FFmpeg
- multiple SIP profiles and registration history
- audio device enumeration and separate capture/playback selection
- microphone/speaker testing
- GUI and CLI
- all 26 S.I.P.H.E.R. themes
- `S.I.P.H.E.R. By GITSC` branding
- portable folder-local config, state, logs, CVE cache, and temporary files

## Windows-specific behavior

- **Windows 10/11:** native console VT rendering is used when available; `pktmon` can be used as the PCAP fallback.
- **Windows 7:** CLI functionality remains the same, but native cmd.exe rendering falls back when VT is unavailable. Packet capture requires a compatible installed capture driver because Windows 7 has no `pktmon`.

No SIP-call, PBX-audit, CVE-correlation, RTP-analysis, queue-test, profile, or GUI feature is intentionally removed from either Windows target.
