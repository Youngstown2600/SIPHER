# S.I.P.H.E.R. 1.0.0 r9 — Unix/Linux audio output selector — 2026-08-18

- New GUI `Settings -> Audio Output...` control on Unix/Linux.
- New CLI `audio-output <playback-id>` command and guided output-device menu.
- Playback can be switched independently of capture/microphone routing, including during a foreground call.
- Builder revision `sipher-r9-20260818-unix-audio-output`.

# S.I.P.H.E.R. 1.0.0 r8 — Chained Security Audit + FreeBSD pthread Fix

- Fixed the FreeBSD/Clang `pthread_create` linker failure in `tm-capture-manager-test` by linking the standalone CaptureManager test target to CMake `Threads::Threads`.
- Added **Automated Chained Audit** so service evidence flows into fingerprinting, auth/oracle interpretation, transport/TLS checks, vulnerability metadata correlation, and one prioritized report.
- Added HIGH/WARN/PASS/INFO posture counts and deduplicated remediation guidance.
- Added posture checks for detailed banner/version disclosure, private topology-address disclosure, optional SIP method exposure, UDP/TCP cleartext transport surface, Digest algorithm posture, TLS legacy protocol/cipher indicators, and TLS certificate trust/identity validation.
- Made extension differential testing opt-in in the automated workflow and kept bounded parser/rate checks non-destructive.
- Reworked the GUI PBX Audit tab around a primary **RUN AUTOMATED CHAINED AUDIT** action with phase progress and explicit feature toggles.
- Added CLI `audit-auto`, made bare `audit` open the guided audit menu, and retained `audit-full` as a compatibility alias.
- Unified the Windows and Unix PBX audit core so the Windows portable build uses the same chained audit logic with Winsock.
- Retains r7 automatic RTP/RTCP Wireshark decode mapping.

# S.I.P.H.E.R. 1.0.0 r7 — Automatic RTP/RTCP Wireshark Decode

- Adds **OPEN LAST PCAP (AUTO RTP)** to the Qt GUI. After an RTP-only or combined call capture is stopped, S.I.P.H.E.R. launches Wireshark with the selected call's negotiated/observed RTP and RTCP UDP ports supplied as command-line Decode As rules.
- Adds CLI command `pcap-open <id> <file>` for the same behavior.
- RTP ports are forced to Wireshark's `rtp` dissector and RTCP ports to `rtcp`; RTP/RTCP-mux overlaps prefer RTP. If no negotiated media ports are available, S.I.P.H.E.R. falls back to Wireshark's `rtp_udp` heuristic.
- The capture file itself remains a normal PCAP/PCAPNG. No proprietary conversion is performed, so the file remains portable and can still be opened normally in Wireshark/tshark.
- Windows 10/11 uses the same auto-decode workflow. The portable build locates `Wireshark.exe` from PATH or a normal Wireshark installation.

# S.I.P.H.E.R. 1.0.0 r6 — Rebrand

S.I.P.H.E.R. — **SIP Inspection, Protocol Handling, Enumeration & Recon** — By GITSC. This release renames S.a.K. while preserving the same SIP/RTP/PBX feature set, 26 themes, CLI/GUI workflows, and legacy TrunkMonkey configuration compatibility. Executables are now `sipher` and `sipher-gui`. The r6 branding refresh adds the supplied block-terminal S.I.P.H.E.R. logo to the CLI, an adaptive 80x25-style layout, a matching GUI banner/application icon, and a compact top-left CLI brand badge.

# S.I.P.H.E.R. 1.0.0 r4

S.I.P.H.E.R. By GITSC. This release focuses on CLI parity, responsive terminal behavior, security-audit visibility, PBX fingerprinting/public vulnerability correlation, and theme expansion.

- Call commands accept a TrunkMonkey-style leading slash: `/dial`, `/answer`, `/hangup`, `/hangup-all`, `/hold`, `/resume`, `/mute`, `/unmute`, `/dtmf`, `/calls`, and the rest of the advanced parser also accepts `/`.
- New CLI Security Audit page on Alt+5; Profile is Alt+6, Help Alt+7, Engine Log Alt+8, Queue/Activity Alt+9.
- CLI redraws on SIGWINCH while waiting at `select>` and preserves partially typed input across the resize. Console TTYs use an automatically compact layout.
- PBX fingerprinting identifies common SIP/PBX/SBC products and versions from disclosed banners and lists remotely disclosed SIP capabilities.
- Vulnerability correlation uses NIST NVD CVE API 2.0 plus official Exploit-DB CSV metadata. It does not execute exploit code. `NVD_API_KEY` is optional.
- Ten new themes were added to CLI and GUI: Blue Box, Red Box, Beige Box, 2600, WarGames, CRT Green, VT220, Cobalt, Vaporwave, and Stealth.
- Visible branding is `S.I.P.H.E.R. By GITSC`. Existing `trunkmonkey` internal namespaces/config paths remain for backward compatibility.

