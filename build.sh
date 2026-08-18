#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILDER_REVISION="sipher-r6-20260818-version-header-fix"

# A shell can keep an old logical $PWD after a desktop file manager moves the
# directory to Trash. Building from that relocated inode is especially unsafe
# for PJSIP because its generated GNU make files contain absolute source paths.
# Refuse that situation early and make the remedy obvious.
case "$ROOT_DIR" in
  */.local/share/Trash/*|*/.Trash/*|*/Trash/files/*)
    if [ "${TRUNKMONKEY_ALLOW_TRASH_SOURCE:-0}" != 1 ]; then
      echo "ERROR: S.I.P.H.E.R. is being run from a directory that is physically inside Trash:" >&2
      echo "  $ROOT_DIR" >&2
      echo >&2
      echo "Your shell prompt may still show the old directory name after a file manager moved it." >&2
      echo "Open/cd into a freshly extracted S.I.P.H.E.R. directory and run ./build.sh there." >&2
      echo "Set TRUNKMONKEY_ALLOW_TRASH_SOURCE=1 only if this is intentional." >&2
      exit 2
    fi
    ;;
esac

BUILD_CLI=0
BUILD_GUI=0
BUILD_PJSIP=0
CLEAN=0
DRY_RUN=0
SHOW_DEPS=0
AUDIO_DIAG_ONLY=0
CONFIGURE_CAPTURE_ONLY=0
AUTO_AUDIO_FIX=1
UNINSTALL=0
PURGE_USER_DATA=0
SELECTED_BY_FLAG=0
AUTO_DEPS=1
INSTALL_MODE=ask
INSTALL_PREFIX=${INSTALL_PREFIX:-/usr/local}
USER_HOME=${HOME:-$ROOT_DIR}
case "${XDG_CONFIG_HOME:-}" in
  /) USER_CONFIG_BASE=/ ;;
  /*) USER_CONFIG_BASE=${XDG_CONFIG_HOME%/} ;;
  *) USER_CONFIG_BASE="$USER_HOME/.config" ;;
esac
case "${XDG_STATE_HOME:-}" in
  /) USER_STATE_BASE=/ ;;
  /*) USER_STATE_BASE=${XDG_STATE_HOME%/} ;;
  *) USER_STATE_BASE="$USER_HOME/.local/state" ;;
esac
DEFAULT_PROFILE_PATH="$USER_CONFIG_BASE/trunkmonkey/profile.conf"
PROFILE_TARGET=${TRUNKMONKEY_PROFILE:-$DEFAULT_PROFILE_PATH}
PJSIP_PREFIX=${PJSIP_PREFIX:-$USER_HOME/.local/trunkmonkey-pjsip}
PJSIP_SOURCE_DIR=${PJSIP_SOURCE_DIR:-$ROOT_DIR/third_party/pjproject}
PRIV_METHOD=
PKG_MANAGER=
PKGCONF_BIN=
SYSTEM_PACKAGES=
MISSING_DESCRIPTIONS=

HOST_OS=$(uname -s)
case "$HOST_OS" in
  Linux) OS_FAMILY=linux ;;
  FreeBSD) OS_FAMILY=freebsd ;;
  *) echo "Unsupported OS: $HOST_OS (supported: Linux, FreeBSD)" >&2; exit 2 ;;
esac
HOST_ARCH=$(uname -m 2>/dev/null || echo unknown)
if [ "$OS_FAMILY" = freebsd ]; then
  FREEBSD_CC=/usr/bin/cc
  FREEBSD_CXX=/usr/bin/c++
  FREEBSD_ABI=$(uname -K 2>/dev/null || uname -r 2>/dev/null || echo unknown)
  FREEBSD_CXX_VERSION=$($FREEBSD_CXX --version 2>/dev/null | sed -n '1s/.*clang version \([^ ]*\).*/\1/p')
  [ -n "$FREEBSD_CXX_VERSION" ] || FREEBSD_CXX_VERSION=unknown
  FREEBSD_CXX_VERSION=$(printf '%s' "$FREEBSD_CXX_VERSION" | tr -c 'A-Za-z0-9._-' '_')
  PJSIP_BUILD_ID="2.17-tm64-pic-${OS_FAMILY}-${HOST_ARCH}-libcxx-${FREEBSD_ABI}-${FREEBSD_CXX_VERSION}-v9"
else
  FREEBSD_CC=
  FREEBSD_CXX=
  PJSIP_BUILD_ID="2.17-tm64-pic-${OS_FAMILY}-${HOST_ARCH}-v9"
fi
# FreeBSD ports/packages are normally rooted at LOCALBASE (/usr/local).
# Keep this separate from S.I.P.H.E.R.'s own install prefix: PJSIP may link
# against PortAudio/codecs installed by pkg(8) even when S.I.P.H.E.R. itself
# is installed elsewhere.
if [ "$OS_FAMILY" = freebsd ]; then
  FREEBSD_LOCALBASE=${LOCALBASE:-/usr/local}
else
  FREEBSD_LOCALBASE=
fi

if command -v nproc >/dev/null 2>&1; then
  JOBS=$(nproc)
elif command -v sysctl >/dev/null 2>&1; then
  JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 1)
else
  JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
fi
case "$JOBS" in ''|*[!0-9]*) JOBS=1 ;; esac
[ "$JOBS" -gt 0 ] 2>/dev/null || JOBS=1

logo() {
cat <<'LOGO'
============================================================
                    S.I.P.H.E.R. 1.0.0
          SIP / RTP / PBX DIAGNOSTICS — LINUX + FREEBSD
============================================================
LOGO
}

root_warning() {
cat <<'EOF2'

============================================================
 DEPENDENCY / ROOT NOTICE
============================================================
S.I.P.H.E.R. checks this host for required build/runtime dependencies before
compiling. If required system packages are missing, this builder will install
them automatically and WILL request root privileges via sudo, doas, or su.

The builder also configures least-privilege packet capture, verifies ffmpeg/OpenSSL
for queue audio and PBX/TLS diagnostics, and may apply a recognized, hardware-specific
audio repair after backing up system files.
Compilation and the local PJSIP build remain unprivileged; S.I.P.H.E.R. itself
should not be run as root.
EOF2
}

usage() {
  cat <<EOF2
Usage: ./build.sh [--cli] [--gui] [--all] [--pjsip] [--clean]
                  [--deps] [--audio-diagnose] [--configure-capture] [--dry-run] [--install | --no-install]
                  [--uninstall] [--purge-user-data]
                  [--auto-deps | --no-auto-deps]
                  [--prefix PATH] [--pjsip-prefix PATH]

Detected host: $HOST_OS

With no build-target option, an interactive multi-select menu is shown.
Selections may be comma- or space-separated (for example: 1,2 or P A).

Build targets:
  --cli             Build S.I.P.H.E.R. CLI
  --gui             Build S.I.P.H.E.R. Qt GUI
  --all             Build CLI + GUI
  --pjsip           Force/bootstrap PJSIP 2.17 with PJSUA_MAX_CALLS=64
  --clean           Remove S.I.P.H.E.R. build directories before building
  --deps            Show host dependency requirements
  --audio-diagnose  Run the host audio preflight (standalone or with a build)
  --configure-capture Configure non-root SIP/RTP packet-capture permissions only
  --no-audio-fix    Diagnose audio but do not apply recognized safe audio repairs
  --dry-run         Show actions without changing/building anything
  --uninstall       Remove an installed S.I.P.H.E.R. from --prefix
  --purge-user-data With --uninstall, also remove this user's S.I.P.H.E.R. config/state

Dependency handling:
  --auto-deps       Automatically install missing system packages (default)
  --no-auto-deps    Check dependencies but never install packages

Installation:
  --install         Install selected S.I.P.H.E.R. binaries after a successful build
  --no-install      Do not install system-wide
  --prefix PATH     Installation prefix (default: /usr/local)
  --pjsip-prefix P  PJSIP local install prefix (default: $PJSIP_PREFIX)

PJSIP source directory:
  $PJSIP_SOURCE_DIR

If CLI/GUI is selected and libpjproject is not found, the builder automatically
bootstraps S.I.P.H.E.R.'s local 64-call PJSIP after system dependencies pass.

After a successful CLI/GUI system install, the builder also creates:
  $PROFILE_TARGET
from the installed profile.conf.example (falling back to the source-tree example)
with private permissions. Existing profiles are never overwritten.
EOF2
}

find_pkgconf() {
  if command -v pkg-config >/dev/null 2>&1; then
    PKGCONF_BIN=$(command -v pkg-config)
  elif command -v pkgconf >/dev/null 2>&1; then
    PKGCONF_BIN=$(command -v pkgconf)
  else
    PKGCONF_BIN=
  fi
}

detect_package_manager() {
  if [ "$OS_FAMILY" = freebsd ]; then
    if command -v pkg >/dev/null 2>&1; then PKG_MANAGER=freebsd-pkg; else PKG_MANAGER=none; fi
    return
  fi

  if command -v apt-get >/dev/null 2>&1; then
    PKG_MANAGER=apt
  elif command -v dnf >/dev/null 2>&1; then
    PKG_MANAGER=dnf
  else
    PKG_MANAGER=none
  fi
}

show_host_requirements() {
  detect_package_manager
  echo
  echo "Builder revision:  $BUILDER_REVISION"
echo "Host platform:     $HOST_OS | $JOBS build job(s)"
  echo "Package manager:   $PKG_MANAGER"
  if [ "$OS_FAMILY" = linux ]; then
    case "$PKG_MANAGER" in
      apt)
        echo "Debian/Ubuntu/Mint base: build-essential cmake pkg-config git libasound2-dev libssl-dev uuid-dev tcpdump libcap2-bin ffmpeg openssl curl"
        echo "GUI addition:             qt6-base-dev qt6-qpa-plugins"
        ;;
      dnf)
        echo "Fedora/RHEL base: gcc-c++ make cmake pkgconf-pkg-config git alsa-lib-devel openssl-devel libuuid-devel tcpdump libcap ffmpeg-free openssl curl"
        echo "GUI addition:     qt6-qtbase-devel"
        ;;
      *)
        echo "Linux requires: C++17 compiler, make, cmake, pkg-config/pkgconf, git, curl, ALSA development headers, OpenSSL development headers, Qt 6 Widgets for GUI, and dumpcap or tcpdump for PCAP capture."
        ;;
    esac
  else
    echo "FreeBSD packages: cmake pkgconf git gmake portaudio opus bcg729 libuuid ffmpeg curl"
    echo "GUI addition:     qt6-base"
    echo "Compiler:         FreeBSD base Clang/libc++ (/usr/bin/cc + /usr/bin/c++)."
    echo "OpenSSL and tcpdump are provided by the FreeBSD base system."
    echo "Audio:            PJSIP uses external PortAudio (OSS backend on FreeBSD)."
    echo "PJSIP ABI:         base Clang/libc++; PortAudio + Opus + G.729 + libuuid are required; UPnP/WebRTC disabled."
  fi
  echo "PJSIP:             managed local PJSIP 2.17 is validated/auto-bootstrapped for S.I.P.H.E.R."
  echo "PJSIP prefix:      $PJSIP_PREFIX"
  echo "Install prefix:    $INSTALL_PREFIX"
  echo "User profile:      $PROFILE_TARGET"
}

