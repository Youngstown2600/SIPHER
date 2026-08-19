## S.I.P.H.E.R. 1.0.0 r14 — FreeBSD audio compatibility hardening — 2026-08-19

- Adds a hardware-discovered FreeBSD `snd_hda` compatibility pass to normal CLI/GUI builds.
- Detects only high-confidence laptop layouts with exactly one fixed `Speaker` pin and one jack `Headphones` pin and no analog `Line-out`; complex desktop/5.1/7.1 layouts are advisory-only.
- Uses the codec firmware's Speaker association as the conservative target and assigns the headphone pin to the same association with `seq=15`, enabling kernel headphone duplication/auto-mute where jack detection is supported.
- Before changing HDA routing, r14 checks for existing user pin hints and refuses to override custom policy.
- Runtime repair pauses user PulseAudio when necessary, rebuilds `snd_hda`, verifies playback/capture PCM availability, and rolls back immediately if validation fails.
- A validated repair is persisted in `/boot/device.hints` with a timestamped backup; only the headphone hint is written, leaving the firmware Speaker mapping intact.
- Retains the narrowly fingerprinted ALC236/VREF80 headset-mic repair and all r13 main-screen prefix + r12 live audio hot-swap behavior.
- Adds `tests/freebsd_audio_compat_test.sh` and a reusable hardware parser at `scripts/freebsd-hda-output-detect.awk`.
- Builder revision: `sipher-r14-20260819-freebsd-audio-compat`.

## S.I.P.H.E.R. 1.0.0 r13 — Main-screen runtime dial prefix — 2026-08-19

- Moved the operational PBX dial prefix to the main dialing workflow in both GUI and CLI.
- GUI now has an editable **Dial prefix** field directly beside Destination/Caller ID. Blank means no prefix.
- CLI Main/Account panel now shows the current prefix; `prefix 4071` changes it immediately and `prefix off` clears it.
- Guided CLI dialing prompts for the prefix on every call and remembers the chosen value for the session.
- The profile `dial_prefix` remains supported only as the startup/default value for backward compatibility.
- Queue/test calls inherit the current runtime prefix through the shared SIP engine.
- Explicit `sip:`, `sips:`, and `user@domain` destinations continue to bypass prefix manipulation.
- Builder revision: `sipher-r13-20260819-main-screen-dial-prefix`.

## S.I.P.H.E.R. 1.0.0 r12 — Linux + FreeBSD live headset/device switching — 2026-08-19

- Extends r11 automatic audio recovery to FreeBSD while retaining Linux PipeWire/PulseAudio support.
- Linux watches the current PulseAudio/PipeWire sink/source and active ports and performs a live PJSIP detach -> close -> refresh -> reopen -> reattach when the route changes.
- FreeBSD first uses PulseAudio route/port state when `pactl` is available. Otherwise it watches native OSS/snd_hda state: `hw.snd.default_unit`, `/dev/sndstat`, and `mixer -d <unit> -s` recording-source changes.
- FreeBSD `snd_hda` same-association headphone switching remains kernel-managed; r12 reopens PJSIP when userland-visible route/PCM/record-source state changes, including USB PCM attach/detach and headset-mic autosrc changes.
- Added `audio-auto on|off` and a GUI **Automatically Follow Headset / System Audio** toggle. Automatic switching defaults to ON.
- Audio Status now reports watcher availability, backend, automatic-switch state and current system route.
- Fixed refreshed-device disappearance handling: a removed device now falls back to PJSIP's capture/playback default instead of reusing a stale numeric index that may point at a different device.
- Builder revision: `sipher-r12-20260819-linux-freebsd-audio-hotswap`.

## S.I.P.H.E.R. 1.0.0 r11 — Linux/Unix audio reopen + headset hot-plug recovery — 2026-08-19