# S.I.P.H.E.R. 1.0.0 — SIP Inspection, Protocol Handling, Enumeration & Recon

This is a user-interface repackaging of the TrunkMonkey 2.0.0 r20 core. No feature modules or themes were removed. The CLI now defaults to guided Operator Mode with numbered workflows while retaining the original advanced command parser. The GUI is branded S.I.P.H.E.R. By GITSC and keeps the existing compact layout and all diagnostic/security controls.

The installed binary names are `sipher` and `sipher-gui`. Existing `~/.config/trunkmonkey`, `~/.local/state/trunkmonkey`, managed PJSIP, temporary diagnostics, and FreeBSD repair markers are intentionally retained for compatibility with known-good installations.

---

# S.I.P.H.E.R. 2.0.0 — Diagnostics & Security Suite — 2026-08-16
- Unix r19 FreeBSD hotfix: explicitly includes `<netinet/in.h>` in the PBX audit implementation and test harness so native FreeBSD exposes IPv4 socket types/constants during compilation.

- Version: `2.0.0`; SIP User-Agent: `S.I.P.H.E.R./2.0.0 Diagnostics & Security Suite`; builder revision: `unix-r18-20260816-attack-sim`.
- Adds live RTP/RTCP engineering statistics, estimated R-factor/MOS, SIP ladder view, and exportable call reports.
- Queue/blast tests can inject WAV/MP3 and other ffmpeg-readable audio; the builder installs/checks ffmpeg on Linux and FreeBSD.
- Adds PBX Audit for authorized systems: service/capability probing, rate-limited extension differential checks, authentication-policy review, Digest nonce/account-oracle checks, bounded SIP compliance probes, UDP response-ratio review, TLS inspection, and private reports.
- Adds a real-world-style **Attack Scenario** that chains reconnaissance, method-policy checks, Digest/account-response oracle analysis, a five-case parser-edge corpus, and a hard-capped sequential rate-resilience burst. Parser/rate tests are deliberately bounded and report IDS/rate-limit/service-protection behavior without password cracking or takeover automation.
- PBX Audit warning is displayed in CLI/GUI and reports: only test systems you own or are explicitly authorized to assess. No password cracking, destructive crash payloads, unbounded flooding, or automated takeover is included. Low-volume rate-resilience simulation is capped at 20 sequential requests per run.
- Retains the proven FreeBSD ALC236/VREF80 audio repair signature, dedicated headset-mic routing, Linux capability setup, FreeBSD BPF/devfs capture permissions, compact GUI, themes, and dashboard-safe engine logging.

# S.I.P.H.E.R. 1.0 — Stable Release Notes — 2026-08-16

## Unix r15 GUI/audio refresh

- Builder revision `unix-r15-20260816-gui-refresh`; application remains S.I.P.H.E.R. 1.0.0.
- Compact 860x560 GUI, consolidated Main tab, visible/File Exit controls, press-and-hold DTMF pad, and refreshed monkey branding.
- FreeBSD headset microphone auto-selection is more defensive while preserving the verified ALC236/VREF80 repair path.
- Packet-capture setup, RTP/SIP diagnostics, CLI engine-log page, themes, and all r14 stable fixes remain included.

## Unix r14 stable release

