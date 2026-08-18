# S.I.P.H.E.R. Portable Windows Builds

**S.I.P.H.E.R. By GITSC — SIP Inspection, Protocol Handling, Enumeration & Recon**

The Windows port uses the same S.I.P.H.E.R. SIP/PJSIP core, GUI workflows, CLI commands, security-audit code, RTP diagnostics, queue testing and 26-theme set as the Linux/FreeBSD release.

## Two targets

### Windows 7 SP1 x64

Build with:

```bat
build-windows-portable.cmd win7
```

This target selects Qt 5 and `_WIN32_WINNT=0x0601`. The portable application data stays in the release folder. The native Win7 console does not support the same VT/ANSI rendering as current Windows consoles, so the CLI automatically falls back to clean non-ANSI rendering when VT cannot be enabled.

**Packet capture:** Windows 7 has no native `pktmon`. PCAP capture needs a compatible packet-capture driver (Npcap/WinPcap class) installed on the host. If Wireshark/dumpcap exists on the build machine, the staging script also copies its user-space dumpcap runtime into the portable package. This driver requirement affects packet capture only, not SIP calls or the audit/test engine.

### Windows 10/11 x64

Build with:

```bat
build-windows-portable.cmd win10
```

This target selects Qt 6. The Windows CLI enables VT rendering when the console supports it. For PCAP capture, S.I.P.H.E.R. can use `dumpcap` when available or the Windows `pktmon` fallback.

## Build host

Use a current Windows 10/11 x64 machine with MSYS2 installed at `C:\msys64`. The builder runs from the MSYS2 MINGW64 environment and automatically installs missing build dependencies with `pacman` unless `--no-install-deps` is passed to `windows/build-portable.sh`.

The builder automatically downloads/builds **PJSIP 2.17**, preserving S.I.P.H.E.R.'s 64-call PJSUA configuration and Call-slot lifetime guard.

## Automatic RTP decode in Wireshark (r7)

After an RTP-only or combined call capture is stopped, the GUI's **OPEN LAST PCAP (AUTO RTP)** button launches `Wireshark.exe` with the current call's dynamic RTP and RTCP ports supplied as Decode As rules. The CLI equivalent is `pcap-open <id> <file>`. This avoids the manual **Analyze → Decode As → RTP** step while leaving the capture as a standard PCAP/PCAPNG file.

S.I.P.H.E.R. looks for `Wireshark.exe` on PATH and in the normal 64-bit/32-bit Wireshark install directories. Wireshark itself is not redistributed in the portable application package.

## Produced portable folder

The result is staged under `dist/`:

```text
SIPHER-1.0.0-Windows...-Portable-x64/
  sipher-gui.exe
  sipher.exe
  SIPHER-GUI.cmd
  SIPHER-CLI.cmd
  Qt*.dll
  platforms/
  tools/
    ffmpeg.exe
    curl.exe
    openssl.exe
    ...helper DLLs...
  data/
    config/
    state/
    tmp/
  examples/
  docs/
```

`SIPHER-GUI.cmd` and `SIPHER-CLI.cmd` set `SIPHER_PORTABLE_ROOT` and add the local `tools` folder to `PATH`. The executables are also compiled with portable-path support, so S.I.P.H.E.R. keeps profiles, themes, logs, diagnostics, CVE cache, and temporary queue-audio files in the local `data` tree instead of `%APPDATA%`.

## Feature parity

See `FEATURE-PARITY.md`. The one OS-level limitation is Win7 packet capture driver availability; the S.I.P.H.E.R. code paths themselves remain present.

## Security audit scope

The PBX assessment functionality remains bounded and intended for systems you own or are explicitly authorized to assess. It fingerprints SIP/PBX banners/capabilities, correlates disclosed component/version information with NIST NVD and Exploit-DB metadata, and runs bounded protocol/resilience tests. It does not add password cracking, destructive crash payloads, or automatic system compromise.