- Reworked PJSIP audio switching: device changes now detach foreground media, call `setNoDev()`, refresh device enumeration, restore selections by driver/name, call `setSndDevMode(0)` for an immediate full-duplex reopen, verify `sndIsActive()`, and reattach the live call.
- Fixes the r10 behavior where `audio-output` / `audio-use` only changed selected device IDs while an already-open PJSIP stream remained pinned to the old speaker/headset route.
- Linux desktop startup now prefers the PJSIP-visible `ALSA / pipewire` device when available and no explicit environment override was supplied.
- Linux GUI and dashboard CLI poll the PipeWire/Pulse compatibility route via `pactl`; speaker/headset or microphone port changes automatically trigger a PJSIP audio reopen when system/default audio is selected.
- Added CLI `audio-status`, `audio-reopen`, and `audio-refresh`; `audio-devices` now reports whether the PJSIP sound device is actually active and shows the detected system route.
- Added GUI **Audio Status...** and **Reopen / Refresh Audio** actions.
- Direct ALSA/HDMI selections remain manual and are not overridden by desktop hot-plug policy.
- Builder revision: `sipher-r11-20260819-audio-hotplug-reopen`.

## S.I.P.H.E.R. 1.0.0 r10 — Dial prefix + explicit SIP TX/RX — 2026-08-18

- Added `dial_prefix` to the SIP profile for Asterisk/FreePBX or other PBXs that require access/routing digits in the actual outbound SIP destination.
- The prefix is applied only to plain dial strings. Explicit `sip:`, `sips:`, name-addr, and `user@domain` destinations are never rewritten.
- GUI Main page now shows a per-call **Use configured dial prefix** switch; the saved prefix is edited under **Settings → SIP Profile**.
- CLI profile editor/show pages now expose `dial_prefix`; normal `dial` commands automatically use the configured prefix.
- Dial notices/status now show the effective SIP URI after prefix processing, making it easy to verify the exact Request-URI being attempted.
- SIP Log direction labels are now explicit: **SENT →** and **← RECEIVED**, replacing terse TX/RX-only presentation.
- Raw SIP selection, CLI raw SIP overlay, saved text traces, and the SIP ladder now explicitly identify traffic sent by S.I.P.H.E.R. versus traffic received from the PBX.
- Builder revision: `sipher-r10-20260818-dial-prefix-sip-tx`.

## S.I.P.H.E.R. 1.0.0 r9 — Unix/Linux audio output selector — 2026-08-18

- Added a dedicated Unix/Linux GUI **Settings -> Audio Output...** selector for playback-capable PJSIP devices.
- Added CLI `audio-output <playback-id>` and a guided **Choose audio output device** workflow.
- Output-only changes preserve the current microphone/capture device.
- If a foreground call is active, its audio bridge is rebound immediately to the newly selected playback device.
- Existing full `audio-use <capture-id> <playback-id>` routing remains available.
- Builder revision: `sipher-r9-20260818-unix-audio-output`.

## S.I.P.H.E.R. 1.0.0 r8 — Automated chained audit / FreeBSD pthread fix

- Link CaptureManager regression test with `Threads::Threads` on all platforms.
- Add chained audit orchestration, risk prioritization, and remediation report generation.
- Add guided CLI/GUI audit navigation and progress reporting.
- Unify cross-platform PBX audit networking and Windows Winsock implementation.
- Add security posture checks for disclosure, method surface, cleartext transports, TLS indicators, and correlated auth evidence.

# S.I.P.H.E.R. 2.0.0 — Diagnostics & Security Suite — 2026-08-16

## Unix r20 FreeBSD/Release test deadlock hotfix
- Fixed `tm-pbx-audit-test` hanging in optimized (`NDEBUG`) builds by removing all socket/system-call side effects from `assert(...)` expressions.
- PBX audit test now uses always-active runtime checks so bind/getsockname/recvfrom validation works in Release and Debug builds.
- Added a 30-second CTest timeout to `pbx-audit-test` as a fail-safe against future blocking regressions.

## Unix r19 FreeBSD networking-header hotfix
- Fixes native FreeBSD compilation of the PBX audit core/tests by explicitly including `<netinet/in.h>` for `sockaddr_in`, `INADDR_LOOPBACK`, `IPPROTO_UDP`, and `IPPROTO_TCP`.
- No audit behavior or safety bounds changed.

## Unix r18 attack-simulation hardening