- Promotes the application, GUI, CLI, package, and SIP User-Agent to **S.I.P.H.E.R. 1.0 / 1.0.0**.
- Builder revision `unix-r14-20260816-stable`.
- Linux packet capture setup installs/uses `tcpdump` or `dumpcap` and configures `CAP_NET_RAW` + `CAP_NET_ADMIN` on the capture helper; S.I.P.H.E.R. itself remains unprivileged.
- FreeBSD packet capture setup creates a persistent, per-user `devfs` rule for `/dev/bpf*`, preserving an existing resolvable ruleset and backing up system files before changes.
- Adds standalone `./build.sh --configure-capture` repair/setup mode.
- Builder retains the verified FreeBSD ALC236 headset-mic repair path: it only applies the `init_clear=1` + `ivref80` correction when the exact known hardware/pin signature is detected, backs up files first, and verifies NID25 reaches pin-control `0x24`. Unknown audio hardware is diagnosed but not rewritten.
- PJSIP console logging is redirected away from the interactive CLI into private `/tmp/trunkmonkey-<uid>/pjsip-engine.log`; S.I.P.H.E.R.'s asynchronous logger is also silenced on the dashboard so calls cannot overwrite the interface.
- Adds an **Alt+8 Engine Log** page with PageUp/PageDown and `log-up`, `log-down`, `log-tail` navigation.
- GUI opens at a smaller 920x620 footprint (720x500 minimum) and uses horizontally scrollable call tables rather than forcing an oversized main window.
- CLI and GUI theme collections now include Hacker, Matrix, Phosphor, Midnight, Amber, Ice, Solarized, Dracula, Nord, Cyberpunk, Blood Moon, Ocean, Retro Blue, Monochrome, and classic/system variants.
- Restores a compact monkey mark/header in the CLI.

## Unix r13 audio routing


- Builder revision `unix-r13-20260816-audio-routing`: adds PJSIP audio device IDs/names to diagnostics, ignores commented HDA hints, detects multiple FreeBSD capture paths, and recognizes the verified ALC236 VREF80 headset-mic state.
- SIP startup now enumerates/logs PJSIP audio devices and independently selects capture/playback. On FreeBSD, when exactly one capture-only device exists, S.I.P.H.E.R. prefers it so a dedicated headset mic is not silently replaced by the default internal mic.
- Numeric runtime overrides are available through `S.I.P.H.E.R._CAPTURE_DEVICE` and `S.I.P.H.E.R._PLAYBACK_DEVICE`.
- The r13 builder diagnostics were non-destructive; r14 adds only a narrowly signature-matched, backed-up ALC236 repair for the exact verified failure mode while leaving unknown hardware advisory-only.

# S.I.P.H.E.R. 1.0 — Audio preflight / FreeBSD diagnostics — 2026-08-15

- Bumped application/package version to `1.0.0` and builder revision to `unix-r12-20260815-audio-preflight`.
- Added automatic non-destructive audio preflight for CLI/GUI builds plus standalone `./build.sh --audio-diagnose`.
- FreeBSD preflight reports playback/capture PCM availability, default PCM unit, HDA codec descriptions, `snd_hda` association errors, runtime pin overrides, persistent HDA pin hints, and PulseAudio defaults when present.
- Explicitly warns when no capture device is visible because PJSIP may fail calls with `PJMEDIA_EAUD_NODEFDEV`.
- PJSIP validation now performs runtime audio-device enumeration after the static-link probe and warns when PJSIP itself sees no input or output device.
- Diagnostics are advisory only: the builder never auto-edits `/boot/device.hints`, HDA NID mappings, mixer levels, or user audio-server settings.
- Retains all Unix r11 PJSIP ABI/I/O-queue hardening, FreeBSD base-Clang/libc++ enforcement, and LOCALBASE static-link handling.


## Unix r9 FreeBSD bootstrap fix

- Accepts a clean PJSIP 2.17 source tree even when `.git` is absent (for example a release archive or an older extracted `third_party/pjproject`).
- Validates non-Git PJSIP sources using `version.mak` and core source-tree markers instead of aborting simply because the directory is not a Git checkout.
- If S.I.P.H.E.R.'s managed non-Git PJSIP tree contains stale generated configure state, the bootstrap safely replaces it with a clean 2.17 checkout and restores the old tree if the download fails.
- FreeBSD now passes PJSIP's supported `--disable-libwebrtc` configure option directly, avoiding the legacy WebRTC AEC/SSE2 linker failure while retaining external PortAudio audio support.
- Local PJSIP compatibility stamp bumped to v5 so older FreeBSD builds are rebuilt automatically.

## CLI dashboard refresh — Unix r7

The real `sipher` now uses the mockup-style terminal dashboard on interactive Linux/FreeBSD terminals. It includes the operator header, account/registration panels, active-call table, selected-call RTP/SIP diagnostics, recent SIP signals, activity log, quick commands, and a persistent `select>` prompt. Wide terminals use two columns and narrower terminals collapse automatically. The implementation uses ANSI terminal control sequences and does not add an ncurses dependency.

The dashboard renderer has a standalone release-mode test so its major panels and SIP/RTP fields are validated without requiring PJSIP or Qt.

- Unix builder now detects source trees physically moved into desktop Trash and automatically scrubs stale generated PJSIP GNU-build state before reconfiguration.
# S.I.P.H.E.R. 1.0 — Test Notes