select_token() {
  token=$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')
  case "$token" in
    1|cli) BUILD_CLI=1 ;;
    2|gui|g) BUILD_GUI=1 ;;
    a|all|'*') BUILD_CLI=1; BUILD_GUI=1 ;;
    p|pjsip) BUILD_PJSIP=1 ;;
    x|clean) CLEAN=1 ;;
    d|deps|dependencies) SHOW_DEPS=1 ;;
    h|audio|audio-diagnose) AUDIO_DIAG_ONLY=1 ;;
    c|capture|capture-permissions) CONFIGURE_CAPTURE_ONLY=1 ;;
    r|remove|uninstall) UNINSTALL=1 ;;
    q|quit|exit) exit 0 ;;
    '') ;;
    *) echo "Unknown selection: $1" >&2; return 1 ;;
  esac
}

interactive_select() {
  if [ -t 1 ] && command -v clear >/dev/null 2>&1; then clear || true; fi
  logo
  root_warning
  show_host_requirements
  cat <<'EOF2'

Select one, several, or all actions:

  1) Terminal interface    Guided CLI + advanced command mode
  2) Desktop interface     Qt 6 GUI
  A) Build both            Recommended
  P) Rebuild PJSIP         Force local PJSIP 2.17 / 64-call dependency build
  X) Clean                 Remove S.I.P.H.E.R. build directories first
  D) Dependencies          Show host dependency information
  H) Audio Diagnose        Run audio/HDA preflight
  C) Capture Permissions   Configure non-root SIP/RTP capture access
  R) Remove Install        Remove installed S.I.P.H.E.R. from the selected prefix
  Q) Quit

Examples: press Enter for both     1     2     A     H     C     R

NOTE: P is no longer required on a fresh host. Choosing 1, 2, or A will
      automatically validate/build S.I.P.H.E.R.'s managed PJSIP 2.17 dependency.
EOF2
  printf 'Selection [A]: '
  IFS= read -r answer
  [ -n "$answer" ] || answer=A
  answer=$(printf '%s' "$answer" | tr ',' ' ')
  for token in $answer; do
    select_token "$token" || exit 2
  done

  if [ "$BUILD_CLI" -eq 0 ] && [ "$BUILD_GUI" -eq 0 ] && \
     [ "$BUILD_PJSIP" -eq 0 ] && [ "$CLEAN" -eq 0 ] && [ "$SHOW_DEPS" -eq 0 ] && [ "$AUDIO_DIAG_ONLY" -eq 0 ] && [ "$CONFIGURE_CAPTURE_ONLY" -eq 0 ] && [ "$UNINSTALL" -eq 0 ]; then
    exit 0
  fi
}

run_cmd() {
  if [ "$DRY_RUN" -eq 1 ]; then
    printf '  [dry-run]'
    for arg in "$@"; do printf ' %s' "$arg"; done
    echo
    return 0
  fi
  "$@"
}

prepare_privileges_for() {
  purpose=$1

  if [ "$DRY_RUN" -eq 1 ]; then
    echo "[dry-run] Would request root privileges for $purpose."
    PRIV_METHOD=dry-run
    return 0
  fi

  if [ -n "$PRIV_METHOD" ] && [ "$PRIV_METHOD" != dry-run ]; then
    case "$PRIV_METHOD" in
      sudo) sudo -v ;;
      doas) doas true ;;
    esac
    return 0
  fi

  echo
  echo "============================================================"
  echo " ROOT ACCESS REQUIRED: $purpose"
  echo "============================================================"
  echo "The privileged operation is limited to: $purpose"
  echo "S.I.P.H.E.R. compilation and local PJSIP compilation remain unprivileged."

  if [ "$(id -u)" -eq 0 ]; then
    PRIV_METHOD=root
    echo "Already running as root; no password is required."
    return 0
  fi

  if command -v sudo >/dev/null 2>&1; then
    PRIV_METHOD=sudo
    echo "sudo detected. Enter your sudo password if prompted:"
    sudo -v
    return 0
  fi

  if command -v doas >/dev/null 2>&1; then
    PRIV_METHOD=doas
    echo "doas detected. Enter your password if prompted:"
    doas true
    return 0
  fi

  if command -v su >/dev/null 2>&1; then
    PRIV_METHOD=su
    echo "No sudo/doas helper found. Enter the root password if prompted:"
    if [ "$OS_FAMILY" = freebsd ]; then
      su -m root -c true
    else
      su root -c true
    fi
    echo "Note: su may request the root password again for subsequent privileged commands."
    return 0
  fi

  echo "Cannot continue: no sudo, doas, or su command is available for $purpose." >&2
  return 1
}

run_privileged() {
  if [ "$DRY_RUN" -eq 1 ] || [ "$PRIV_METHOD" = dry-run ]; then
    printf '  [dry-run] privileged:'
    for arg in "$@"; do printf ' %s' "$arg"; done
    echo
    return 0
  fi

  case "$PRIV_METHOD" in
    root) "$@" ;;
    sudo) sudo "$@" ;;
    doas) doas "$@" ;;
    su)
      cmd=''
      for arg in "$@"; do
        escaped=$(printf "%s" "$arg" | sed "s/'/'\\\\''/g")
        cmd="$cmd '$escaped'"
      done
      if [ "$OS_FAMILY" = freebsd ]; then
        su -m root -c "$cmd"
      else
        su root -c "$cmd"
      fi
      ;;
    *) echo "Privilege method was not initialized." >&2; return 1 ;;
  esac
}

add_system_package() {
  pkgname=$1
  case " $SYSTEM_PACKAGES " in
    *" $pkgname "*) ;;
    *) SYSTEM_PACKAGES="$SYSTEM_PACKAGES $pkgname" ;;
  esac
}

add_missing_description() {
  desc=$1
  if [ -z "$MISSING_DESCRIPTIONS" ]; then
    MISSING_DESCRIPTIONS=$desc
  else
    MISSING_DESCRIPTIONS="$MISSING_DESCRIPTIONS; $desc"
  fi
}

add_package_group() {
  for pkgname in "$@"; do add_system_package "$pkgname"; done
}

map_missing_to_packages() {
  item=$1
  detect_package_manager
  case "$PKG_MANAGER:$item" in
    apt:compiler) add_package_group build-essential ;;
    apt:cmake) add_package_group cmake ;;
    apt:pkgconfig) add_package_group pkg-config ;;
    apt:git) add_package_group git ;;
    apt:make) add_package_group build-essential ;;
    apt:alsa) add_package_group libasound2-dev ;;
    apt:openssl) add_package_group libssl-dev ;;
    apt:uuid) add_package_group uuid-dev ;;
    apt:qt6) add_package_group qt6-base-dev qt6-qpa-plugins ;;
    apt:qpa) add_package_group qt6-qpa-plugins ;;
    apt:capture) add_package_group tcpdump libcap2-bin ;;
    apt:captool) add_package_group libcap2-bin ;;
    apt:ffmpeg) add_package_group ffmpeg ;;
    apt:opensslcli) add_package_group openssl ;;
    apt:curl) add_package_group curl ;;

    dnf:compiler) add_package_group gcc-c++ ;;
    dnf:cmake) add_package_group cmake ;;
    dnf:pkgconfig) add_package_group pkgconf-pkg-config ;;
    dnf:git) add_package_group git ;;
    dnf:make) add_package_group make ;;
    dnf:alsa) add_package_group alsa-lib-devel ;;
    dnf:openssl) add_package_group openssl-devel ;;
    dnf:uuid) add_package_group libuuid-devel ;;
    dnf:qt6) add_package_group qt6-qtbase-devel ;;
    dnf:qpa) add_package_group qt6-qtbase-gui ;;
    dnf:capture) add_package_group tcpdump libcap ;;
    dnf:captool) add_package_group libcap ;;
    dnf:ffmpeg) add_package_group ffmpeg-free ;;
    dnf:opensslcli) add_package_group openssl ;;
    dnf:curl) add_package_group curl ;;

    freebsd-pkg:cmake) add_package_group cmake ;;
    freebsd-pkg:pkgconfig) add_package_group pkgconf ;;
    freebsd-pkg:git) add_package_group git ;;
    freebsd-pkg:gmake) add_package_group gmake ;;
    freebsd-pkg:portaudio) add_package_group portaudio ;;
    freebsd-pkg:opus) add_package_group opus ;;
    freebsd-pkg:bcg729) add_package_group bcg729 ;;
    freebsd-pkg:uuid) add_package_group libuuid ;;
    freebsd-pkg:qt6) add_package_group qt6-base ;;
    freebsd-pkg:qpa) add_package_group qt6-base ;;
    freebsd-pkg:ffmpeg) add_package_group ffmpeg ;;
    freebsd-pkg:opensslcli) add_package_group openssl ;;
    freebsd-pkg:curl) add_package_group curl ;;
    *) ;;
  esac
}

mark_missing() {
  item=$1
  desc=$2
  add_missing_description "$desc"
  map_missing_to_packages "$item"
}

apt_pkg_installed() {
  command -v dpkg-query >/dev/null 2>&1 || return 1
  dpkg-query -W -f='${Status}\n' "$1" 2>/dev/null | grep -q '^install ok installed$'
}

capture_tool_available() {
  command -v dumpcap >/dev/null 2>&1 || command -v tcpdump >/dev/null 2>&1 || \
    [ -x /usr/sbin/tcpdump ] || [ -x /usr/local/bin/dumpcap ] || [ -x /usr/local/sbin/dumpcap ]
}

