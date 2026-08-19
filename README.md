## r14 FreeBSD audio compatibility

On FreeBSD, a normal CLI/GUI build now performs a conservative `snd_hda` compatibility pass before compilation. r14 discovers the machine's own HDA pins; it does **not** hardcode Project-2501 NIDs. A layout is eligible for automatic correction only when one HDA function group has exactly one fixed `Speaker` pin, exactly one jack `Headphones` pin, and no analog `Line-out`. The builder keeps the firmware Speaker association and tests the headphone in that same association at `seq=15`, which is FreeBSD's special headphone duplicate/auto-mute sequence.

The builder refuses to override existing user HDA pin hints, temporarily releases PulseAudio if needed, validates that playback and capture PCM devices still exist after `snd_hda` reconfiguration, rolls back a failed test, and persists only a validated fix with a `/boot/device.hints` backup. Complex desktop/multi-output layouts remain untouched. Use `./build.sh --audio-diagnose` for read-only inspection or `--no-audio-fix` to build without applying recognized repairs.

## r13 per-PBX dial prefix on Main

The dial prefix is now a live session setting. In the GUI, edit **Dial prefix** directly on Main before calling. In the CLI, use `prefix <value>`, `prefix off`, or simply choose **Place a call** and edit the prefix when prompted. The profile value is only a startup default.

# S.I.P.H.E.R. 1.0.0 — Linux / FreeBSD

## r12 live headset/device switching

r12 adds default-on automatic local audio rerouting during an active SIP call. Linux follows PipeWire/PulseAudio sink/source port changes. FreeBSD combines PulseAudio state (when present) with native OSS/snd_hda state (`hw.snd.default_auto`, `hw.snd.default_unit`, `/dev/sndstat`, and the mixer recording source). When the route changes, S.I.P.H.E.R. keeps the SIP/RTP call up while it detaches the foreground AudioMedia, closes and refreshes PJSIP audio, reopens the selected/default devices, verifies the sound device is active, and reattaches the call.

Use `audio-auto on|off`, `audio-status`, and `audio-reopen` from the CLI. The GUI exposes **Automatically Follow Headset / System Audio** plus Audio Status and Reopen/Refresh actions.


S.I.P.H.E.R. is a usability-first repackaging of the proven TrunkMonkey 2.0 r20 core. **No features were removed.** The goal of S.I.P.H.E.R. 1.0 is to make the same softphone, diagnostics, queue testing, packet capture, audio routing, themes, and bounded PBX audit tools usable by a Tier-1 NOC technician without requiring them to memorize commands.

## S.I.P.H.E.R. 1.0 r5 highlights

- Visible branding: **S.I.P.H.E.R. By GITSC**, with the block-terminal logo in the CLI and matching GUI banner/icon.
- Responsive CLI: live terminal-resize redraws, compact virtual-console/TTY layout, and multi-line Alt-key navigation.
- TrunkMonkey-style slash commands for calls, including `/dial`, `/answer`, `/hangup`, `/hangup-all`, `/hold`, `/resume`, `/mute`, `/unmute`, `/dtmf`, and `/calls`.
- Dedicated **Alt+5 Security Audit** screen; Alt+6 Profile, Alt+7 Help, Alt+8 Engine Log, Alt+9 Queue/Activity.
- `audit-fingerprint` identifies common SIP/PBX/SBC products and disclosed versions/capabilities from SIP responses.
- `audit-vulns` correlates the fingerprint with **NIST NVD CVE API 2.0** and official **Exploit-DB metadata**. It never executes exploit code.
- 26 total themes across CLI and GUI.


## CLI: Operator Mode first

The CLI dashboard now presents numbered guided workflows:

1. Place a call
2. Manage active calls
3. Queue / call-blast test
4. Call diagnostics & packet capture
5. PBX / SIP security audit
6. Audio devices & registration history
7. SIP account / profile
8. Themes & display
9. Logs & capture utilities
10. Advanced command reference
0. Exit

Type `menu` at any time to reopen the guided menu. Every previous advanced command is still available directly, and all 26 CLI/GUI themes are retained.

The existing runtime/config directories are intentionally retained for compatibility with TrunkMonkey installations (`~/.config/trunkmonkey`, `~/.local/state/trunkmonkey`, and `/tmp/trunkmonkey-<uid>`), so existing profiles and known-good FreeBSD audio fixes continue to work.

## Build

```sh
./build.sh
```

