## r14 — FreeBSD automatic audio compatibility

A normal FreeBSD CLI/GUI build now checks `snd_hda` before compiling. For a simple laptop codec with exactly one fixed Speaker and one headphone jack, r14 can safely test the FreeBSD-native Speaker/Headphones grouping automatically. The headphone is placed in the firmware Speaker association at `seq=15`; FreeBSD uses that sequence for headphone duplication and jack-triggered speaker auto-mute.

Safeguards are built in: custom HDA hints are never overridden, PulseAudio is released only for the short HDA rebuild, playback/capture PCM availability is validated, failed changes are rolled back, and only a validated result is written to `/boot/device.hints` after a timestamped backup. Desktops with Line-out/multiple analog outputs are not retasked. Use `./build.sh --audio-diagnose` for a read-only report or `./build.sh --no-audio-fix ...` to opt out.

## r13 — Change PBX dial prefix from Main

**GUI:** Main → **Dial prefix**. Enter values such as `9` or `4071`; leave blank for none. No profile reload is required.

**CLI:** the Main account panel shows the active prefix. Use `prefix 4071`, `prefix off`, or choose **Place a call** and edit the prefix at the prompt.

The prefix applies only to plain dial strings. Explicit SIP URIs are left untouched.

# S.I.P.H.E.R. Operator Guide


## PBX dial prefix

If Asterisk/FreePBX requires access/routing digits before the called number, edit **Dial prefix** directly on the GUI Main page or use `prefix <value>` in the CLI. The SIP profile's `dial_prefix` is only an optional startup default. Example: with the live prefix `4071`, dialing `3306651498` sends the call to `sip:40713306651498@<PBX>`. Use `prefix off` or leave the GUI field blank for no prefix. Explicit `sip:`/`sips:` URIs and `user@domain` destinations are left unchanged.

SIP diagnostics label outbound messages **SENT →** and inbound messages **← RECEIVED** so you can verify the exact signaling S.I.P.H.E.R. transmitted.
S.I.P.H.E.R. 1.0 keeps the full TrunkMonkey 2.0 r20 engine but makes the terminal interface usable without memorizing commands.

## Start here

Run `sipher` after installation, or run the built binary from `build/all/` or `build/cli/`. The Main screen shows a numbered **Operator Menu**. Type a number and press Enter.

| Choice | Workflow | Use it for |
|---|---|---|
| 1 | Place a call | Normal outbound test call with optional caller ID |
| 2 | Manage active calls | Answer, hang up, hold/resume, foreground, mute, DTMF, report |
| 3 | Queue / call-blast test | Single destination or list-based load tests, with optional WAV/MP3/etc. audio |
| 4 | Call diagnostics & packet capture | RTP/media stats, SIP ladder, SIP/RTP/combined PCAP, report export |
| 5 | PBX / SIP security audit | Authorized PBX discovery and bounded audit workflows |
| 6 | Audio & registration | Show/select microphone and playback devices, registration history |
| 7 | SIP account / profile | View, edit, or reload the SIP account |
| 8 | Themes & display | Select any of the 16 retained CLI themes |
| 9 | Logs & capture utilities | PJSIP engine log, capture status/interfaces, stop captures, SIP log |
| 10 | Advanced commands | Complete original command reference |
| 0 | Exit | Clean shutdown |

Type `menu` at any time to reopen the guided menu. Advanced users can type the original commands directly at the same `select>` prompt.

## Tier-1 call test workflow

1. Confirm the registration panel says **REGISTERED**.
2. Select **1 — Place a call** and enter the destination.
3. Use **2 — Manage active calls** for hold, mute, DTMF, or hangup.
4. Use **4 — Call diagnostics & packet capture** to review RTP quality or create a combined PCAP.
5. Choose **Export diagnostic report** when escalation needs a text summary.

For an RTP-only capture, use **OPEN LAST PCAP (AUTO RTP)** in the GUI or `pcap-open <id> <file>` in the CLI after stopping the capture. S.I.P.H.E.R. supplies Wireshark with the call's RTP/RTCP Decode As mappings automatically. A combined call PCAP remains preferable when signaling context is useful.

## When capture fails

The normal builder configures capture permissions. Repair them without rebuilding:

```sh
./build.sh --configure-capture
```

Linux uses least-privilege capture capabilities. FreeBSD uses persistent BPF/devfs access. S.I.P.H.E.R. itself should run as the normal user.

## When the wrong microphone is used

Open **6 — Audio & registration** and list/select the PJSIP devices. For deeper host diagnostics:

```sh
./build.sh --audio-diagnose
```

The verified FreeBSD ALC236/VREF80 repair and automatic headset-microphone routing from the TrunkMonkey core are retained.

## PBX audit safety

PBX audit workflows are for systems you own or are explicitly authorized to test. Active probes can trigger IDS/IPS, alarms, rate limits, or PBX protection controls. The security-audit functions remain bounded and preserve the safety limits from TrunkMonkey 2.0 r20.


## Unix/Linux audio output selection

S.I.P.H.E.R. can switch the PJSIP playback device at runtime without changing the active microphone. This is useful for moving call audio among built-in speakers, USB headsets, HDMI/DisplayPort outputs, and other audio devices exposed to PJSIP.

- **GUI (Unix/Linux):** `Settings -> Audio Output...` lists playback-capable devices and marks the active selection. Applying a selection changes output only.
- **CLI guided menu:** choose `Audio devices & registration history -> Choose audio output device`.
- **CLI command:** run `audio-devices` to list IDs, then `audio-output <playback-id>`.
- **Startup override:** `SIPHER_PLAYBACK_DEVICE=<id> sipher` (or `sipher-gui`) still selects an output before registration.

The existing `audio-use <capture-id> <playback-id>` command and GUI `Audio Devices...` dialog remain available when both microphone and playback routing need to be changed.

## r11 Linux / Unix audio routing

For Linux desktop systems using PipeWire, select `ALSA / pipewire` for both capture and playback when available. r11 normally selects it automatically unless `SIPHER_CAPTURE_DEVICE` / `SIPHER_PLAYBACK_DEVICE` (or legacy aliases) explicitly choose another device.

Useful CLI commands:

```text
audio-status
audio-devices
audio-reopen
audio-refresh
audio-output <playback-id>
audio-use <capture-id> <playback-id>
```

`audio-reopen` performs a real PJSIP close/reopen and reattaches the foreground call. In the dashboard CLI and GUI, Linux system-route changes detected through `pactl` automatically invoke the same recovery path when the selected devices follow PipeWire/default policy. If `pactl` is unavailable, automatic route watching is disabled but manual reopen remains available.


## r12 automatic headset / audio-device switching

Automatic switching is enabled by default on Linux and FreeBSD. Use `audio-auto off` to disable it for a session and `audio-auto on` to enable it again. `audio-status` reports the watcher backend and route.

Linux uses PipeWire/PulseAudio (`pactl`) sink/source port state. FreeBSD prefers PulseAudio state when available; otherwise it monitors `hw.snd.default_unit`, `/dev/sndstat`, and the active `mixer -d <unit> -s` recording source. On a route change S.I.P.H.E.R. detaches the foreground call from the local sound bridge, closes PJSIP audio, refreshes devices, reopens audio, verifies the sound device, and reattaches the call without sending SIP BYE or redialing.

On FreeBSD snd_hda systems where internal speakers and headphones are in the same output association, headphone speaker-auto-mute is performed by the kernel. S.I.P.H.E.R. intentionally does not rewrite pin associations during a call.