qt6_widgets_available() {
  if [ -n "$PKGCONF_BIN" ] && "$PKGCONF_BIN" --exists Qt6Widgets 2>/dev/null; then
    return 0
  fi
  command -v cmake >/dev/null 2>&1 || return 1
  compiler_id=GNU
  [ "$OS_FAMILY" = freebsd ] && compiler_id=Clang
  probe_dir=$(mktemp -d "${TMPDIR:-/tmp}/trunkmonkey-qt-probe.XXXXXX") || return 1
  if (cd "$probe_dir" && cmake --find-package -DNAME=Qt6Widgets -DCOMPILER_ID="$compiler_id" -DLANGUAGE=CXX -DMODE=EXIST >/dev/null 2>&1); then
    rm -rf "$probe_dir"
    return 0
  fi
  rm -rf "$probe_dir"
  return 1
}

audio_warn() {
  echo "  [WARN] $*"
}

audio_info() {
  echo "  [INFO] $*"
}

run_audio_preflight() {
  echo
  echo "============================================================"
  echo " TRUNKMONKEY AUDIO PREFLIGHT"
  echo "============================================================"
  echo "This check never rewrites mixer settings, HDA pin mappings, loader hints,"
  echo "or PulseAudio/PipeWire configuration. It only reports potential problems."

  if [ "$OS_FAMILY" = freebsd ]; then
    echo "  Platform: FreeBSD OSS / snd_hda"

    if [ -r /dev/sndstat ]; then
      sndstat_out=$(cat /dev/sndstat 2>/dev/null || true)
      echo "  FreeBSD PCM devices:"
      printf '%s\n' "$sndstat_out" | grep '^pcm[0-9][0-9]*:' | sed 's/^/    /' || true
      play_count=$(printf '%s\n' "$sndstat_out" | grep '^pcm[0-9][0-9]*:' | grep -Ec '\((play|play/rec)\)' || true)
      rec_count=$(printf '%s\n' "$sndstat_out" | grep '^pcm[0-9][0-9]*:' | grep -Ec '\((rec|play/rec)\)' || true)
      case "$play_count" in ''|*[!0-9]*) play_count=0 ;; esac
      case "$rec_count" in ''|*[!0-9]*) rec_count=0 ;; esac
      [ "$play_count" -gt 0 ] || audio_warn "No FreeBSD playback PCM device is visible."
      if [ "$rec_count" -eq 0 ]; then
        audio_warn "No FreeBSD recording/capture PCM device is visible."
        audio_warn "PJSIP calls may fail with PJMEDIA_EAUD_NODEFDEV when it opens capture+playback."
      fi
    else
      audio_warn "/dev/sndstat is not readable; unable to inspect FreeBSD PCM devices."
    fi

    default_unit=$(sysctl -n hw.snd.default_unit 2>/dev/null || true)
    [ -n "$default_unit" ] && audio_info "FreeBSD default PCM unit: pcm$default_unit"

    if [ -n "${sndstat_out:-}" ]; then
      rec_only=$(printf '%s\n' "$sndstat_out" | grep '^pcm[0-9][0-9]*:' | grep -E '\(rec\)' || true)
      capture_total=$(printf '%s\n' "$sndstat_out" | grep '^pcm[0-9][0-9]*:' | grep -Ec '\((rec|play/rec)\)' || true)
      case "$capture_total" in ''|*[!0-9]*) capture_total=0 ;; esac
      if [ "$capture_total" -gt 1 ] && [ -n "$rec_only" ]; then
        audio_warn "Multiple capture-capable PCM devices detected while the default is pcm${default_unit:-?}."
        audio_warn "Applications that use the default capture device may select the internal microphone instead of a dedicated headset/external mic:"
        printf '%s\n' "$rec_only" | sed 's/^/         /'
      fi
    fi

    codec_desc=$(sysctl -a 2>/dev/null | sed -n 's/^dev\.hdacc\.[0-9][0-9]*\.%desc: / /p' | sed 's/^ *//' | paste -sd ';' - 2>/dev/null || true)
    [ -n "$codec_desc" ] && audio_info "HDA codec(s): $codec_desc"

    hda_errors=$(dmesg 2>/dev/null | grep -Ei 'hdaa_audio_as_parse|wrong direction|duplicate pin|disabling association' | tail -20 || true)
    if [ -n "$hda_errors" ]; then
      audio_warn "snd_hda association errors were found in the kernel log:"
      printf '%s\n' "$hda_errors" | sed 's/^/         /'
    fi

    pin_overrides=$(sysctl -a 2>/dev/null | awk '
      /^dev\.hdaa\.[0-9]+\.nid[0-9]+_original:/ {
        key=$1; sub(/_original:$/, "", key); val=$0; sub(/^[^:]*:[[:space:]]*/, "", val); orig[key]=val
      }
      /^dev\.hdaa\.[0-9]+\.nid[0-9]+_config:/ {
        key=$1; sub(/_config:$/, "", key); val=$0; sub(/^[^:]*:[[:space:]]*/, "", val); cfg[key]=val
      }
      END { for (key in orig) if ((key in cfg) && orig[key] != cfg[key]) print key ": original=" orig[key] " | configured=" cfg[key] }
    ' || true)
    if [ -n "$pin_overrides" ]; then
      audio_warn "HDA runtime pin configuration differs from codec original values:"
      printf '%s\n' "$pin_overrides" | sort | sed 's/^/         /'
    fi

    boot_hints=
    for hint_file in /boot/device.hints /boot/loader.conf /boot/loader.conf.local; do
      if [ -r "$hint_file" ]; then
        found=$(grep -HnE '^[[:space:]]*hint\.(hdac|hdaa)\..*nid[0-9]+\.config' "$hint_file" 2>/dev/null || true)
        [ -z "$found" ] || boot_hints="${boot_hints}${boot_hints:+
}$found"
      fi
    done
    if [ -n "$boot_hints" ]; then
      audio_warn "Persistent HDA pin overrides were found (review if capture/playback associations look wrong):"
      printf '%s\n' "$boot_hints" | sed 's/^/         /'
    fi

    if printf '%s\n' "$codec_desc" | grep -qi 'ALC236'; then
      audio_info "Realtek ALC236 detected. Combo-jack headset mic routing can be OEM-specific."
      for hdaa_unit in $(sysctl -a 2>/dev/null | sed -n 's/^dev\.hdaa\.\([0-9][0-9]*\)\.nid[0-9][0-9]*: pin: Mic.*/\1/p' | sort -u); do
        init_clear=$(sysctl -n "dev.hdaa.${hdaa_unit}.init_clear" 2>/dev/null || true)
        [ -z "$init_clear" ] || audio_info "hdaa${hdaa_unit} init_clear=$init_clear"
        mic_nids=$(sysctl -a 2>/dev/null | sed -n "s/^dev\.hdaa\.${hdaa_unit}\.nid\([0-9][0-9]*\): pin: Mic.*/\1/p" | sort -n)
        for mic_nid in $mic_nids; do
          mic_info=$(sysctl "dev.hdaa.${hdaa_unit}.nid${mic_nid}" 2>/dev/null || true)
          printf '%s\n' "$mic_info" | grep -q 'conn=Jack' || continue
          pin_control=$(printf '%s\n' "$mic_info" | sed -n 's/^[[:space:]]*Pin control: \(0x[0-9A-Fa-f]*\).*/\1/p' | head -1)
          pin_cfg=$(printf '%s\n' "$mic_info" | sed -n 's/^[[:space:]]*Pin config: \(0x[0-9A-Fa-f]*\).*/\1/p' | head -1)
          audio_info "ALC236 jack mic: hdaa${hdaa_unit} nid${mic_nid} config=${pin_cfg:-unknown} pin-control=${pin_control:-unknown}"
          if [ "$pin_control" = "0x00000025" ] || [ "$pin_control" = "0x25" ]; then
            audio_warn "ALC236 jack mic is using VREF100 (pin control 0x25). On Lenovo subsystem 0x17aa390b we verified voice capture only after init_clear=1 + ivref80 produced pin control 0x24."
            audio_warn "Diagnostic only: do not auto-apply this to unrelated hardware; compare a known-working OS or OEM codec routing first."
          elif [ "$pin_control" = "0x00000024" ] || [ "$pin_control" = "0x24" ]; then
            audio_info "ALC236 jack mic is using VREF80 (0x24), matching the known-working Project-2501 headset-mic configuration."
          fi
        done
      done
    fi
  else
    echo "  Platform: Linux audio preflight"
    if command -v aplay >/dev/null 2>&1; then
      play_lines=$(aplay -l 2>/dev/null | grep '^card ' || true)
      [ -n "$play_lines" ] || audio_warn "ALSA reports no playback hardware through aplay -l."
    else
      audio_info "aplay is unavailable; skipping direct ALSA playback enumeration."
    fi
    if command -v arecord >/dev/null 2>&1; then
      rec_lines=$(arecord -l 2>/dev/null | grep '^card ' || true)
      [ -n "$rec_lines" ] || audio_warn "ALSA reports no capture hardware through arecord -l."
    else
      audio_info "arecord is unavailable; skipping direct ALSA capture enumeration."
    fi
  fi

  if command -v pactl >/dev/null 2>&1 && pactl info >/dev/null 2>&1; then
    pa_sink=$(pactl get-default-sink 2>/dev/null || true)
    pa_source=$(pactl get-default-source 2>/dev/null || true)
    [ -n "$pa_sink" ] && audio_info "PulseAudio default sink: $pa_sink"
    [ -n "$pa_source" ] && audio_info "PulseAudio default source: $pa_source"
    sink_count=$(pactl list short sinks 2>/dev/null | wc -l | tr -d ' ')
    source_count=$(pactl list short sources 2>/dev/null | grep -v '\.monitor[[:space:]]' | wc -l | tr -d ' ')
    case "$sink_count" in ''|*[!0-9]*) sink_count=0 ;; esac
    case "$source_count" in ''|*[!0-9]*) source_count=0 ;; esac
    [ "$sink_count" -gt 1 ] && audio_info "PulseAudio exposes multiple sinks ($sink_count); applications may keep a previously opened route."
    [ "$source_count" -gt 1 ] && audio_info "PulseAudio exposes multiple capture sources ($source_count); verify the intended headset/internal source."
  fi

  echo "  Preflight result: inspection complete."
}