- Adds bounded parser-abuse simulation with five small SIP edge cases and explicit result classification.
- Adds a sequential, hard-capped rate-resilience OPTIONS test (default 10 requests, maximum 20).
- Adds CLI `audit-parser`, `audit-resilience`, and `audit-scenario`; GUI gains Parser Abuse, Rate Resilience, and Attack Scenario controls.
- Full engineering audit now includes the attack-scenario chain plus compliance, alternate transport reachability, and TLS inspection.
- Strengthens the authorized-use warning to call out IDS/IPS, alarms, throttling, and service-protection triggers.


- Added live RTP/RTCP statistics and call-quality engineering estimates.
- Added SIP ladder and diagnostic report export.
- Added ffmpeg-normalized audio-file injection for queue/blast tests.
- Added bounded, authorized PBX security auditing with service, extension, auth, Digest oracle/nonce, compliance, UDP ratio, and TLS checks.
- Builder revision `unix-r16-20260816-voip-engineer-edition`; Linux and FreeBSD remain first-class targets.

# S.I.P.H.E.R. 1.0 — Unix r15 GUI/audio refresh — 2026-08-16

- Keeps the stable application version at `1.0.0` / SIP User-Agent `S.I.P.H.E.R./1.0`; builder revision is `unix-r15-20260816-gui-refresh`.
- Replaces the GUI emoji mascot with a native Qt-drawn S.I.P.H.E.R. mark and refreshes the CLI monkey header.
- Consolidates the former Phone and Media/RTP pages into a compact `Main` tab with dialing, call controls, selected-call media, SIP/RTP PCAP controls, and a visible Exit button.
- Adds File -> Exit and reduces GUI startup size to 860x560 (680x460 minimum).
- Replaces free-form GUI DTMF entry with a 12-key press-and-hold pad. Key hold time becomes the RFC4733 event duration (80-5000 ms) and is sent on mouse release.
- Hardens FreeBSD PJSIP microphone selection by scoring dedicated capture/headset/microphone device names in addition to capture-only topology, protecting Project-2501 style ALC236 routing.
- Keeps builder-managed Linux/FreeBSD capture permissions, ALC236 VREF80 repair, CLI engine-log isolation, and the 16-theme set.
- Adds a Main-tab Wireshark note for RTP-only captures that may need the `rtp_udp` heuristic enabled.

# S.I.P.H.E.R. 1.0 — Unix r14 stable — 2026-08-16

- First stable S.I.P.H.E.R. 1.0 Unix package; application version `1.0.0`, SIP User-Agent `S.I.P.H.E.R./1.0`, builder revision `unix-r14-20260816-stable`.
- Builder automatically prepares SIP/RTP packet capture: Linux uses `CAP_NET_RAW` + `CAP_NET_ADMIN` on the resolved capture helper; FreeBSD uses a backed-up persistent per-user `devfs` rule for `/dev/bpf*`. Added `--configure-capture`.
- Builder adds a narrowly signature-matched ALC236 repair for the verified Project-2501 failure (`init_clear=1`, `ivref80`, NID25 verification at `0x24`) and refuses broad/unknown HDA rewrites.
- PJSIP verbose output moved to `/tmp/trunkmonkey-<uid>/pjsip-engine.log`; dashboard-mode S.I.P.H.E.R. logging no longer writes asynchronously over the terminal UI.
- CLI adds Alt+8 Engine Log, PageUp/PageDown scrolling, persistent expanded themes, and the compact monkey mark/header.
- GUI default footprint reduced to 920x620 with a 720x500 minimum; Active Calls uses horizontal scrolling instead of expanding the whole window.
- Added eight more GUI/CLI themes: Solarized, Dracula, Nord, Cyberpunk, Blood Moon, Ocean, Retro Blue, and Monochrome.
- Retains r13 independent capture/playback device selection and all prior FreeBSD PJSIP/LOCALBASE/audio hardening.

# S.I.P.H.E.R. 1.0 — Unix r13 audio routing — 2026-08-16