## Diagnostic refresh

This repack adds the requested single-call troubleshooting tools before the first live beta test:

- Live per-call SIP TX/RX dialog logging.
- Full raw SIP message viewer.
- INVITE versus **RE-INVITE** classification using in-dialog `To` tags, with responses associated by CSeq within the SIP Call-ID.
- Readable raw SIP trace recording to disk.
- SIP PCAP capture through `dumpcap`, with `tcpdump` fallback.
- RTP/RTCP PCAP capture using the selected call's negotiated/observed media ports.
- Main Phone workspace displays SIP Call-ID, negotiated remote RTP target, observed RTP source, local RTP endpoint, and codec/rate.
- Diagnostics are deliberately limited to calls created as normal `Phone` calls in Beta 0.2. Queue-test calls remain independent and visible but do not expose the detailed trace/capture controls yet.

The SIP wire monitor is a read-only PJSIP endpoint module. Early dialog messages are temporarily buffered by SIP Call-ID so the initial INVITE can be attached after PJSUA2 finishes creating the call object.

Packet capture is external by design: S.I.P.H.E.R. starts `dumpcap` when available or `tcpdump` otherwise. It never auto-escalates privileges from the running GUI/CLI.

## Builder refresh

The package retains the WaffleHouse-style top-level `build.sh` workflow:

- POSIX `/bin/sh` for Linux and FreeBSD.
- Interactive multi-select builder.
- `--cli`, `--gui`, `--all`, `--clean`, `--dry-run`.
- Optional `--install` / `--no-install` and custom `--prefix`.
- Root escalation for missing system-package installation and, if selected, the final system installation step; compilation remains unprivileged.
- S.I.P.H.E.R.-specific `--pjsip` action for a local PJSIP 2.17 build configured with `PJSUA_MAX_CALLS=64`.
- Automatic detection of the standard local PJSIP pkg-config directory.
- Legacy `scripts/build-trunkmonkey.sh` retained as a compatibility wrapper.

## Validated in the build workspace

- Profile parser and writer compile cleanly under C++17.
- Destination/caller-ID text pool compiles cleanly and rotates correctly.
- Top-level and helper shell scripts pass POSIX `sh -n` syntax checks.
- Builder dry-run modes execute for CLI, GUI, combined, PJSIP, clean, dependency display, and install paths without modifying the tree.
- SIP trace classifier unit test covers the initial INVITE, an auth/retry INVITE with a changed CSeq, a true in-dialog RE-INVITE, and its response.
- Queue-test launcher registers its native worker thread with PJSUA2 and cancellation joins it before SIP-engine shutdown.
- Source/text files pass a basic sanity scan.

## Full SIP/Qt build status

See the final packaging notes supplied with this archive. If the assembly environment cannot provide PJSIP/Qt, preserve the exact compiler output from the first Linux Mint build. The code is deliberately layered so PJSIP compatibility fixes remain localized to the core.

## Historical first beta-test order

1. Run `./build.sh` and select `A` for the first build; dependencies and local PJSIP are now handled automatically when missing.
2. Configure one test SIP extension/account.
3. Verify REGISTER.
4. Place **one** outbound Phone call.
5. Confirm the GUI shows the SIP Call-ID and negotiated RTP target.
6. Confirm `Observed RTP source` populates once packets arrive.
7. Confirm live SIP log contains the initial INVITE and responses.
8. Put the call on hold/resume or otherwise trigger a re-offer and verify a later INVITE is labeled **RE-INVITE**.
9. Start/stop a raw SIP trace and inspect the file.
10. Start a SIP PCAP, then an RTP PCAP after media negotiation, and verify both open in Wireshark.
11. Confirm two-way audio, DTMF, hold/resume, and hangup.
12. Only after the single-call path is solid, launch 2 independent Queue-test calls and increase gradually.

## Dependency-aware builder update

The top-level `build.sh` now checks the selected host/target before compiling. Missing system packages are installed automatically by default, with a prominent warning before any root request. Supported automatic package-manager paths are `apt-get` (Debian/Ubuntu/Linux Mint), `dnf` (Fedora/RHEL family), and `pkg` (FreeBSD). Use `--no-auto-deps` to make the preflight report-only.

CLI/GUI builds also check for `libpjproject`; when it is absent, the builder automatically invokes the included PJSIP 2.17 bootstrap configured with `PJSUA_MAX_CALLS=64`. A normal fresh build can therefore select `A`/`--all` without separately selecting `P`.