The builder retains Linux and FreeBSD dependency checks, PJSIP 2.17 bootstrap/validation, Qt 6 GUI dependencies, ffmpeg audio-file support, Linux capture capabilities, FreeBSD BPF/devfs capture permissions, the r14 conservative laptop Speaker/Headphones compatibility pass, and the narrowly verified ALC236 headset-mic repair path.

---

## Detailed feature reference

# S.I.P.H.E.R. 2.0.0 — Diagnostics & Security Suite — Linux / FreeBSD

## 2.0.0 Diagnostics & Security Suite

S.I.P.H.E.R. 2.0.0 turns the softphone into a combined SIP call tool, media-quality workstation, queue/blast generator, and bounded PBX security-audit suite for **Linux and FreeBSD**. The builder checks and installs the platform dependencies it needs, including PJSIP 2.17 prerequisites, Qt 6 for the GUI, packet-capture tools/permissions, `ffmpeg` for queue audio normalization, and the OpenSSL command-line client used by TLS audits.

### New engineering diagnostics

- Live RTP/RTCP counters: TX/RX packets and bytes, reported loss/discards, TX/RX jitter, RTT, and jitter-buffer delay.
- Lightweight R-factor/MOS **engineering estimates** for fast triage. These are not PESQ/POLQA measurements.
- SIP ladder view for normal phone calls.
- Exportable call diagnostic reports containing SIP result, codec, RTP endpoints, media counters, quality figures, and SIP ladder.
- Queue/blast tests can inject an operator-selected WAV/MP3/FLAC/OGG/M4A file into every test call. The builder installs `ffmpeg`; S.I.P.H.E.R. normalizes the source to a private mono PCM WAV in its per-user temporary directory before handing it to PJSIP.

### PBX Audit

**AUTHORIZED SYSTEMS ONLY — USE AT YOUR OWN RISK.** PBX Audit transmits active SIP probes. Only use it on systems you own or have explicit authorization to assess.

The 2.0.0 audit suite includes service/capability probing, banner/method disclosure review, rate-limited extension differential checks, unauthenticated registration-policy testing using `Expires: 0`, Digest algorithm/qop/realm review, nonce reuse and account-response-oracle checks, bounded standards/compliance probes, UDP response-ratio review, and TLS handshake inspection. Reports can be saved as private user-only files.

The **Attack Scenario** mode deliberately mirrors a realistic PBX assessment sequence: reconnaissance, SIP method/policy discovery, authentication-oracle analysis, parser-edge requests, and a sequential low-volume rate-resilience check. The parser corpus is limited to five small requests; resilience is hard-capped at 20 requests and defaults to 10 at roughly 6–7 requests/second. These modes may trigger IDS/IPS, alarms, throttling, or PBX protection logic, so the authorization warning is shown in the CLI, GUI, and saved reports.

The audit suite intentionally does **not** perform password cracking, destructive crash payloads, denial-of-service floods, or automated takeover/exploitation.

### Platform guarantees carried forward

The 1.0 FreeBSD fixes remain part of 2.0.0: Project-2501-style ALC236/VREF80 diagnosis and narrowly signature-matched repair, independent PJSIP capture/playback device selection, Linux capture capabilities, FreeBSD BPF/devfs capture permissions, the compact GUI, shared theme set, and the dashboard-safe `/tmp` engine-log view.


S.I.P.H.E.R. is a Linux/FreeBSD SIP softphone plus a call-center queue troubleshooting workspace. The CLI and Qt GUI share the same C++/PJSUA2 core.

## What 1.0 implements

### Stable r15 refresh — 2026-08-16

S.I.P.H.E.R. 1.0 was the first stable Unix release; 2.0.0 builds on that base. It includes the proven Project-2501 FreeBSD audio fixes, independent PJSIP capture/playback routing, automatic packet-capture permission configuration, a smaller Qt GUI, expanded themes, and a CLI engine-log page that keeps raw PJSIP output away from the dashboard.

The normal builder performs dependency checks, audio preflight, recognized safe audio repair, and packet-capture permission setup before compilation. `./build.sh --audio-diagnose` is diagnostic-only; `./build.sh --configure-capture` installs/configures packet-capture prerequisites without rebuilding the client.

### Unix r13 audio routing refresh — 2026-08-16

S.I.P.H.E.R. now enumerates PJSIP audio devices at SIP startup and treats capture and playback as separate routes. On FreeBSD, if PortAudio exposes exactly one capture-only endpoint alongside the normal default duplex device, S.I.P.H.E.R. prefers that dedicated capture endpoint for the microphone while leaving playback on the PJSIP/system default. This addresses laptop layouts where `pcm0` is the internal play/record device and a headset microphone appears separately as a record-only device. Set `S.I.P.H.E.R._CAPTURE_DEVICE=<id>` or `S.I.P.H.E.R._PLAYBACK_DEVICE=<id>` to override the numeric PJSIP device IDs for testing.