configure_audio_fixes() {
  [ "$AUTO_AUDIO_FIX" -eq 1 ] || { audio_info "Automatic audio repair disabled (--no-audio-fix)."; return 0; }
  [ "$OS_FAMILY" = freebsd ] || return 0

  codec_desc=$(sysctl -a 2>/dev/null | sed -n 's/^dev\.hdacc\.[0-9][0-9]*\.%desc: / /p' | sed 's/^ *//' | paste -sd ';' - 2>/dev/null || true)
  printf '%s\n' "$codec_desc" | grep -qi 'ALC236' || return 0

  # This is intentionally a narrow hardware fingerprint derived from the
  # Project-2501/Lenovo ALC236 codec we verified against Linux. Unknown ALC236
  # layouts are diagnosed but never rewritten automatically.
  matched_unit=
  for unit in $(sysctl -a 2>/dev/null | sed -n 's/^dev\.hdaa\.\([0-9][0-9]*\)\.nid25_original:.*/\1/p' | sort -u); do
    n18=$(sysctl -n "dev.hdaa.${unit}.nid18_original" 2>/dev/null || true)
    n20=$(sysctl -n "dev.hdaa.${unit}.nid20_original" 2>/dev/null || true)
    n25=$(sysctl -n "dev.hdaa.${unit}.nid25_original" 2>/dev/null || true)
    n33=$(sysctl -n "dev.hdaa.${unit}.nid33_original" 2>/dev/null || true)
    case "$n18" in *0x90a60130*) ;; *) continue ;; esac
    case "$n20" in *0x90170120*) ;; *) continue ;; esac
    case "$n25" in *0x04a11040*) ;; *) continue ;; esac
    case "$n33" in *0x04211010*) ;; *) continue ;; esac
    matched_unit=$unit
    break
  done
  [ -n "$matched_unit" ] || {
    audio_info "ALC236 detected, but it does not match the verified Project-2501 pin fingerprint; no automatic HDA changes will be made."
    return 0
  }

  unit=$matched_unit
  mic_info=$(sysctl "dev.hdaa.${unit}.nid25" 2>/dev/null || true)
  pin_control=$(printf '%s\n' "$mic_info" | sed -n 's/^[[:space:]]*Pin control: \(0x[0-9A-Fa-f]*\).*/\1/p' | head -1)
  init_clear=$(sysctl -n "dev.hdaa.${unit}.init_clear" 2>/dev/null || true)
  config=$(sysctl -n "dev.hdaa.${unit}.config" 2>/dev/null || true)

  known_bad_hints=0
  if [ -r /boot/device.hints ] && \
     grep -Eq '^[[:space:]]*hint\.hdac\.0\.cad0\.nid18\.config="as=1 seq=0 device=Speaker"' /boot/device.hints && \
     grep -Eq '^[[:space:]]*hint\.hdac\.0\.cad0\.nid21\.config="as=1 seq=1 device=Headphones"' /boot/device.hints && \
     grep -Eq '^[[:space:]]*hint\.hdac\.0\.cad0\.nid25\.config="as=1 seq=2 device=Mic"' /boot/device.hints; then
    known_bad_hints=1
  fi

  needs_fix=0
  [ "$pin_control" = "0x00000024" ] || [ "$pin_control" = "0x24" ] || needs_fix=1
  [ "$init_clear" = "1" ] || needs_fix=1
  case ",$config," in *,ivref80,*) ;; *) needs_fix=1 ;; esac

  persist_ok=0
  if [ -r /etc/sysctl.conf ] && grep -q '^# BEGIN TRUNKMONKEY ALC236 HEADSET MIC$' /etc/sysctl.conf && \
     grep -q "^dev.hdaa.${unit}.init_clear=1$" /etc/sysctl.conf && \
     grep -q "^dev.hdaa.${unit}.config=forcestereo,ivref80$" /etc/sysctl.conf; then
    persist_ok=1
  fi

  [ "$needs_fix" -eq 1 ] || [ "$persist_ok" -eq 0 ] || [ "$known_bad_hints" -eq 1 ] || {
    audio_info "Verified ALC236 headset-mic repair is already active and persistent (VREF80 / pin control 0x24)."
    return 0
  }

  echo
  echo "==> Verified FreeBSD ALC236 headset-mic repair"
  echo "    Hardware fingerprint matches the Project-2501 layout."
  echo "    Current: init_clear=${init_clear:-?} config=${config:-?} pin-control=${pin_control:-?}"
  prepare_privileges_for "verified FreeBSD ALC236 headset-mic repair"
  if [ "$DRY_RUN" -eq 1 ]; then
    echo "  [dry-run] back up /etc/sysctl.conf and persist init_clear=1 + forcestereo,ivref80"
    [ "$known_bad_hints" -eq 0 ] || echo "  [dry-run] back up /boot/device.hints and disable the exact known-bad S.I.P.H.E.R.-era pin override trio"
    echo "  [dry-run] reconfigure hdaa${unit} and verify nid25 pin control 0x24"
    return 0
  fi

  stamp=$(date +%Y%m%d-%H%M%S)
  [ ! -e /etc/sysctl.conf ] || run_privileged cp -p /etc/sysctl.conf "/etc/sysctl.conf.trunkmonkey-backup-$stamp"
  tmp_sysctl=$(mktemp "${TMPDIR:-/tmp}/trunkmonkey-sysctl.conf.XXXXXX")
  if [ -r /etc/sysctl.conf ]; then
    awk '
      /^# BEGIN TRUNKMONKEY ALC236 HEADSET MIC$/ {skip=1; next}
      /^# END TRUNKMONKEY ALC236 HEADSET MIC$/ {skip=0; next}
      !skip {print}
    ' /etc/sysctl.conf > "$tmp_sysctl"
  fi
  {
    echo ""
    echo "# BEGIN TRUNKMONKEY ALC236 HEADSET MIC"
    echo "# Verified ALC236 combo-jack mic repair: VREF80, preserved across reboot."
    echo "dev.hdaa.${unit}.init_clear=1"
    echo "dev.hdaa.${unit}.config=forcestereo,ivref80"
    echo "dev.hdaa.${unit}.reconfig=1"
    echo "# END TRUNKMONKEY ALC236 HEADSET MIC"
  } >> "$tmp_sysctl"
  run_privileged install -m 0644 "$tmp_sysctl" /etc/sysctl.conf
  rm -f "$tmp_sysctl"

  if [ "$known_bad_hints" -eq 1 ]; then
    run_privileged cp -p /boot/device.hints "/boot/device.hints.trunkmonkey-backup-$stamp"
    tmp_hints=$(mktemp "${TMPDIR:-/tmp}/trunkmonkey-device.hints.XXXXXX")
    awk '
      /^hint\.hdac\.0\.cad0\.nid18\.config="as=1 seq=0 device=Speaker"$/ {print "# S.I.P.H.E.R. 1.0.0 disabled known-bad override: "$0; next}
      /^hint\.hdac\.0\.cad0\.nid21\.config="as=1 seq=1 device=Headphones"$/ {print "# S.I.P.H.E.R. 1.0.0 disabled known-bad override: "$0; next}
      /^hint\.hdac\.0\.cad0\.nid25\.config="as=1 seq=2 device=Mic"$/ {print "# S.I.P.H.E.R. 1.0.0 disabled known-bad override: "$0; next}
      {print}
    ' /boot/device.hints > "$tmp_hints"
    run_privileged install -m 0644 "$tmp_hints" /boot/device.hints
    rm -f "$tmp_hints"
    audio_warn "Known-bad boot HDA pin overrides were disabled. A reboot is recommended after this build so codec associations are rebuilt from the original pin map."
  fi

  run_privileged sysctl "dev.hdaa.${unit}.init_clear=1" >/dev/null
  run_privileged sysctl "dev.hdaa.${unit}.config=forcestereo,ivref80" >/dev/null
  run_privileged sysctl "dev.hdaa.${unit}.reconfig=1" >/dev/null
  mic_info=$(sysctl "dev.hdaa.${unit}.nid25" 2>/dev/null || true)
  pin_control=$(printf '%s\n' "$mic_info" | sed -n 's/^[[:space:]]*Pin control: \(0x[0-9A-Fa-f]*\).*/\1/p' | head -1)
  if [ "$pin_control" = "0x00000024" ] || [ "$pin_control" = "0x24" ]; then
    audio_info "ALC236 repair verified: nid25 is now IN + VREF80 (pin control 0x24)."
  else
    audio_warn "ALC236 repair was applied but nid25 did not verify as pin control 0x24; leaving backups in place for manual review."
  fi
}