Root privileges are never used for C++ compilation or for the local PJSIP compile/install prefix under the user's home directory. They are used only to install missing system packages and, if explicitly selected, to install S.I.P.H.E.R. under the system prefix.

## Theme parity refresh

The Qt GUI now carries the same eight theme choices as WaffleHouse Client 2.1 Alpha: System, Hacker, Matrix, Phosphor, Midnight, Amber, Ice, and Classic Light. The selected theme is persisted via Qt `QSettings` and restored at startup. S.I.P.H.E.R.-specific controls such as the active-call table, SIP transaction table, raw SIP viewer, media diagnostics, and Queue Test tab are included in the theme styling.

### Unix compile hotfix
The Unix package was refreshed on 2026-08-15 after the first live Linux compile exposed a const-correctness mismatch with PJSUA2 `pj::Call::dump()`. The signature is corrected and the warnings surfaced by GCC were cleaned up.
- The first Unix linker hotfix exposed that static PJSIP carries more than OpenSSL as transitive dependencies. The final Beta 0.2 Unix build therefore uses PJSIP's complete static pkg-config linker flags rather than hard-coding individual libraries.

## Static PJSIP linking
S.I.P.H.E.R.'s Unix CMake build consumes `PJPROJECT_STATIC_LDFLAGS`, the flags reported by `pkg-config --libs --static libpjproject`. This is required because the locally bootstrapped PJSIP is a static build and may depend on additional codec, audio, crypto, UUID, SRTP, and echo-cancellation libraries.


## Beta 0.2 Unix portability/stability audit — 2026-08-15

A full Linux/FreeBSD audit was performed after the first live Linux linker tests. In addition to the earlier const/OpenSSL/static-pkg-config fixes, this refresh hardens the Unix release in the following areas:

- PJSIP validation now performs a real PJSUA2/audio static-link probe before compiling S.I.P.H.E.R. and automatically rebuilds the local dependency if validation fails.
- Local PJSIP builds use `-fPIC`, an OS/architecture-aware compatibility stamp, and exact 2.17 source-tag validation.
- FreeBSD builds use `gmake`, `--enable-kqueue`, external PortAudio, and FreeBSD-aware Qt/compiler checks.
- Runtime config/log/state paths now use per-user XDG locations instead of executable-adjacent directories; app-owned directories use mode 0700 and sensitive text outputs use mode 0600.
- Packet-capture launch now uses `posix_spawn()` rather than `fork()` after PJSIP worker threads exist. Capture stop escalates SIGINT -> SIGTERM -> SIGKILL if needed.
- The queue launcher uses direct PJLIB thread registration/unregistration and joins before SIP shutdown.
- SIP wire monitoring is synchronized during shutdown; raw SIP retention and pre-call buffering are bounded; late Call-ID discovery refreshes the wire-monitor map.
- SIP accounts are explicitly bound to the configured UDP/TCP/TLS transport.
- Disconnected calls keep stored diagnostics/history but no longer invoke live PJSUA2 call/media methods.
- Background queue calls report actual media state independently of foreground headset attachment.
- Profile parsing validates transport, booleans, and port ranges and writes private profile files.

Validation in the assembly workspace includes GCC and Clang warning-as-error builds for platform-neutral components, ASan/UBSan runs for parsers/trace/runtime-path logic, POSIX shell syntax/dry-run tests, and a real CaptureManager process integration test. A complete native PJSIP + Qt application link still requires the target host because the assembly environment does not provide the full Linux/FreeBSD audio/Qt development stack.


## Beta 0.2 first-run profile/settings + uninstall refresh — 2026-08-15

- `build.sh` now seeds the user's `profile.conf` on the first CLI/GUI build and never overwrites an existing profile.
- The CLI automatically opens an internal SIP profile editor when the profile is new/unconfigured, and adds `profile-show`, `profile-edit`, and `profile-reload` commands.
- CLI password entry is hidden on a terminal; `profile-show` never prints the stored password.
- The Qt GUI adds **Settings -> SIP Profile...** with the complete Beta 0.2 SIP account/profile fields.
- Profile edits validate before use and live edits restart the SIP engine only when calls are inactive. Failed reconnects roll back to the previous profile.
- Profile writes use a private temporary file and atomic same-directory replacement on Unix, retaining mode `0600`.
- Qt dependency probing now runs in a temporary directory and no longer leaves source-tree `CMakeFiles/` artifacts.
- The builder interactive menu adds **R) Remove Install** and non-interactive `--uninstall` support.
- Uninstall preserves user profile/settings/logs by default; `--purge-user-data` explicitly removes standard current-user S.I.P.H.E.R. config/state directories.