`./build.sh --audio-diagnose` now reports PJSIP device IDs/names when the managed PJSIP is already installed, ignores commented-out HDA hints, warns about multiple FreeBSD capture paths, and recognizes the verified ALC236 VREF80 (`0x24`) jack-mic state. Diagnostics remain advisory. During a normal r14 FreeBSD build, the builder may also test the conservative single-Speaker/single-Headphones association repair described above; complex or custom-hinted layouts are never rewritten automatically. The exact verified ALC236 headset-mic repair remains a separate narrowly fingerprinted path.

### Audio-preflight foundation — 2026-08-15

S.I.P.H.E.R. 1.0 adds a non-destructive audio preflight to `build.sh`. Every CLI/GUI build inspects host playback/capture availability before compiling, and `./build.sh --audio-diagnose` can run the same check by itself. On FreeBSD it reports `/dev/sndstat` topology, the default PCM unit, `snd_hda` association errors, runtime HDA pin overrides versus codec originals, persistent `/boot/device.hints`/loader pin overrides, detected codecs, and PulseAudio defaults when available. The diagnostic command never rewrites HDA pins or mixer settings. Normal r14 builds may apply only recognized guarded repairs: the conservative simple-laptop Speaker/Headphones association fix and the narrowly fingerprinted ALC236 headset-mic fix. The PJSIP static-link validation probe also enumerates PJSIP audio devices and warns when PJSIP sees no capture or playback device, including the `PJMEDIA_EAUD_NODEFDEV` risk.


- Provider-neutral SIP REGISTER with digest authentication.
- UDP, TCP, and TLS SIP transports.
- Incoming and outgoing calls.
- Two-way microphone/speaker audio for the selected **foreground call**.
- Answer, reject, hangup, hold, resume, and RFC2833 DTMF.
- **1–50 independent simultaneous calls**.
- Single destination or a rotating destination list loaded from text.
- Fixed caller identity or rotating caller identities loaded from text.
- Caller-identity signaling modes: `From`, `P-Asserted-Identity`, `Remote-Party-ID`, `From + PAI`.
- Configurable inter-call launch pacing for queue tests.
- Per-call state/SIP response display.
- **Single-call live SIP dialog log** showing TX/RX method, CSeq, status, reason, and the complete raw SIP message.
- **RE-INVITE identification** using an established-dialog `To` tag, with responses associated by CSeq within the same SIP Call-ID.
- **Raw SIP trace recording** to a text file for a selected normal Phone call.
- **SIP packet capture** and **RTP/RTCP packet capture** through `dumpcap` or `tcpdump`.
- **Media endpoint display**: negotiated remote RTP target, actual observed RTP source, local RTP endpoint, and codec/rate.
- Raw PJSIP call/media diagnostics (`stats <call-id>`).
- Qt GUI with persistent themes: System, Hacker, Matrix, Phosphor, Midnight, Amber, Ice, Classic Light, Solarized Dark, Dracula, Nord, Cyberpunk, Blood Moon, Ocean, Retro Blue, and Monochrome.

- **Eight-page ANSI CLI dashboard** with the S.I.P.H.E.R. monkey header, registration/account status, active calls, SIP/RTP diagnostics, activity feed, quick commands, a persistent `tm>` prompt, and a scrollable Engine Log page.

## S.I.P.H.E.R. CLI dashboard

The interactive CLI now uses the same dashboard layout shown in the S.I.P.H.E.R. concept mockup. No ncurses package is required: the interface uses standard ANSI terminal control sequences and detects whether stdin/stdout are real terminals.

On a wide terminal, the layout is split into the main troubleshooting workspace and a right-side account/quick-command column. Smaller terminals automatically collapse to a compact single-column view. Redirected output and `TERM=dumb` use the plain non-dashboard CLI path.

The dashboard contains:

```text
Monkey / S.I.P.H.E.R. header      Account / registration state
Registration details            Quick command reference
Active independent calls
Selected-call SIP/RTP diagnostics
Recent SIP TX/RX signals
Recent command/activity messages
tm> command prompt
```

Use `media <id>` or `siplog <id>` to select which Phone call is displayed in **CALL DIAGNOSTICS**. A blank Enter or `refresh` redraws the current state. `help`, `profile-show`, `sipraw`, and `stats` open a temporary full-screen overlay and return to the dashboard afterward.

Terminal overrides are available when needed:

```sh
S.I.P.H.E.R._FORCE_DASHBOARD=1 sipher
S.I.P.H.E.R._NO_DASHBOARD=1 sipher
NO_COLOR=1 sipher
```

### Engine log without dashboard flooding

PJSIP's verbose engine output is written to the private per-user temporary path `/tmp/trunkmonkey-<uid>/pjsip-engine.log` instead of stdout/stderr. Use **Alt+8** or `engine-log` to view it inside the CLI. PageUp/PageDown (or `log-up`/`log-down`) scroll through the retained lines, and `log-tail` jumps back to live output. The temporary log naturally disappears on systems that clear `/tmp` at boot.

### Independent-call rule

A queue test creates separate SIP dialogs and separate RTP sessions:

```text
Call 01 -> SIP dialog 01 -> RTP 01
Call 02 -> SIP dialog 02 -> RTP 02
...
Call 50 -> SIP dialog 50 -> RTP 50
```

They are **not conferenced together**. S.I.P.H.E.R. connects only the explicitly selected foreground call to the local microphone/speaker. Other calls remain independent media sessions.

## Single-call troubleshooting workspace

S.I.P.H.E.R. 1.0 deliberately keeps detailed SIP/RTP capture controls on normal **Phone** calls. Queue-test calls remain visible in the active-call table, but selecting a Queue call disables the detailed trace/capture controls so a 20- or 50-call test cannot accidentally be mistaken for one clean dialog trace.

For a selected Phone call the GUI displays:

```text
SIP Call-ID
Negotiated RTP target   <IP:port from negotiated media>
Observed RTP source     <IP:port packets are actually arriving from>
Local RTP endpoint      <local IP:port>
Codec                    <codec / clock rate>
```

The live SIP table contains:

```text
Time | TX/RX | Signal | CSeq | Code | Reason
```

Selecting a transaction displays the complete SIP message below the table. The initial INVITE is shown as `INVITE`. An INVITE request that already carries the dialog `To` tag is shown as `RE-INVITE`; its matching responses are associated by CSeq and shown as, for example, `200 OK (RE-INVITE)`. This avoids mistaking an authenticated/retried initial INVITE for a re-INVITE.

### SIP trace and packet grabs

S.I.P.H.E.R. provides two different kinds of SIP troubleshooting output:

1. **Raw SIP trace** — S.I.P.H.E.R.'s internal decoded SIP messages, written to a readable text log. This is useful even when SIP transport encryption would make an on-wire packet capture unreadable as cleartext SIP.
2. **SIP PCAP** — an on-wire packet capture using `dumpcap` when available, otherwise `tcpdump`.

RTP/RTCP capture uses the negotiated/observed media ports for the selected normal Phone call. Start the RTP capture after media has been negotiated (normally after ringing/answer depending on SDP behavior).

**r8 Automated Audit:** use `audit` for the guided CLI workflow or `audit-auto <host> [user|-] [port] [udp|tcp] [ext-first ext-last]` for the new chained PBX/SIP audit. The GUI PBX Audit tab now leads with **RUN AUTOMATED CHAINED AUDIT** and reports phase progress plus HIGH/WARN/PASS/INFO posture counts. Extension differential testing is opt-in; bounded parser/rate checks, TLS posture, and public vulnerability metadata can be toggled.

**r7 Auto RTP Decode:** after stopping an RTP-only or combined call PCAP, click **OPEN LAST PCAP (AUTO RTP)** in the GUI. S.I.P.H.E.R. launches Wireshark with the selected call's RTP/RTCP UDP ports already mapped to the RTP/RTCP dissectors, so **Telephony → RTP → RTP Streams** can be used without manually choosing **Decode As**. CLI users can run `pcap-open <id> <file>`. The `.pcap`/`.pcapng` remains a standard capture file; the decode mapping is passed only when Wireshark is launched.

Packet-capture privileges are configured by the **builder**, not by the running softphone. On Linux the builder installs a capture helper when needed and grants it `CAP_NET_RAW` + `CAP_NET_ADMIN`. On FreeBSD it creates a persistent per-user `devfs` rule for `/dev/bpf*`. The builder requests `sudo`, `doas`, or root only for those setup operations; the GUI and CLI continue to run as the normal user. Re-run `./build.sh --configure-capture` at any time to repair capture permissions without rebuilding S.I.P.H.E.R..

The default capture interface is `any` where supported. On FreeBSD, select the actual interface name (for example `wlan0`, `em0`, or `igb0`) when the installed capture tool does not provide an `any` pseudo-interface.