- Builder revision `unix-r13-20260816-audio-routing`: adds PJSIP audio device IDs/names to diagnostics, ignores commented HDA hints, detects multiple FreeBSD capture paths, and recognizes the verified ALC236 VREF80 headset-mic state.
- SIP startup now enumerates/logs PJSIP audio devices and independently selects capture/playback. On FreeBSD, when exactly one capture-only device exists, S.I.P.H.E.R. prefers it so a dedicated headset mic is not silently replaced by the default internal mic.
- Numeric runtime overrides are available through `S.I.P.H.E.R._CAPTURE_DEVICE` and `S.I.P.H.E.R._PLAYBACK_DEVICE`.
- The builder remains non-destructive: it reports HDA/VREF/audio-routing conditions but never rewrites host pin mappings or mixer settings.

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

# S.I.P.H.E.R. Beta 0.2 — Unix r8

- Reworked CLI into seven Alt+number pages: Main, SIP Log, Media/RTP, Active Calls, Queue/Activity, Help, and Profile/Config.
- Main CLI page keeps a compact Activity feed; full SIP signaling has its own page.
- Added shared Linux/FreeBSD capture-interface discovery and detailed capture stderr/permission errors for both CLI and GUI.
- Linux builder can configure CAP_NET_RAW/CAP_NET_ADMIN on dumpcap/tcpdump for non-root capture.
- Streamlined Qt GUI into Phone, Active Calls, SIP Log, Media/RTP, Queue Test, Profile, and Activity tabs.
- Fixed GUI shutdown assertion by releasing pj::Call wrappers before PJSUA libDestroy().
- FreeBSD PJSIP build now disables optional legacy WebRTC AEC to avoid unresolved WebRtc_GetCPUInfo/SSE2 symbols while retaining PortAudio audio support.
- PJSIP compatibility stamp bumped so older FreeBSD/Linux local builds are rebuilt automatically.

## Beta 0.2 Unix r7 — CLI dashboard refresh

- Replaced the plain scrolling CLI shell with a persistent ANSI terminal dashboard matching the S.I.P.H.E.R. mockup.
- Added the monkey/brand header, account panel, registration panel, active-call table, call diagnostics, recent SIP signals, activity log, quick-command panel, and bottom `tm>` prompt.
- The dashboard is dependency-free (no ncurses requirement) and automatically uses ANSI only on an interactive terminal.
- Wide terminals use a two-column layout; narrow/short terminals collapse into a compact single-column layout.
- `media <id>` and `siplog <id>` select the call shown in the diagnostics panel; blank Enter/`refresh` redraws current state.
- Full raw SIP, PJSIP stats, profile display, and help use temporary full-screen overlays and return to the dashboard.
- Added `S.I.P.H.E.R._FORCE_DASHBOARD=1` and `S.I.P.H.E.R._NO_DASHBOARD=1` overrides for unusual terminals/testing.
- Added a release-mode CLI dashboard test covering branding, registration, active calls, RTP details, SIP signals, quick commands, and prompt rendering.
- Builder revision is now `unix-r7-20260815`.

## Beta 0.2 Unix r6 — CTest release-build fix

- Replaced `assert()`-based unit-test checks with always-on checks so RelWithDebInfo/Release builds still execute the tests.
- Removed side effects from disabled assertions; first-run profile creation is now actually exercised by CTest.
- Profile tests now use a dedicated temporary directory rather than the CTest working directory.
- Runtime path and SIP trace tests now fail explicitly with useful diagnostics when a check fails.
- Builder revision is now `unix-r6-20260815`.

## Unix builder stale-PJSIP source recovery

- Detects and refuses builds launched from a source directory physically inside desktop Trash (unless explicitly overridden).
- Makes the builder revision visible in the dependency banner so stale extracted copies are easy to identify.
- Cleans S.I.P.H.E.R.'s cached in-tree PJSIP checkout with `git clean -ffdx` before reconfiguration, avoiding `make distclean` failures caused by generated `build.mak` files containing old absolute paths after a directory move/rename.
- Custom git PJSIP source trees fall back to cleaning ignored generated files if `distclean` cannot parse stale state; non-git custom trees are left untouched and fail safely.

# Changelog

## Beta 0.2 profile management / uninstall refresh — 2026-08-15