## Beta 0.2 post-install profile seeding fix — 2026-08-15

- `build.sh` now seeds `~/.config/trunkmonkey/profile.conf` **after** a successful CLI/GUI system install instead of before installation.
- The seeder prefers the installed template at `<prefix>/share/trunkmonkey/examples/profile.conf.example` and falls back to the source-tree example for development installs.
- Existing user profiles are always preserved; the builder never overwrites SIP credentials/settings.
- The generated profile directory is private (`0700`) and the profile is private (`0600`) on Unix.
- The builder verifies that the seeded profile is non-empty and clearly reports the template source used.
### Unix r6 test harness correction
The Unix release tests no longer use C/C++ `assert()` for validation. `RelWithDebInfo` defines `NDEBUG`, so assertions can be compiled out; the tests now use always-on checks and temporary test paths.


## Unix r8 UI / capture / FreeBSD notes

- CLI pages: Alt+1 Main, Alt+2 SIP Log, Alt+3 Media/RTP, Alt+4 Active Calls, Alt+5 Security Audit, Alt+6 Profile/Config, Alt+7 Help, Alt+8 Engine Log, Alt+9 Queue/Activity.
- `siplog <id>` jumps to the SIP page; `media <id>` jumps to Media/RTP.
- `capture-ifaces` shows the detected capture tool/interfaces and platform permission guidance.
- Linux builder offers to apply capture capabilities to dumpcap/tcpdump so GUI/CLI capture can run without launching S.I.P.H.E.R. as root.
- FreeBSD capture requires BPF access; S.I.P.H.E.R. now reports the actual capture-tool error rather than only "exited immediately".
- FreeBSD PJSIP r8 disables legacy WebRTC AEC because the optional PJSIP 2.17 archive can fail to resolve CPU/SSE2 helper symbols on FreeBSD x86_64.


### Unix r10 FreeBSD installer/linker note
- FreeBSD static PJSIP verification now retains the ports `LOCALBASE` library directory and passes it into CMake. An r9 PJSIP installation can be reused; r10 does not require a PJSIP rebuild for this metadata/search-path fix.

### Unix r11 PJSIP hardening (2026-08-15)
- FreeBSD PJSIP/PJSUA2 and S.I.P.H.E.R. are pinned to the base Clang/libc++ ABI; mixed libstdc++ metadata is rejected.
- FreeBSD PJSIP uses external PortAudio but no longer forces experimental kqueue. FreeBSD PJSIP requires PortAudio, Opus, bcg729, and libuuid explicitly; UPnP and legacy libwebrtc remain disabled for deterministic static linking.
- Local PJSIP compatibility stamps include the FreeBSD ABI/compiler version so OS/toolchain upgrades trigger a safe rebuild.
- Shutdown uses PJSIP 2.17 Account::shutdown2(), drains calls before account/endpoint teardown, and rejects new calls while stopping.
- Multi-call queue launches require at least 50 ms spacing to avoid unsupported high-concurrency INVITE bursts.


### r11 PJSIP hardening completion
- S.I.P.H.E.R. now requires its stamped managed PJSIP 2.17 build instead of silently consuming an arbitrary system libpjproject.
- The managed PJSIP source applies a narrow **PJSUA2 Call lifetime compatibility guard** so a delayed `pj::Call` destructor cannot clear a reused call slot or touch PJSUA after shutdown.
- PJSIP is built with `make lib` rather than the default all-target, avoiding failures in sample/test executables S.I.P.H.E.R. does not ship or use.
- The FreeBSD bootstrap explicitly requires PortAudio, Opus, bcg729, and libuuid, while disabling WebRTC AEC, UPnP, AMR, SILK, and video helpers. Bundled G.711/G.722/GSM/Speex/iLBC remain available.
- The PJSIP compatibility stamp is v9 so older local builds are rebuilt once with the r11 ABI/I/O-queue recipe.

- r11: managed PJSIP now sets `PJ_IOQUEUE_MAX_HANDLES=256` for the 64-call compile-time ceiling and validates it before building S.I.P.H.E.R..

## r6 build hotfix
- Fixed Linux/FreeBSD full-build failure in `SipEngine.cpp` by explicitly including `trunkmonkey/Version.h` before using `SIPHER_USER_AGENT`.
- No SIP/RTP behavior changed; this is a compile-time visibility fix only.