## Automatic profile creation and in-program SIP settings

A normal CLI or GUI build now creates the user's SIP profile automatically when it does not already exist:

```text
~/.config/trunkmonkey/profile.conf
```

`XDG_CONFIG_HOME` and `S.I.P.H.E.R._PROFILE` are honored. The builder copies `examples/profile.conf.example`, creates the S.I.P.H.E.R. config directory privately, sets the profile to mode `0600`, and **never overwrites an existing profile**.

The seeded profile intentionally has blank SIP server/account fields. On the first launch, CLI and GUI detect that the profile still needs configuration and open S.I.P.H.E.R.'s own profile editor instead of exiting with "No SIP profile found".

CLI profile commands:

```text
profile-show      show the active profile with the password redacted
profile-edit      edit/save the profile interactively and reconnect SIP
profile-reload    reload profile.conf from disk and reconnect SIP
```

The CLI editor supports every S.I.P.H.E.R. 2.0.0 SIP profile option. Press Enter to preserve the current value, type `-` to clear a field, and the password is not echoed on a terminal. Profile changes are validated before use. If reconnecting with a newly edited profile fails, S.I.P.H.E.R. restores the previous working profile and attempts to reconnect with it.

The Qt GUI exposes the same configuration through **Settings → SIP Profile...**. The compact **Main** tab combines phone controls, selected-call media, and SIP/RTP packet capture. **DTMF PAD...** opens a press-and-hold 12-key pad whose hold duration is sent as one RFC4733 event. **File → Exit** and the visible **EXIT** button use the normal cleanup path.

 It provides fields for domain/registrar/authentication, transport, local SIP port, identity mode, STUN/ICE/SRTP, caller-ID domain, and outbound proxy. Active calls must be ended before a profile change can restart the SIP engine.

## Removing a previous installation

The top-level builder can also remove a prior system installation:

```sh
./build.sh --uninstall
```

or choose **R) Remove Install** from the interactive menu. The selected `--prefix` is respected (default `/usr/local`). The uninstall removes the installed CLI/GUI executables and S.I.P.H.E.R.'s installed share/documentation directories.

User credentials, settings, logs, and diagnostic state are **preserved by default**. Interactive uninstall offers an additional purge prompt. For a non-interactive full purge:

```sh
./build.sh --uninstall --purge-user-data
```

This removes the current user's standard S.I.P.H.E.R. config/state directories in addition to the system installation. It deliberately does not remove the local PJSIP build or arbitrary custom files outside those directories.

## WaffleHouse-style top-level builder

The normal entry point is:

```sh
chmod +x build.sh
./build.sh
```

The builder is POSIX `/bin/sh` and supports both Linux and FreeBSD. **Automatic dependency preflight is enabled by default.** Before compiling, it checks the selected target's required compiler/tools/libraries and packet-capture support. If system packages are missing, the builder warns that root privileges are required, obtains `sudo`/`doas`/`su` access, installs the missing packages, and re-checks the host before continuing. Compilation itself still runs as the normal user.

With no build-target arguments it opens an interactive selector:

```text
Select one, several, or all actions:

  1) S.I.P.H.E.R.-CLI       C++ terminal SIP softphone
  2) S.I.P.H.E.R.-GUI       C++ / Qt 6 desktop softphone
  A) CLI + GUI             Build both editions
  P) Rebuild PJSIP         Force local PJSIP 2.17 / 64-call dependency build
  X) Clean                 Remove S.I.P.H.E.R. build directories first
  D) Dependencies          Show host dependency information
  H) Audio Diagnose        Run audio/HDA preflight
  C) Capture Permissions   Configure non-root SIP/RTP capture access
  R) Remove Install        Remove installed S.I.P.H.E.R. from the selected prefix
  Q) Quit

Examples: 1     1,2     A     P,A     X A     R
```

The same builder supports non-interactive operation:

```sh
./build.sh --cli
./build.sh --gui
./build.sh --all
./build.sh --pjsip
./build.sh --pjsip --all
./build.sh --all --clean
./build.sh --all --dry-run
./build.sh --all --install
./build.sh --all --no-install
./build.sh --deps
./build.sh --audio-diagnose
./build.sh --configure-capture
./build.sh --uninstall
./build.sh --uninstall --purge-user-data
./build.sh --all --no-auto-deps
```

Additional options:

```text
--prefix PATH         system install prefix (default /usr/local)
--pjsip-prefix PATH   local PJSIP install prefix
--auto-deps           install missing system dependencies automatically (default)
--no-auto-deps        report missing dependencies but do not install them
--audio-diagnose      inspect OS/PJSIP audio devices without changing audio configuration
--configure-capture   install/configure non-root SIP/RTP packet-capture permissions
--no-audio-fix        skip recognized automatic audio repair during a normal build
--uninstall           remove the installed S.I.P.H.E.R. files from --prefix
--purge-user-data     with --uninstall, also remove current-user config/state
-h, --help            show complete builder help
```

System installation is **opt-in**, but **dependency installation is automatic by default**. At startup the builder prints a root/dependency warning. Root privileges are requested only if packages are actually missing (or if you later choose system installation). The builder uses root directly when already root, otherwise `sudo`, then `doas`, then `su` as a fallback. Compilation and the local PJSIP build never run as root.

The dependency check is target-aware: a CLI-only build does not install Qt, while a GUI build verifies/installs Qt 6 Widgets/platform support. The PCAP feature also verifies that `dumpcap` or `tcpdump` is available.

The older command remains as a compatibility wrapper:

```sh
./scripts/build-trunkmonkey.sh
```

With no arguments it builds both CLI and GUI without installing them.

## PJSIP requirement for 50 calls

PJSUA2 runtime `maxCalls` must be at or below the library's compile-time `PJSUA_MAX_CALLS`. S.I.P.H.E.R. 2.0.0 requires at least 50. The included PJSIP helper sets:

```c
#define PJSUA_MAX_CALLS 64
#define PJ_IOQUEUE_MAX_HANDLES 256
```

The top-level builder automatically adds its standard local PJSIP prefix to `PKG_CONFIG_PATH`. **You no longer need to remember `--pjsip` on a fresh machine.** If CLI or GUI is selected, the builder validates the discovered PJSIP installation with a small PJSUA2/audio link probe using the complete static `pkg-config` flags and verifies that the compile-time call ceiling is at least 50. If PJSIP is missing, stale, incompatible, or its static dependency chain cannot link, the builder automatically rebuilds S.I.P.H.E.R.'s local PJSIP 2.17 tree with `PJSUA_MAX_CALLS=64` and `PJ_IOQUEUE_MAX_HANDLES=256` before continuing.

Normal first and later builds are therefore both:

```sh
./build.sh --all
```

Use `--pjsip` only when you explicitly want to force/rebuild the local PJSIP dependency.

## Automatic dependency handling

On supported package-manager paths, you normally **do not install prerequisites manually**. The builder detects and installs what the selected target needs:

- Debian / Ubuntu / Linux Mint: `apt-get`
- Fedora / RHEL-family systems with DNF: `dnf`
- FreeBSD: `pkg`

For Debian/Ubuntu/Linux Mint, the mapped base packages are `build-essential`, `cmake`, `pkg-config`, `git`, `libasound2-dev`, `libssl-dev`, `uuid-dev`, and `tcpdump`; GUI builds add `qt6-base-dev` and `qt6-qpa-plugins`. For Fedora/DNF, equivalent GCC/ALSA/OpenSSL/libuuid/Qt 6 development packages are used. On FreeBSD, the builder installs `cmake`, `pkgconf`, `git`, `gmake`, `portaudio`, `opus`, `bcg729`, and `libuuid`; GUI builds additionally install `qt6-base`. The compiler, OpenSSL, and `tcpdump` are treated as base-system facilities and are verified directly.

If the machine uses an unsupported Linux package manager, the builder stops with the missing requirements rather than guessing package names. Use `--no-auto-deps` when you deliberately want a check-only build environment.

## Unix portability and stability audit

The earlier Unix beta received a full Linux/FreeBSD source audit after the first live Linux builds exposed static-link issues. The refresh includes:

- Complete static PJSIP dependency consumption through `pkg-config --libs --static libpjproject`, plus an early PJSUA2/audio link probe before the main build.
- Automatic rebuild of a missing, stale, incompatible, or incompletely linked local PJSIP installation. Local builds are stamped with PJSIP version, operating-system family, CPU architecture, PIC configuration, and S.I.P.H.E.R.'s build recipe.
- PJSIP built with `-fPIC`; FreeBSD uses `gmake`, `kqueue`, and external PortAudio.
- Exact PJSIP 2.17 source-tag verification before reusing an existing bootstrap checkout.
- Explicit SIP account transport binding so the profile's UDP/TCP/TLS selection controls registration and calls.
- Safer queue-test worker registration/unregistration with PJLIB and deterministic cancellation/join during shutdown.
- SIP wire-monitor shutdown synchronization, bounded unmatched-dialog buffering, bounded raw SIP retention, and late SIP Call-ID correlation.
- `posix_spawn()` packet-capture launch instead of post-thread `fork()` logic, with Linux/FreeBSD capture-tool path handling and graceful stop escalation.
- Per-user Unix runtime paths: configuration under `$XDG_CONFIG_HOME/trunkmonkey` (normally `~/.config/trunkmonkey`) and logs/state under `$XDG_STATE_HOME/trunkmonkey` (normally `~/.local/state/trunkmonkey`). App-owned directories are private and profile/log/SIP-trace files are created with owner-only permissions.
- Guards that preserve completed-call SIP/history data without invoking live PJSUA2 operations after a call has disconnected.
- Correct media-active reporting for non-foreground queue calls: media state is independent from whether a call is attached to the local headset.