detect_missing_dependencies() {
  SYSTEM_PACKAGES=
  MISSING_DESCRIPTIONS=
  find_pkgconf
  detect_package_manager

  if [ "$OS_FAMILY" = freebsd ]; then
    if [ ! -x "$FREEBSD_CC" ] || [ ! -x "$FREEBSD_CXX" ]; then
      mark_missing compiler "FreeBSD base Clang C/C++ compiler"
    elif ! "$FREEBSD_CXX" --version 2>/dev/null | grep -qi 'clang'; then
      mark_missing compiler "FreeBSD base Clang/libc++ toolchain"
    fi
  elif ! command -v c++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
    mark_missing compiler "C++17 compiler"
  fi
  command -v cmake >/dev/null 2>&1 || mark_missing cmake "CMake"
  [ -n "$PKGCONF_BIN" ] || mark_missing pkgconfig "pkg-config/pkgconf"
  command -v git >/dev/null 2>&1 || mark_missing git "Git"
  command -v ffmpeg >/dev/null 2>&1 || mark_missing ffmpeg "ffmpeg (queue/blast audio normalization)"
  command -v openssl >/dev/null 2>&1 || mark_missing opensslcli "OpenSSL command-line client (PBX TLS audit)"
  command -v curl >/dev/null 2>&1 || mark_missing curl "curl (NIST NVD / Exploit-DB metadata lookup)"

  if [ "$OS_FAMILY" = linux ]; then
    command -v make >/dev/null 2>&1 || mark_missing make "GNU make"
    if [ -z "$PKGCONF_BIN" ] || ! "$PKGCONF_BIN" --exists alsa 2>/dev/null; then
      mark_missing alsa "ALSA development files"
    fi
    if [ -z "$PKGCONF_BIN" ] || ! "$PKGCONF_BIN" --exists openssl 2>/dev/null; then
      mark_missing openssl "OpenSSL development files"
    fi
    if [ -z "$PKGCONF_BIN" ] || ! "$PKGCONF_BIN" --exists uuid 2>/dev/null; then
      mark_missing uuid "libuuid development files"
    fi
  else
    command -v gmake >/dev/null 2>&1 || mark_missing gmake "GNU make (gmake)"
    if [ -z "$PKGCONF_BIN" ] || ! "$PKGCONF_BIN" --exists portaudio-2.0 2>/dev/null; then
      mark_missing portaudio "PortAudio development files (FreeBSD audio backend)"
    fi
    if [ -z "$PKGCONF_BIN" ] || ! "$PKGCONF_BIN" --exists opus 2>/dev/null; then
      mark_missing opus "Opus development files (FreeBSD SIP codec)"
    fi
    if [ -z "$PKGCONF_BIN" ] || ! "$PKGCONF_BIN" --exists libbcg729 2>/dev/null; then
      mark_missing bcg729 "bcg729 development files (FreeBSD G.729 codec)"
    fi
    if [ -z "$PKGCONF_BIN" ] || ! "$PKGCONF_BIN" --exists uuid 2>/dev/null; then
      mark_missing uuid "libuuid development files (robust SIP GUID generation)"
    fi
  fi

  if [ "$BUILD_GUI" -eq 1 ]; then
    if ! qt6_widgets_available; then
      mark_missing qt6 "Qt 6 Core/Widgets development files"
    fi
    if [ "$PKG_MANAGER" = apt ] && ! apt_pkg_installed qt6-qpa-plugins; then
      mark_missing qpa "Qt 6 QPA platform plugins"
    fi
  fi

  if ! capture_tool_available; then
    if [ "$OS_FAMILY" = freebsd ]; then
      mark_missing capture "tcpdump/dumpcap packet capture tool (normally supplied by FreeBSD base)"
    else
      mark_missing capture "tcpdump/dumpcap packet capture tool"
    fi
  fi
  if [ "$OS_FAMILY" = linux ] && ! command -v setcap >/dev/null 2>&1; then
    mark_missing captool "Linux setcap utility for non-root packet capture"
  fi
}

install_missing_dependencies() {
  [ -n "$MISSING_DESCRIPTIONS" ] || return 0

  echo
  echo "==> Missing S.I.P.H.E.R. dependencies detected"
  echo "    $MISSING_DESCRIPTIONS"

  if [ -z "$(printf '%s' "$SYSTEM_PACKAGES" | tr -d ' ')" ]; then
    echo "No automatic package mapping is available for one or more missing dependencies on this host." >&2
    show_host_requirements >&2
    return 1
  fi

  echo "    Packages to install:$SYSTEM_PACKAGES"

  if [ "$AUTO_DEPS" -eq 0 ]; then
    echo "Automatic dependency installation is disabled (--no-auto-deps)." >&2
    echo "Install the packages above and rerun the builder." >&2
    return 1
  fi

  detect_package_manager
  if [ "$PKG_MANAGER" = none ]; then
    echo "No supported automatic package manager was detected." >&2
    show_host_requirements >&2
    return 1
  fi

  if [ "$DRY_RUN" -eq 1 ]; then
    case "$PKG_MANAGER" in
      apt)
        echo "  [dry-run] privileged: env DEBIAN_FRONTEND=noninteractive apt-get update"
        echo "  [dry-run] privileged: env DEBIAN_FRONTEND=noninteractive apt-get install -y$SYSTEM_PACKAGES"
        ;;
      dnf) echo "  [dry-run] privileged: dnf install -y$SYSTEM_PACKAGES" ;;
      freebsd-pkg) echo "  [dry-run] privileged: pkg install -y$SYSTEM_PACKAGES" ;;
    esac
    return 0
  fi

  prepare_privileges_for "S.I.P.H.E.R. dependency installation"
  case "$PKG_MANAGER" in
    apt)
      run_privileged env DEBIAN_FRONTEND=noninteractive apt-get update
      # Intentional word splitting: SYSTEM_PACKAGES is a controlled package-name list.
      # shellcheck disable=SC2086
      run_privileged env DEBIAN_FRONTEND=noninteractive apt-get install -y $SYSTEM_PACKAGES
      ;;
    dnf)
      # shellcheck disable=SC2086
      run_privileged dnf install -y $SYSTEM_PACKAGES
      ;;
    freebsd-pkg)
      # shellcheck disable=SC2086
      run_privileged pkg install -y $SYSTEM_PACKAGES
      ;;
    *)
      echo "Automatic dependency installation is not implemented for package manager: $PKG_MANAGER" >&2
      return 1
      ;;
  esac
}

ensure_system_dependencies() {
  echo
  echo "==> Checking S.I.P.H.E.R. system dependencies"
  detect_missing_dependencies

  if [ -z "$MISSING_DESCRIPTIONS" ]; then
    echo "    All required system dependencies are present."
    return 0
  fi

  install_missing_dependencies

  if [ "$DRY_RUN" -eq 1 ]; then
    echo "    [dry-run] Dependency preflight would re-check the host after installation."
    return 0
  fi

  hash -r 2>/dev/null || true
  detect_missing_dependencies
  if [ -n "$MISSING_DESCRIPTIONS" ]; then
    echo "Dependency installation completed, but these requirements are still missing:" >&2
    echo "  $MISSING_DESCRIPTIONS" >&2
    show_host_requirements >&2
    return 1
  fi
  echo "    Dependency installation/re-check passed."
}

ensure_capture_dependencies() {
  SYSTEM_PACKAGES=
  MISSING_DESCRIPTIONS=
  detect_package_manager

  if ! capture_tool_available; then
    if [ "$OS_FAMILY" = freebsd ]; then
      mark_missing capture "tcpdump/dumpcap packet capture tool (normally supplied by FreeBSD base)"
    else
      mark_missing capture "tcpdump/dumpcap packet capture tool"
    fi
  fi
  if [ "$OS_FAMILY" = linux ] && { ! command -v setcap >/dev/null 2>&1 || ! command -v getcap >/dev/null 2>&1; }; then
    mark_missing captool "Linux setcap/getcap utilities for non-root packet capture"
  fi

  [ -n "$MISSING_DESCRIPTIONS" ] || return 0
  install_missing_dependencies
  [ "$DRY_RUN" -eq 1 ] && return 0
  hash -r 2>/dev/null || true
  if ! capture_tool_available; then
    echo "ERROR: no dumpcap/tcpdump capture tool is available after dependency setup." >&2
    return 1
  fi
  if [ "$OS_FAMILY" = linux ] && { ! command -v setcap >/dev/null 2>&1 || ! command -v getcap >/dev/null 2>&1; }; then
    echo "ERROR: Linux setcap/getcap are still unavailable after dependency setup." >&2
    return 1
  fi
}