- Builder creates a private first-run SIP profile automatically after a successful CLI/GUI build and preserves existing profiles.
- CLI adds built-in profile show/edit/reload commands with password redaction and hidden password input.
- GUI adds Settings -> SIP Profile with live save/reconnect and rollback to the last working profile on startup failure.
- First launch enters the in-program setup flow when the profile is missing, blank, or still contains the old example placeholders.
- Profile writes now use a temporary sibling file and atomic Unix replacement to reduce partial-write risk.
- Qt dependency probing now runs in a temporary directory so dependency checks do not dirty the source tree.
- Builder adds interactive Remove Install plus `--uninstall`; current-user data is preserved unless `--purge-user-data` is explicitly selected.


## Beta 0.2 Unix portability/stability audit — 2026-08-15

- Added complete static PJSIP link validation using the installed `libpjproject.pc` static flags and a PJSUA2/audio probe before the main build.
- Automatically rebuilds local PJSIP when missing, stale, architecture/OS incompatible, below the 50-call ceiling, or unable to satisfy its static dependency chain.
- Added `-fPIC` local PJSIP builds and OS/architecture-aware build stamps; bootstrap reuse now requires the exact PJSIP 2.17 source tag.
- FreeBSD PJSIP builds now use `gmake`, `kqueue`, and external PortAudio; dependency checks use FreeBSD/Clang-aware Qt detection and treat base `tcpdump` as a system facility.
- Added Unix per-user runtime paths through XDG config/state locations and private directory/file permissions for profiles, logs, and raw SIP traces.
- Replaced packet-capture `fork()` launch with `posix_spawn()` and added graceful termination escalation.
- Hardened queue worker PJLIB thread registration/unregistration and SIP-engine shutdown ordering.
- Synchronized SIP wire-monitor teardown; bounded unmatched-dialog and raw-message retention; fixed late SIP Call-ID correlation.
- Bound SIP accounts explicitly to the profile-selected transport.
- Prevented live PJSUA2 call/media operations after DISCONNECTED while retaining stored post-call diagnostics.
- Corrected queue-call media-state semantics so non-foreground calls can remain media-active without headset attachment.
- Hardened profile validation and startup cleanup paths.


## Beta 0.2 Unix compile hotfix — 2026-08-15

- Fixed `CallSession::mediaDump()` const mismatch with PJSUA2 `pj::Call::dump()`.
- Cleaned misleading-indentation warnings in call state, identity, multi-call, and SIP monitor code.
- Limited Qt AUTOMOC to the GUI target so CLI-only CMake configuration does not emit Qt AUTOGEN developer warnings.
### WaffleHouse 2.1 Alpha theme parity refresh

- Replaced the early Classic/Dark/Hacker GUI theme list with the WaffleHouse Client 2.1 Alpha set: System, Hacker, Matrix, Phosphor, Midnight, Amber, Ice, and Classic Light.
- Ported the 2.1 Alpha Qt palette/style rules and adapted them to S.I.P.H.E.R. tables, SIP trace views, tabs, buttons, forms, scrollbars, and diagnostics controls.
- Theme selection now persists across launches using Qt `QSettings`.
- System theme clears the application stylesheet so the native Qt/desktop appearance is restored.

### Builder dependency preflight refresh

- `build.sh` now performs an automatic, target-aware dependency check before compilation.
- Missing OS packages are automatically installed by default after a prominent root-access warning.
- Root escalation supports existing root, `sudo`, `doas`, and `su`; compilation and local PJSIP compilation remain unprivileged.
- Debian/Ubuntu/Linux Mint (`apt-get`), Fedora/RHEL-family (`dnf`), and FreeBSD (`pkg`) installation paths are supported.
- CLI-only builds avoid installing Qt; GUI builds verify Qt 6 Widgets/platform support.
- PCAP support verifies `dumpcap` or `tcpdump`.
- CLI/GUI builds now auto-bootstrap the local PJSIP 2.17 / 64-call dependency when `libpjproject` is missing, so fresh installs no longer require selecting `P,A`.
- Added `--auto-deps` / `--no-auto-deps` builder controls.
- Packet-capture tool discovery now also checks common `/usr/sbin` and `/usr/local/{bin,sbin}` paths for FreeBSD and other Unix layouts.