### Runtime files on Unix

Default locations are:

```text
Profile/config:  ~/.config/trunkmonkey/
Settings:        ~/.config/trunkmonkey/trunkmonkey.ini
Logs/state:      ~/.local/state/trunkmonkey/
```

`XDG_CONFIG_HOME`, `XDG_STATE_HOME`, and `S.I.P.H.E.R._PROFILE` are honored when set to usable paths. This keeps an optional `/usr/local/bin` installation read-only and avoids trying to write configuration beside the executable.

## Quick start

From a freshly extracted package on Linux or FreeBSD:

```sh
chmod +x build.sh
./build.sh
```

For the first build, simply select:

```text
A
```

The builder checks/installs missing system dependencies first and automatically builds the local 64-call PJSIP dependency if it is not already available, then builds both S.I.P.H.E.R. clients.

Create a SIP profile:

```sh
cp examples/profile.conf.example my-sip.conf
$EDITOR my-sip.conf
```

Run the CLI after a combined build:

```sh
./build/all/sipher my-sip.conf
```

Run the GUI:

```sh
./build/all/sipher-gui my-sip.conf
```

If you build only one edition, the corresponding output is under `build/cli/` or `build/gui/`.

## Profile notes

Bare destinations are converted to:

```text
sip:<destination>@<sip_domain>
```

Bare caller-ID values are converted to:

```text
sip:<caller-id>@<caller_id_domain>
```

Complete SIP/SIPS URIs are accepted as-is. Authentication credentials remain independent of the requested outbound identity.

`identity_mode` selects how the requested caller identity is presented to the next-hop SIP system:

```text
from
pai
rpid
from+pai
```

Your PBX/SBC/provider remains authoritative and may accept, normalize, rewrite, or reject the identity according to its configuration.

## CLI examples

Normal softphone call:

```text
tm> dial 3305551212
```

Inspect the media endpoint and live SIP transaction log:

```text
tm> media 0
tm> siplog 0
tm> sipraw 0 3
```

Record readable raw SIP messages:

```text
tm> siptrace-start 0 call-0-sip.log
...
tm> siptrace-stop 0
```

Capture SIP and RTP packets on Linux's `any` interface:

```text
tm> sipcap-start 0 call-0-sip.pcapng any
tm> rtpcap-start 0 call-0-rtp.pcapng any
tm> capture-status
tm> capture-stop all
```

Normal call with a requested identity:

```text
tm> dial 3305551212 3305552001
```

Five independent calls to one queue, 500 ms apart:

```text
tm> blast 5 500 3305551200 3305552001
```

Twenty independent calls using text lists:

```text
tm> blast-file 20 250 examples/destinations.txt examples/callerids.txt
```

Inspect and select calls:

```text
tm> calls
tm> foreground 3
tm> stats 3
tm> hold 3
tm> resume 3
tm> dtmf 3 1234#
tm> hangup 3
```

## GUI themes

S.I.P.H.E.R. 2.0.0 includes the shared GUI/CLI theme family:

- System
- Hacker
- Matrix
- Phosphor
- Midnight
- Amber
- Ice
- Classic Light

Use the Theme selector in the main window. The selection is saved with Qt `QSettings` to the local `config/trunkmonkey.ini` file and restored the next time S.I.P.H.E.R. starts. System clears the application stylesheet and follows the desktop/Qt platform theme.

## Later targets

- GUI account/profile editor.
- Audio-device and codec selector.
- Incoming-call popup/ringtone.
- Call history.
- Structured RTP quality metrics: packet loss, jitter, RTT, R-factor/MOS where available.
- CSV/JSON queue-test export.
- Sensible per-call diagnostic drill-down for selected Queue-test calls without mixing trace data.
- Optional WAV/media injection per **individual** test call.
- Automated queue-test call lifetime / teardown policies.
- Multiple simultaneous SIP accounts.
- System tray.