configure_capture_permissions() {
  [ "${TRUNKMONKEY_SKIP_CAPTURE_PERMS:-0}" = 1 ] && return 0

  echo
  echo "============================================================"
  echo " PACKET CAPTURE PERMISSION SETUP"
  echo "============================================================"
  echo "S.I.P.H.E.R. configures the capture helper/device instead of running the"
  echo "softphone as root. Root access is requested only while permissions are set."

  tool=
  command -v dumpcap >/dev/null 2>&1 && tool=$(command -v dumpcap)
  [ -n "$tool" ] || { command -v tcpdump >/dev/null 2>&1 && tool=$(command -v tcpdump); }
  if [ -z "$tool" ]; then
    if [ "$DRY_RUN" -eq 1 ]; then
      tool=tcpdump
      echo "  [dry-run] capture helper would be resolved after dependency installation (using tcpdump for preview)."
    else
      echo "WARNING: no dumpcap/tcpdump capture tool is installed." >&2
      return 0
    fi
  fi

  if [ "$OS_FAMILY" = linux ]; then
    if ! command -v getcap >/dev/null 2>&1 || ! command -v setcap >/dev/null 2>&1; then
      if [ "$DRY_RUN" -eq 1 ]; then
        echo "  [dry-run] setcap cap_net_raw,cap_net_admin=eip $tool"
        return 0
      fi
      echo "WARNING: Linux setcap/getcap are unavailable; dependency installation should provide libcap tools." >&2
      return 1
    fi
    caps=$(getcap "$tool" 2>/dev/null || true)
    case "$caps" in
      *cap_net_raw*cap_net_admin*|*cap_net_admin*cap_net_raw*)
        echo "    Linux non-root capture already configured: $caps"
        return 0 ;;
    esac
    prepare_privileges_for "Linux non-root packet capture capability setup"
    if [ "$DRY_RUN" -eq 1 ]; then
      echo "  [dry-run] setcap cap_net_raw,cap_net_admin=eip $tool"
      return 0
    fi
    run_privileged setcap cap_net_raw,cap_net_admin=eip "$tool"
    caps=$(getcap "$tool" 2>/dev/null || true)
    case "$caps" in
      *cap_net_raw*cap_net_admin*|*cap_net_admin*cap_net_raw*)
        echo "    Linux non-root capture configured: $caps" ;;
      *) echo "ERROR: expected CAP_NET_RAW/CAP_NET_ADMIN were not visible on $tool" >&2; return 1 ;;
    esac
    return 0
  fi

  # FreeBSD uses /dev/bpf* for libpcap. Grant only the user who invoked the
  # builder access to BPF devices through a persistent devfs ruleset. If the
  # host already uses a system devfs ruleset, include it so existing policy is
  # preserved rather than replaced.
  capture_user=${SUDO_USER:-$(id -un)}
  [ -n "$capture_user" ] || capture_user=$(id -un)
  existing_name=$(sysrc -n devfs_system_ruleset 2>/dev/null || true)
  [ "$existing_name" = "NO" ] && existing_name=
  existing_id=
  if [ -n "$existing_name" ] && [ "$existing_name" != trunkmonkey_bpf ]; then
    existing_id=$(awk -v n="$existing_name" '
      $0 ~ "^\\[" n "=[0-9]+\\]" { line=$0; sub(/^.*=/,"",line); sub(/].*$/,"",line); print line; exit }
    ' /etc/devfs.rules /etc/defaults/devfs.rules 2>/dev/null || true)
  fi

  if [ -n "$existing_name" ] && [ "$existing_name" != trunkmonkey_bpf ] && [ -z "$existing_id" ]; then
    echo "ERROR: existing FreeBSD devfs_system_ruleset '$existing_name' could not be resolved to a numeric ruleset." >&2
    echo "       Refusing to replace it automatically. Review /etc/devfs.rules and rerun --configure-capture." >&2
    return 1
  fi

  ruleset_id=$(awk '/^\[trunkmonkey_bpf=[0-9]+\]/{x=$0;sub(/^.*=/,"",x);sub(/].*$/,"",x);print x;exit}' /etc/devfs.rules 2>/dev/null || true)
  if [ -z "$ruleset_id" ]; then
    ruleset_id=199
    used=$( { devfs rule showsets 2>/dev/null || true; sed -n 's/^\[[^=]*=\([0-9][0-9]*\)\].*/\1/p' /etc/devfs.rules /etc/defaults/devfs.rules 2>/dev/null || true; } | sort -nu )
    while printf '%s\n' "$used" | grep -qx "$ruleset_id"; do ruleset_id=$((ruleset_id-1)); [ "$ruleset_id" -ge 150 ] || { echo "ERROR: unable to reserve a devfs ruleset for S.I.P.H.E.R.." >&2; return 1; }; done
  fi

  tmp_rules=$(mktemp "${TMPDIR:-/tmp}/trunkmonkey-devfs.rules.XXXXXX")
  if [ -r /etc/devfs.rules ]; then
    awk '
      /^# BEGIN TRUNKMONKEY BPF$/ {skip=1; next}
      /^# END TRUNKMONKEY BPF$/ {skip=0; next}
      !skip {print}
    ' /etc/devfs.rules > "$tmp_rules"
  fi
  {
    echo "# BEGIN TRUNKMONKEY BPF"
    echo "# Managed by S.I.P.H.E.R. 1.0.0 for non-root SIP/RTP packet capture."
    echo "[trunkmonkey_bpf=$ruleset_id]"
    [ -z "$existing_id" ] || echo "add include $existing_id"
    echo "add path 'bpf*' user $capture_user mode 0600"
    echo "# END TRUNKMONKEY BPF"
  } >> "$tmp_rules"

  prepare_privileges_for "FreeBSD /dev/bpf packet capture permission setup"
  if [ "$DRY_RUN" -eq 1 ]; then
    echo "  [dry-run] install updated /etc/devfs.rules with ruleset trunkmonkey_bpf=$ruleset_id"
    echo "  [dry-run] sysrc devfs_system_ruleset=trunkmonkey_bpf"
    echo "  [dry-run] service devfs restart"
    rm -f "$tmp_rules"
    return 0
  fi

  stamp=$(date +%Y%m%d-%H%M%S)
  [ ! -e /etc/devfs.rules ] || run_privileged cp -p /etc/devfs.rules "/etc/devfs.rules.trunkmonkey-backup-$stamp"
  [ ! -e /etc/rc.conf ] || run_privileged cp -p /etc/rc.conf "/etc/rc.conf.trunkmonkey-backup-$stamp"
  run_privileged install -m 0644 "$tmp_rules" /etc/devfs.rules
  rm -f "$tmp_rules"
  run_privileged sysrc devfs_system_ruleset=trunkmonkey_bpf >/dev/null
  run_privileged service devfs restart >/dev/null

  bpf_nodes=$(ls /dev/bpf* 2>/dev/null | head -5 || true)
  if [ -n "$bpf_nodes" ]; then
    echo "    FreeBSD BPF capture access configured for user: $capture_user"
    ls -l /dev/bpf* 2>/dev/null | head -5 | sed 's/^/      /'
  else
    echo "    FreeBSD devfs rule installed; /dev/bpf* will inherit it when BPF nodes are created."
  fi
}

clean_builds() {
  echo
  echo "==> Cleaning S.I.P.H.E.R. build directories"
  if [ "$DRY_RUN" -eq 1 ]; then
    echo "  [dry-run] rm -rf $ROOT_DIR/build"
  else
    rm -rf "$ROOT_DIR/build"
  fi
}

activate_local_pjsip() {
  pcdir="$PJSIP_PREFIX/lib/pkgconfig"
  if [ -d "$pcdir" ]; then
    if [ -n "${PKG_CONFIG_PATH:-}" ]; then
      PKG_CONFIG_PATH="$pcdir:$PKG_CONFIG_PATH"
    else
      PKG_CONFIG_PATH="$pcdir"
    fi
    export PKG_CONFIG_PATH
  fi

  # FreeBSD pkgconf may classify LOCALBASE library directories as system
  # paths and omit their -L flags. That is unsafe for our static PJSIP link:
  # the base linker does not implicitly search every ports LOCALBASE. Preserve
  # the path reported by PJSIP/pkg-config and also use FREEBSD_LOCALBASE as a
  # fallback in the explicit link probe/CMake build below.
  if [ "$OS_FAMILY" = freebsd ]; then
    PKG_CONFIG_ALLOW_SYSTEM_LIBS=1
    export PKG_CONFIG_ALLOW_SYSTEM_LIBS
  fi
}

have_pjsip() {
  activate_local_pjsip
  find_pkgconf
  [ -n "$PKGCONF_BIN" ] || return 1
  "$PKGCONF_BIN" --exists libpjproject 2>/dev/null || return 1
  [ "$("$PKGCONF_BIN" --modversion libpjproject 2>/dev/null || true)" = "2.17" ] || return 1

  # S.I.P.H.E.R. deliberately uses its managed PJSIP build rather than
  # an arbitrary system libpjproject. The managed build guarantees the 64-call
  # ceiling, PIC, deterministic codec/audio options, and our PJSUA2 lifetime
  # compatibility fix. A system PJSIP may be ABI-compatible yet still violate
  # one of those runtime requirements.
  local_pc="$PJSIP_PREFIX/lib/pkgconfig/libpjproject.pc"
  [ -f "$local_pc" ] || return 1
  marker="$PJSIP_PREFIX/.trunkmonkey-pjsip-build"
  [ -f "$marker" ] || return 1
  [ "$(cat "$marker" 2>/dev/null || true)" = "$PJSIP_BUILD_ID" ] || return 1
  pc_prefix=$("$PKGCONF_BIN" --variable=prefix libpjproject 2>/dev/null || true)
  [ "$pc_prefix" = "$PJSIP_PREFIX" ] || return 1

  # S.I.P.H.E.R. links static PJSIP. A plain `pkg-config --libs` omits
  # Libs.private, which contains required codec/audio/crypto dependencies.
  static_flags=$("$PKGCONF_BIN" --libs --static libpjproject 2>/dev/null) || return 1
  if [ "$OS_FAMILY" = freebsd ]; then
    case " $static_flags " in
      *" -lstdc++ "*) return 1 ;;
    esac
    case " $static_flags " in
      *" -lc++ "*) ;;
      *) return 1 ;;
    esac
  fi
  return 0
}

validate_pjsip_static_link() {
  [ "$DRY_RUN" -eq 0 ] || {
    echo "    [dry-run] validate: pkg-config --cflags --libs --static libpjproject"
    return 0
  }

  activate_local_pjsip
  find_pkgconf
  [ -n "$PKGCONF_BIN" ] || return 1

  cxx=
  if [ "$OS_FAMILY" = freebsd ]; then
    cxx=$FREEBSD_CXX
    [ -x "$cxx" ] || return 1
    "$cxx" --version 2>/dev/null | grep -qi 'clang' || return 1
  else
    for candidate in c++ g++ clang++; do
      if command -v "$candidate" >/dev/null 2>&1; then cxx=$candidate; break; fi
    done
    [ -n "$cxx" ] || return 1
  fi

  tmpbase=${TMPDIR:-/tmp}
  tmplink=$(mktemp -d "$tmpbase/trunkmonkey-pjsip-link.XXXXXX") || return 1
  trap 'rm -rf "$tmplink"' 0 HUP INT TERM
  cat > "$tmplink/check.cpp" <<'TM_PJSIP_LINK_EOF'
#include <pjsua2.hpp>
#include <pjsua-lib/pjsua.h>
#include <iostream>
static_assert(PJSUA_MAX_CALLS >= 50, "S.I.P.H.E.R. requires PJSUA_MAX_CALLS >= 50");
static_assert(PJ_IOQUEUE_MAX_HANDLES >= 192, "S.I.P.H.E.R. requires PJ_IOQUEUE_MAX_HANDLES >= 192 for 64-call PJSIP");
int main()
{
    pj::Endpoint endpoint;
    endpoint.libCreate();
    pj::EpConfig config;
    config.uaConfig.maxCalls = 1;
    endpoint.libInit(config);
    try {
        auto &adm = endpoint.audDevManager();
        auto devices = adm.enumDev2();
        unsigned inputDevices = 0;
        unsigned outputDevices = 0;
        for (std::size_t i = 0; i < devices.size(); ++i) {
            const auto &dev = devices[i];
            if (dev.inputCount > 0) ++inputDevices;
            if (dev.outputCount > 0) ++outputDevices;
            std::cout << "TM_AUDIO_DEVICE id=" << i
                      << " driver=\"" << dev.driver << "\""
                      << " name=\"" << dev.name << "\""
                      << " input=" << dev.inputCount
                      << " output=" << dev.outputCount << "\n";
        }
        std::cout << "TM_AUDIO_COUNTS devices=" << devices.size()
                  << " input=" << inputDevices
                  << " output=" << outputDevices << "\n";
    } catch (const pj::Error &err) {
        std::cout << "TM_AUDIO_ERROR " << err.info() << "\n";
    }
    endpoint.libDestroy();
    return 0;
}
TM_PJSIP_LINK_EOF

  # Intentional word splitting: these flags are generated by pkg-config from
  # the trusted libpjproject.pc installed by the PJSIP build.
  pjcflags=$($PKGCONF_BIN --cflags libpjproject) || { rm -rf "$tmplink"; trap - 0 HUP INT TERM; return 1; }
  pjlibs=$($PKGCONF_BIN --libs --static libpjproject) || { rm -rf "$tmplink"; trap - 0 HUP INT TERM; return 1; }
  if [ "$OS_FAMILY" = freebsd ]; then
    case " $pjlibs " in
      *" -lstdc++ "*)
        echo "PJSIP FreeBSD ABI mismatch: libpjproject requests libstdc++, but S.I.P.H.E.R./Qt use base Clang/libc++." >&2
        echo "The local PJSIP must be rebuilt by this r11 builder." >&2
        rm -rf "$tmplink"; trap - 0 HUP INT TERM; return 1 ;;
    esac
  fi
  tm_extra_ldflags=
  if [ "$OS_FAMILY" = freebsd ]; then
    tm_extra_ldflags="-L$FREEBSD_LOCALBASE/lib"
  fi

  echo "    Verifying complete static PJSIP link chain..."
  if [ "$OS_FAMILY" = freebsd ]; then
    echo "    FreeBSD compiler/ABI: $FREEBSD_CXX (Clang/libc++)"
    echo "    FreeBSD LOCALBASE library path: $FREEBSD_LOCALBASE/lib"
  fi
  # shellcheck disable=SC2086
  if ! "$cxx" -std=c++17 $pjcflags "$tmplink/check.cpp" -o "$tmplink/check" $tm_extra_ldflags $pjlibs >/dev/null 2>"$tmplink/link.err"; then
    echo "PJSIP is installed, but its complete static dependency chain does not link." >&2
    echo "Command used by S.I.P.H.E.R.:" >&2
    echo "  $PKGCONF_BIN --cflags --libs --static libpjproject" >&2
    echo "Linker output:" >&2
    sed -n '1,120p' "$tmplink/link.err" >&2
    rm -rf "$tmplink"
    trap - 0 HUP INT TERM
    return 1
  fi

  echo "    Static PJSIP link verification passed."
  if "$tmplink/check" >"$tmplink/runtime.out" 2>&1; then
    audio_error=$(grep 'TM_AUDIO_ERROR' "$tmplink/runtime.out" | tail -1 || true)
    if [ -n "$audio_error" ]; then
      echo "    [WARN] PJSIP audio enumeration reported: $audio_error"
    fi
    device_lines=$(grep '^TM_AUDIO_DEVICE ' "$tmplink/runtime.out" || true)
    if [ -n "$device_lines" ]; then
      echo "    PJSIP audio devices:"
      printf '%s\n' "$device_lines" | sed 's/^TM_AUDIO_DEVICE /      /'
    fi
    counts=$(grep 'TM_AUDIO_COUNTS' "$tmplink/runtime.out" | tail -1 || true)
    if [ -n "$counts" ]; then
      echo "    PJSIP audio enumeration: $counts"
      pjin=$(printf '%s\n' "$counts" | sed -n 's/.* input=\([0-9][0-9]*\).*/\1/p')
      pjout=$(printf '%s\n' "$counts" | sed -n 's/.* output=\([0-9][0-9]*\).*/\1/p')
      [ "${pjin:-0}" -gt 0 ] 2>/dev/null || echo "    [WARN] PJSIP sees no capture device; calls may fail with PJMEDIA_EAUD_NODEFDEV."
      [ "${pjout:-0}" -gt 0 ] 2>/dev/null || echo "    [WARN] PJSIP sees no playback device; two-way media will not be available."
    fi
  else
    echo "    [WARN] PJSIP linked successfully but its runtime audio enumeration probe failed:" >&2
    sed -n '1,80p' "$tmplink/runtime.out" >&2
  fi
  rm -rf "$tmplink"
  trap - 0 HUP INT TERM
}