## 0.2.0-beta — single-call diagnostics refresh

- Added read-only PJSIP wire monitor for actual SIP TX/RX messages.
- Added per-call SIP Call-ID correlation and early-dialog buffering so the initial INVITE is retained.
- Added live SIP transaction table and full raw SIP message viewer in the GUI.
- Added explicit **RE-INVITE** labeling using the in-dialog `To` tag, with responses associated to the re-INVITE by CSeq. This avoids mislabeling initial INVITE authentication/retry traffic.
- Added raw readable SIP trace recording for normal Phone calls.
- Added SIP packet capture and RTP/RTCP packet capture using `dumpcap` or `tcpdump`.
- Added negotiated RTP target, observed RTP source, local RTP endpoint, and codec/rate to the Phone diagnostics panel.
- Added CLI `media`, `siplog`, `sipraw`, `siptrace-*`, `sipcap-start`, `rtpcap-start`, and capture status/stop commands.
- Marked calls internally as `Phone` or `QueueTest`; detailed Beta 0.2 capture controls are restricted to normal Phone calls.
- Registered the queue-launch worker thread with PJSUA2 and made cancellation join cleanly before engine shutdown.
- Added optional packet-capture dependency guidance to Linux/FreeBSD builder output.

## 0.2.0-beta — builder refresh

- Added WaffleHouse-style top-level `build.sh` for Linux and FreeBSD.
- Added interactive CLI/GUI/PJSIP multi-select workflow.
- Added non-interactive `--cli`, `--gui`, `--all`, `--pjsip`, `--clean`, `--deps`, and `--dry-run` modes.
- Added optional `--install`, `--no-install`, `--prefix`, and privilege handling for system installation.
- Made PJSIP helper scripts POSIX `/bin/sh` compatible for FreeBSD as well as Linux.
- Top-level builder automatically recognizes the standard local S.I.P.H.E.R. PJSIP prefix.
- Kept `scripts/build-trunkmonkey.sh` as a compatibility wrapper.

## 0.2.0-beta — initial source package

- Provider-neutral PJSUA2 SIP engine.
- Linux/FreeBSD CMake build.
- REGISTER + digest auth; UDP/TCP/TLS.
- Incoming/outgoing two-way calls.
- Independent multi-call engine (1-50) with launch pacing.
- Destination and caller-ID text-file pools.
- Per-profile From / PAI / RPID identity signaling.
- Foreground-call audio model (no batch-call mixing).
- Hold/resume, DTMF, hangup controls and media dumps.
- CLI plus Qt 6 GUI with System/Hacker/Matrix/Phosphor/Midnight/Amber/Ice/Classic Light themes.

### Beta 0.2 Unix static-PJSIP link fix
- Switched CMake from the non-static `PkgConfig::PJPROJECT` imported target to PJSIP's complete `pkg-config --static` linker flags.
- This automatically carries the transitive libraries used by the local PJSIP build (for example ALSA, codec libraries, SRTP, UUID, WebRTC/resampler libraries, and OpenSSL when enabled) instead of hard-coding them one at a time.

## Beta 0.2 post-install profile seeding fix — 2026-08-15

- `build.sh` now seeds `~/.config/trunkmonkey/profile.conf` **after** a successful CLI/GUI system install instead of before installation.
- The seeder prefers the installed template at `<prefix>/share/trunkmonkey/examples/profile.conf.example` and falls back to the source-tree example for development installs.
- Existing user profiles are always preserved; the builder never overwrites SIP credentials/settings.
- The generated profile directory is private (`0700`) and the profile is private (`0600`) on Unix.
- The builder verifies that the seeded profile is non-empty and clearly reports the template source used.


### Unix r10 FreeBSD static-link fix
- Preserve FreeBSD pkg-config system library paths and explicitly add `${LOCALBASE:-/usr/local}/lib` to both the PJSIP static-link preflight and the real CMake link. This fixes post-PJSIP failures for PortAudio, bcg729, Opus, libupnp/ixml, and libuuid without rebuilding PJSIP.

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