## PJSIP licensing

PJSIP is separately licensed (GPLv2-or-later or a commercial/proprietary license option). Review its terms before distributing S.I.P.H.E.R. binaries. S.I.P.H.E.R.'s own project license has not been selected yet.

### Static PJSIP dependency verification
On Unix, the builder now verifies the exact static PJSIP dependency chain before starting the S.I.P.H.E.R. compile. It uses `pkg-config --cflags --libs --static libpjproject`, matching PJSIP's installed-static-library usage guidance. This prevents late linker failures caused by omitted `Libs.private` dependencies such as codec, ALSA, SRTP, UUID, echo-cancellation, and crypto libraries.

## First install profile seeding

When a CLI and/or GUI build is installed system-wide through `build.sh`, the builder creates the current user's default profile at `~/.config/trunkmonkey/profile.conf` (or the XDG / `S.I.P.H.E.R._PROFILE` override) **after the install succeeds**. It copies the installed `profile.conf.example` and never overwrites an existing profile. The installed CLI/GUI can then launch into S.I.P.H.E.R.'s built-in SIP Profile editor without requiring the user to create the file manually.

### If the source directory was moved or deleted while a terminal was open

Desktop file managers can move an open project directory into Trash while the shell prompt continues to display the old logical path. S.I.P.H.E.R. now detects this and refuses to build from a physical Trash path. `cd` out of the old terminal directory and enter a freshly extracted S.I.P.H.E.R. directory before running `./build.sh`.

The PJSIP helper also scrubs generated GNU-build state from S.I.P.H.E.R.'s cached `third_party/pjproject` checkout before reconfiguration. This prevents stale absolute paths in PJSIP's generated `build.mak` from breaking rebuilds after a source directory is renamed or moved.


### FreeBSD LOCALBASE static linking
FreeBSD ports/packages are normally installed under `LOCALBASE` (default `/usr/local`). The builder preserves pkg-config system library flags and explicitly supplies `$LOCALBASE/lib` when validating and linking static PJSIP, so ports-provided audio/codec/helper libraries can be resolved. Set `LOCALBASE` before running `build.sh` if your FreeBSD installation uses a non-default local prefix.

### Unix r11 PJSIP hardening (2026-08-15)
- FreeBSD PJSIP/PJSUA2 and S.I.P.H.E.R. are pinned to the base Clang/libc++ ABI; mixed libstdc++ metadata is rejected.
- FreeBSD PJSIP uses external PortAudio but no longer forces experimental kqueue. FreeBSD PJSIP requires PortAudio, Opus, bcg729, and libuuid explicitly; UPnP and legacy libwebrtc remain disabled for deterministic static linking.
- Local PJSIP compatibility stamps include the FreeBSD ABI/compiler version so OS/toolchain upgrades trigger a safe rebuild.
- Shutdown uses PJSIP 2.17 Account::shutdown2(), drains calls before account/endpoint teardown, and rejects new calls while stopping.
- Multi-call queue launches require at least 50 ms spacing to avoid unsupported high-concurrency INVITE bursts.
- The managed PJSIP sets `PJ_IOQUEUE_MAX_HANDLES=256` alongside `PJSUA_MAX_CALLS=64`, so the library has enough I/O queue capacity for the 50-call test ceiling.


### r11 PJSIP hardening completion
- S.I.P.H.E.R. now requires its stamped managed PJSIP 2.17 build instead of silently consuming an arbitrary system libpjproject.
- The managed PJSIP source applies a narrow **PJSUA2 Call lifetime compatibility guard** so a delayed `pj::Call` destructor cannot clear a reused call slot or touch PJSUA after shutdown.
- PJSIP is built with `make lib` rather than the default all-target, avoiding failures in sample/test executables S.I.P.H.E.R. does not ship or use.
- The FreeBSD bootstrap explicitly requires PortAudio, Opus, bcg729, and libuuid, while disabling WebRTC AEC, UPnP, AMR, SILK, and video helpers. Bundled G.711/G.722/GSM/Speex/iLBC remain available.
- The PJSIP compatibility stamp is v9 so older local builds are rebuilt once with the r11 ABI/I/O-queue recipe.

### Windows 11 CI build

The source tree includes `.github/workflows/windows11-portable.yml`. When the project is hosted on GitHub, **Actions → Windows 11 Portable Build → Run workflow** builds and regression-tests the Windows 10/11 x64 portable folder on a native Windows runner and publishes it as a downloadable workflow artifact containing `sipher.exe` and `sipher-gui.exe` plus their runtime dependencies.