build_pjsip() {
  echo
  echo "==> Building local PJSIP dependency"
  echo "    Source: $PJSIP_SOURCE_DIR"
  echo "    Prefix: $PJSIP_PREFIX"
  if [ "$DRY_RUN" -eq 1 ]; then
    echo "  [dry-run] PJSIP_PREFIX=$PJSIP_PREFIX PJSIP_SOURCE_DIR=$PJSIP_SOURCE_DIR $ROOT_DIR/scripts/bootstrap-pjsip.sh"
  else
    if [ "$OS_FAMILY" = freebsd ]; then
      PJSIP_PREFIX="$PJSIP_PREFIX" PJSIP_SOURCE_DIR="$PJSIP_SOURCE_DIR" \
      TRUNKMONKEY_PJSIP_CC="$FREEBSD_CC" TRUNKMONKEY_PJSIP_CXX="$FREEBSD_CXX" \
        "$ROOT_DIR/scripts/bootstrap-pjsip.sh"
    else
      PJSIP_PREFIX="$PJSIP_PREFIX" PJSIP_SOURCE_DIR="$PJSIP_SOURCE_DIR" \
        "$ROOT_DIR/scripts/bootstrap-pjsip.sh"
    fi
  fi
  activate_local_pjsip
  if [ "$DRY_RUN" -eq 0 ]; then
    if ! have_pjsip; then
      echo "PJSIP build finished, but libpjproject/pkg-config metadata is unavailable." >&2
      return 1
    fi
    validate_pjsip_static_link || return 1
  fi
}

ensure_pjsip() {
  if [ "$BUILD_PJSIP" -eq 1 ]; then
    build_pjsip
    return 0
  fi

  if have_pjsip; then
    echo
    echo "==> Managed PJSIP dependency found; validating S.I.P.H.E.R. requirements"
    if [ "$DRY_RUN" -eq 1 ] || validate_pjsip_static_link; then
      return 0
    fi
    echo
    echo "==> Existing PJSIP is not compatible/link-complete; rebuilding the local S.I.P.H.E.R. PJSIP"
    build_pjsip
    return 0
  fi

  echo
  echo "==> PJSIP is missing/stale; automatically bootstrapping S.I.P.H.E.R. PJSIP 2.17 (64-call PIC build)"
  build_pjsip
}

check_build_dependencies() {
  if [ "$DRY_RUN" -eq 1 ]; then return 0; fi

  find_pkgconf
  if ! command -v cmake >/dev/null 2>&1; then
    echo "Required build tool disappeared after dependency preflight: cmake" >&2
    return 1
  fi
  if [ "$OS_FAMILY" = freebsd ]; then
    if [ ! -x "$FREEBSD_CXX" ] || ! "$FREEBSD_CXX" --version 2>/dev/null | grep -qi 'clang'; then
      echo "Required FreeBSD base Clang/libc++ compiler is unavailable: $FREEBSD_CXX" >&2
      return 1
    fi
  elif ! command -v c++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1 && ! command -v clang++ >/dev/null 2>&1; then
    echo "Required C++ compiler disappeared after dependency preflight." >&2
    return 1
  fi
  if [ -z "$PKGCONF_BIN" ]; then
    echo "pkg-config/pkgconf disappeared after dependency preflight." >&2
    return 1
  fi
  if ! have_pjsip; then
    echo "PJSIP/libpjproject is still unavailable after bootstrap or does not expose static link flags." >&2
    return 1
  fi
  if ! validate_pjsip_static_link; then
    echo "The local PJSIP installation is not link-complete. Rebuild it with: ./build.sh --pjsip" >&2
    return 1
  fi
  if [ "$BUILD_GUI" -eq 1 ] && ! qt6_widgets_available; then
    echo "Qt6Widgets is still unavailable after dependency preflight." >&2
    return 1
  fi
}

build_selected() {
  check_build_dependencies

  if [ "$BUILD_CLI" -eq 1 ] && [ "$BUILD_GUI" -eq 1 ]; then
    BUILD_DIR="$ROOT_DIR/build/all"
    GUI_OPT=ON
    CLI_OPT=ON
    LABEL="S.I.P.H.E.R. CLI + GUI"
  elif [ "$BUILD_GUI" -eq 1 ]; then
    BUILD_DIR="$ROOT_DIR/build/gui"
    GUI_OPT=ON
    CLI_OPT=OFF
    LABEL="S.I.P.H.E.R. GUI"
  else
    BUILD_DIR="$ROOT_DIR/build/cli"
    GUI_OPT=OFF
    CLI_OPT=ON
    LABEL="S.I.P.H.E.R. CLI"
  fi

  echo
  echo "==> Building $LABEL on $HOST_OS"
  if [ "$OS_FAMILY" = freebsd ]; then
    run_cmd cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DTRUNKMONKEY_BUILD_GUI="$GUI_OPT" \
      -DTRUNKMONKEY_BUILD_CLI="$CLI_OPT" \
      -DTRUNKMONKEY_BUILD_TESTS=ON \
      -DCMAKE_CXX_COMPILER="$FREEBSD_CXX" \
      -DTRUNKMONKEY_FREEBSD_LOCALBASE="$FREEBSD_LOCALBASE"
  else
    run_cmd cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DTRUNKMONKEY_BUILD_GUI="$GUI_OPT" \
      -DTRUNKMONKEY_BUILD_CLI="$CLI_OPT" \
      -DTRUNKMONKEY_BUILD_TESTS=ON
  fi
  run_cmd cmake --build "$BUILD_DIR" --parallel "$JOBS"
  run_cmd ctest --test-dir "$BUILD_DIR" --output-on-failure

  LAST_BUILD_DIR=$BUILD_DIR
  export LAST_BUILD_DIR

  echo
  echo "Build outputs:"
  if [ "$BUILD_CLI" -eq 1 ]; then echo "  $BUILD_DIR/sipher"; fi
  if [ "$BUILD_GUI" -eq 1 ]; then echo "  $BUILD_DIR/sipher-gui"; fi
}

seed_user_profile() {
  [ "$BUILD_CLI" -eq 1 ] || [ "$BUILD_GUI" -eq 1 ] || return 0

  if [ -f "$PROFILE_TARGET" ]; then
    echo "  Existing SIP profile preserved: $PROFILE_TARGET"
    return 0
  fi

  installed_example="$INSTALL_PREFIX/share/trunkmonkey/examples/profile.conf.example"
  source_example="$ROOT_DIR/examples/profile.conf.example"
  profile_source=$source_example
  if [ -r "$installed_example" ]; then
    profile_source=$installed_example
  fi

  echo
  echo "==> Creating first-run S.I.P.H.E.R. SIP profile"
  if [ "$DRY_RUN" -eq 1 ]; then
    echo "  [dry-run] mkdir -p $(dirname -- "$PROFILE_TARGET")"
    echo "  [dry-run] copy $installed_example -> $PROFILE_TARGET (fallback: $source_example; mode 600)"
    return 0
  fi

  if [ ! -r "$profile_source" ]; then
    echo "Unable to seed S.I.P.H.E.R. SIP profile: no readable profile.conf.example was found." >&2
    echo "Checked: $installed_example" >&2
    echo "         $source_example" >&2
    return 1
  fi

  profile_dir=$(dirname -- "$PROFILE_TARGET")
  if [ "$profile_dir" != "." ]; then
    mkdir -p "$profile_dir"
    chmod 700 "$profile_dir" 2>/dev/null || true
  fi

  # The profile belongs to the user running the builder, not root. Copy it
  # after the privileged system install has completed so ownership remains
  # correct even when /usr/local was installed through sudo/doas/su.
  cp "$profile_source" "$PROFILE_TARGET"
  chmod 600 "$PROFILE_TARGET" 2>/dev/null || true

  if [ ! -s "$PROFILE_TARGET" ]; then
    echo "S.I.P.H.E.R. SIP profile was not created correctly: $PROFILE_TARGET" >&2
    return 1
  fi

  echo "  Created generic SIP profile: $PROFILE_TARGET"
  echo "  Source template: $profile_source"
  echo "  Existing profiles will never be overwritten by future installs."
  echo "  Launch S.I.P.H.E.R. and use its SIP Profile editor to enter your account details."
}

