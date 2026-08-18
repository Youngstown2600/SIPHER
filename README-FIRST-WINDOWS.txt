S.I.P.H.E.R. By GITSC 1.0.0 r7 - Windows 10/11 x64 portable build kit

FAST PATH
---------
On a Windows 10/11 x64 BUILD machine, right-click BUILD-PORTABLE.ps1 and run with PowerShell.
It can bootstrap MSYS2 if needed, installs missing compiler/Qt/tool dependencies, builds PJSIP 2.17, builds both sipher.exe and sipher-gui.exe, runs tests, and stages the portable runtime under dist/.

r7 adds automatic RTP/RTCP Wireshark decoding: capture RTP or the combined call, stop the PCAP, then use OPEN LAST PCAP (AUTO RTP). S.I.P.H.E.R. launches Wireshark with the dynamic media ports already decoded.

This source kit itself does NOT contain precompiled Windows EXEs because it was generated in a Linux build environment without a Windows compiler/runtime. The included builder is the reproducible path that creates the actual portable EXEs on Windows.

Target: Windows 10/11 x64 using Qt 6.
Both targets use the same S.I.P.H.E.R. SIP/PJSIP core and feature set.

Windows 7 note: build the Win7 target on Windows 10/11. The resulting Win7 build targets _WIN32_WINNT=0x0601. Win7 PCAP capture requires a compatible installed capture driver because Win7 has no pktmon.

WINDOWS 11 CI BUILD (NO LOCAL COMPILER REQUIRED)
-----------------------------------------------
If this source is in GitHub, open Actions -> Windows 11 Portable Build -> Run workflow.
The included .github/workflows/windows11-portable.yml builds/tests on a native Windows
runner and publishes the complete portable x64 folder as a workflow artifact.