uninstall_previous() {
  echo
  echo "==> Removing installed S.I.P.H.E.R. from $INSTALL_PREFIX"
  targets="$INSTALL_PREFIX/bin/sipher $INSTALL_PREFIX/bin/sipher-gui $INSTALL_PREFIX/bin/trunkmonkey-cli $INSTALL_PREFIX/bin/trunkmonkey-gui $INSTALL_PREFIX/share/trunkmonkey $INSTALL_PREFIX/share/doc/trunkmonkey"
  found=0
  for target in $targets; do [ -e "$target" ] && found=1; done
  if [ "$found" -eq 0 ]; then
    echo "  No installed S.I.P.H.E.R. files were found under $INSTALL_PREFIX."
  else
    prepare_privileges_for "removing installed S.I.P.H.E.R. from $INSTALL_PREFIX"
    for target in $targets; do
      if [ -e "$target" ]; then
        run_privileged rm -rf "$target"
        echo "  Removed: $target"
      fi
    done
  fi

  purge=$PURGE_USER_DATA
  if [ "$purge" -eq 0 ] && [ -t 0 ] && [ "$DRY_RUN" -eq 0 ]; then
    echo
    echo "User configuration and diagnostics are preserved by default:"
    echo "  $USER_CONFIG_BASE/trunkmonkey"
    echo "  $USER_STATE_BASE/trunkmonkey"
    printf 'Also remove this user\047s S.I.P.H.E.R. profile/settings/logs? [y/N] '
    IFS= read -r answer
    case "$answer" in y|Y|yes|YES|Yes) purge=1 ;; esac
  fi
  if [ "$purge" -eq 1 ]; then
    echo "  Removing user configuration/state..."
    if [ "$DRY_RUN" -eq 1 ]; then
      echo "  [dry-run] rm -rf $USER_CONFIG_BASE/trunkmonkey $USER_STATE_BASE/trunkmonkey"
    else
      rm -rf "$USER_CONFIG_BASE/trunkmonkey" "$USER_STATE_BASE/trunkmonkey"
    fi
  else
    echo "  User profile/settings/logs preserved."
  fi
}

ask_install() {
  [ "$BUILD_CLI" -eq 1 ] || [ "$BUILD_GUI" -eq 1 ] || { INSTALL_MODE=no; return 0; }
  [ "$INSTALL_MODE" = ask ] || return 0
  if [ ! -t 0 ]; then
    INSTALL_MODE=no
    return 0
  fi
  echo
  echo "System installation is OPTIONAL."
  echo "If selected, the built S.I.P.H.E.R. client(s) will be installed under $INSTALL_PREFIX/bin."
  printf 'Install selected client(s) into %s/bin after building? [y/N] ' "$INSTALL_PREFIX"
  IFS= read -r answer
  case "$answer" in
    y|Y|yes|YES|Yes) INSTALL_MODE=yes ;;
    *) INSTALL_MODE=no ;;
  esac
}

install_selected() {
  [ "$INSTALL_MODE" = yes ] || return 0
  [ -n "${LAST_BUILD_DIR:-}" ] || { echo "No successful S.I.P.H.E.R. build is available to install." >&2; return 1; }

  prepare_privileges_for "S.I.P.H.E.R. system installation into $INSTALL_PREFIX"

  echo
  echo "==> Installing selected S.I.P.H.E.R. clients"
  run_privileged cmake --install "$LAST_BUILD_DIR" --prefix "$INSTALL_PREFIX"
  if [ "$BUILD_CLI" -eq 1 ]; then echo "  Installed: $INSTALL_PREFIX/bin/sipher"; fi
  if [ "$BUILD_GUI" -eq 1 ]; then echo "  Installed: $INSTALL_PREFIX/bin/sipher-gui"; fi
  echo "  User profile: ${XDG_CONFIG_HOME:-$USER_HOME/.config}/trunkmonkey/profile.conf"
  echo "  Examples:     $INSTALL_PREFIX/share/trunkmonkey/examples"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --cli) BUILD_CLI=1; SELECTED_BY_FLAG=1 ;;
    --gui) BUILD_GUI=1; SELECTED_BY_FLAG=1 ;;
    --all) BUILD_CLI=1; BUILD_GUI=1; SELECTED_BY_FLAG=1 ;;
    --pjsip) BUILD_PJSIP=1; SELECTED_BY_FLAG=1 ;;
    --clean) CLEAN=1; SELECTED_BY_FLAG=1 ;;
    --deps) SHOW_DEPS=1; SELECTED_BY_FLAG=1 ;;
    --audio-diagnose) AUDIO_DIAG_ONLY=1; SELECTED_BY_FLAG=1 ;;
    --configure-capture) CONFIGURE_CAPTURE_ONLY=1; SELECTED_BY_FLAG=1 ;;
    --no-audio-fix) AUTO_AUDIO_FIX=0 ;;
    --uninstall|--remove) UNINSTALL=1; SELECTED_BY_FLAG=1 ;;
    --purge-user-data) PURGE_USER_DATA=1 ;;
    --dry-run) DRY_RUN=1 ;;
    --auto-deps) AUTO_DEPS=1 ;;
    --no-auto-deps) AUTO_DEPS=0 ;;
    --install) INSTALL_MODE=yes ;;
    --no-install) INSTALL_MODE=no ;;
    --prefix)
      shift
      [ "$#" -gt 0 ] || { echo "--prefix requires a path" >&2; exit 2; }
      INSTALL_PREFIX=$1
      ;;
    --pjsip-prefix)
      shift
      [ "$#" -gt 0 ] || { echo "--pjsip-prefix requires a path" >&2; exit 2; }
      PJSIP_PREFIX=$1
      ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

if [ "$SELECTED_BY_FLAG" -eq 1 ]; then
  logo
  root_warning
  show_host_requirements
else
  interactive_select
fi

if [ "$SHOW_DEPS" -eq 1 ]; then
  show_host_requirements
fi

if [ "$AUDIO_DIAG_ONLY" -eq 1 ]; then
  run_audio_preflight
  if [ -f "$PJSIP_PREFIX/lib/pkgconfig/libpjproject.pc" ] || [ -f "$PJSIP_PREFIX/libdata/pkgconfig/libpjproject.pc" ]; then
    echo
    echo "============================================================"
    echo " PJSIP AUDIO DEVICE PROBE"
    echo "============================================================"
    if ! validate_pjsip_static_link; then
      audio_warn "Managed PJSIP is present, but its runtime audio-device probe could not complete."
    fi
  else
    audio_info "Managed PJSIP is not installed yet; skipping PJSIP-level audio device enumeration."
  fi
fi

if [ "$CONFIGURE_CAPTURE_ONLY" -eq 1 ]; then
  ensure_capture_dependencies
  configure_capture_permissions
fi

if [ "$UNINSTALL" -eq 1 ]; then
  uninstall_previous
fi

# A dependency-only display and a clean-only operation must never install packages.
if [ "$BUILD_CLI" -eq 1 ] || [ "$BUILD_GUI" -eq 1 ] || [ "$BUILD_PJSIP" -eq 1 ]; then
  ensure_system_dependencies
  if [ "$BUILD_CLI" -eq 1 ] || [ "$BUILD_GUI" -eq 1 ]; then
    run_audio_preflight
    configure_audio_fixes
    configure_capture_permissions
  fi
fi

if [ "$CLEAN" -eq 1 ]; then
  clean_builds
fi

if [ "$BUILD_CLI" -eq 1 ] || [ "$BUILD_GUI" -eq 1 ]; then
  ensure_pjsip
elif [ "$BUILD_PJSIP" -eq 1 ]; then
  build_pjsip
fi

if [ "$BUILD_CLI" -eq 1 ] || [ "$BUILD_GUI" -eq 1 ]; then
  ask_install
  if [ "$INSTALL_MODE" = yes ]; then
    echo "System installation selected; root privileges will be used only for the final install (dependency installation may already have requested root)."
  else
    echo "Selected client(s) will be built but NOT installed system-wide."
  fi

  build_selected
  install_selected
  if [ "$INSTALL_MODE" = yes ]; then
    seed_user_profile
  fi
fi

if [ "$DRY_RUN" -eq 1 ]; then
  echo
  echo "S.I.P.H.E.R. 1.0.0 dry run complete."
else
  echo
  if [ "$BUILD_CLI" -eq 1 ] || [ "$BUILD_GUI" -eq 1 ]; then
    echo "S.I.P.H.E.R. 1.0.0 build complete."
    [ "$INSTALL_MODE" = yes ] && echo "Selected client(s) installed under $INSTALL_PREFIX/bin."
  elif [ "$BUILD_PJSIP" -eq 1 ]; then
    echo "S.I.P.H.E.R. PJSIP dependency build complete."
  elif [ "$CONFIGURE_CAPTURE_ONLY" -eq 1 ]; then
    echo "S.I.P.H.E.R. 1.0.0 packet-capture permission setup complete."
  elif [ "$AUDIO_DIAG_ONLY" -eq 1 ]; then
    echo "S.I.P.H.E.R. 1.0.0 audio diagnostic complete."
  elif [ "$UNINSTALL" -eq 1 ]; then
    echo "S.I.P.H.E.R. installed files removed."
  elif [ "$CLEAN" -eq 1 ]; then
    echo "S.I.P.H.E.R. build directories cleaned."
  fi
fi
