#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${1:-${SAK_WINDOWS_TARGET:-win10}}"
shift || true
INSTALL_DEPS=1
RUN_TESTS=1
CLEAN=0
for arg in "$@"; do
  case "$arg" in
    --no-install-deps) INSTALL_DEPS=0 ;;
    --no-tests) RUN_TESTS=0 ;;
    --clean) CLEAN=1 ;;
    *) echo "Unknown option: $arg" >&2; exit 2 ;;
  esac
done

case "$TARGET" in
  win7)
    QT_MAJOR=5
    LABEL="Windows 7 SP1 x64"
    DIST_NAME="SIPHER-1.0.0-Windows7-Portable-x64"
    ;;
  win10|win11|modern|win10-11)
    TARGET=win10
    QT_MAJOR=6
    LABEL="Windows 10/11 x64"
    DIST_NAME="SIPHER-1.0.0-r8-Windows10-11-Portable-x64"
    ;;
  *) echo "Target must be win7 or win10" >&2; exit 2 ;;
esac

if [[ "${MSYSTEM:-}" != MINGW64 ]]; then
  cat >&2 <<EOF
ERROR: Run this builder from the MSYS2 'MinGW 64-bit' shell.

Or from Command Prompt use:
  build-windows-portable.cmd $TARGET

The builder uses the MINGW64 toolchain for both portable targets.
EOF
  exit 2
fi

PREFIX="${MINGW_PREFIX:-/mingw64}"
ARCH_PREFIX="mingw-w64-x86_64"
BUILD_ROOT="$ROOT/build/windows/$TARGET"
PJSIP_PREFIX="$BUILD_ROOT/pjsip"
APP_BUILD="$BUILD_ROOT/app"
DIST="$ROOT/dist/$DIST_NAME"
PJSIP_SRC="$ROOT/third_party/pjproject"

say(){ printf '\n==> %s\n' "$*"; }
need(){ command -v "$1" >/dev/null 2>&1 || { echo "Missing required command: $1" >&2; exit 1; }; }

if (( INSTALL_DEPS )); then
  say "Checking/installing MSYS2 build dependencies for $LABEL"
  QT_PACKAGE="$ARCH_PREFIX-qt${QT_MAJOR}-base"
  pacman -S --needed --noconfirm \
    git make sed gawk grep coreutils diffutils patch \
    "$ARCH_PREFIX-toolchain" "$ARCH_PREFIX-cmake" "$ARCH_PREFIX-ninja" \
    "$ARCH_PREFIX-pkgconf" "$QT_PACKAGE" \
    "$ARCH_PREFIX-curl" "$ARCH_PREFIX-openssl" "$ARCH_PREFIX-ffmpeg" \
    "$ARCH_PREFIX-ca-certificates"
fi

for c in git make gcc g++ cmake ninja pkg-config; do need "$c"; done
if (( CLEAN )); then rm -rf "$BUILD_ROOT" "$DIST"; fi
mkdir -p "$BUILD_ROOT" "$ROOT/dist"

say "Building S.I.P.H.E.R. managed PJSIP 2.17 for $LABEL"
export CC=gcc CXX=g++
export PJSIP_PREFIX PJSIP_SOURCE_DIR="$PJSIP_SRC"
"$ROOT/scripts/bootstrap-pjsip.sh"
export PKG_CONFIG_PATH="$PJSIP_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
if [[ "$(pkg-config --modversion libpjproject)" != 2.17 ]]; then
  echo "PJSIP 2.17 pkg-config metadata was not found after bootstrap." >&2
  exit 1
fi

say "Configuring S.I.P.H.E.R. GUI + CLI ($LABEL / Qt $QT_MAJOR)"
rm -rf "$APP_BUILD"
cmake -S "$ROOT" -B "$APP_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSAK_QT_MAJOR="$QT_MAJOR" \
  -DTRUNKMONKEY_BUILD_GUI=ON \
  -DTRUNKMONKEY_BUILD_CLI=ON \
  -DTRUNKMONKEY_BUILD_TESTS=ON
cmake --build "$APP_BUILD" --parallel

if (( RUN_TESTS )); then
  say "Running S.I.P.H.E.R. regression tests"
  ctest --test-dir "$APP_BUILD" --output-on-failure --timeout 60
fi

say "Staging portable $LABEL package"
rm -rf "$DIST"
mkdir -p "$DIST"/{data/config,data/state/logs,data/state/cache,data/tmp,tools,docs,examples}
cp "$APP_BUILD/sipher.exe" "$DIST/"
cp "$APP_BUILD/sipher-gui.exe" "$DIST/"
cp "$ROOT/examples/"* "$DIST/examples/"
cp "$ROOT/README.md" "$ROOT/OPERATOR_GUIDE.md" "$ROOT/SECURITY_AUDIT.md" "$ROOT/windows/README-WINDOWS.md" "$ROOT/windows/FEATURE-PARITY.md" "$DIST/docs/"

# Seed profile only as an example. S.I.P.H.E.R. creates data/config/profile.conf on first run.
cp "$ROOT/examples/profile.conf.example" "$DIST/data/config/profile.conf.example"

# Deploy Qt runtime/plugins.
WINDEPLOY="$(command -v windeployqt${QT_MAJOR} 2>/dev/null || command -v windeployqt 2>/dev/null || true)"
if [[ -z "$WINDEPLOY" ]]; then
  echo "windeployqt not found for Qt $QT_MAJOR" >&2; exit 1
fi
"$WINDEPLOY" --release --no-translations "$DIST/sipher-gui.exe"

# Copy the transitive MinGW DLL closure for a PE executable. Using objdump
# avoids depending on a particular MSYS2 ldd implementation.
declare -A COPIED_DLLS=()
copy_mingw_dll_closure(){
  local file="$1" destination="$2"
  local dll src key
  while read -r dll; do
    [[ -n "$dll" ]] || continue
    key="${dll,,}"
    [[ -n "${COPIED_DLLS[$key]:-}" ]] && continue
    src="$PREFIX/bin/$dll"
    [[ -f "$src" ]] || continue
    COPIED_DLLS[$key]=1
    cp -f "$src" "$destination/$dll"
    copy_mingw_dll_closure "$src" "$destination"
  done < <(objdump -p "$file" 2>/dev/null | sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p')
}

copy_pe_with_deps(){
  local source="$1" destination="$2"
  [[ -f "$source" ]] || return 1
  mkdir -p "$destination"
  cp -f "$source" "$destination/"
  COPIED_DLLS=()
  copy_mingw_dll_closure "$source" "$destination"
}

# Main MinGW runtime dependencies not handled by windeployqt.
COPIED_DLLS=()
copy_mingw_dll_closure "$DIST/sipher.exe" "$DIST"
copy_mingw_dll_closure "$DIST/sipher-gui.exe" "$DIST"

# Portable helper tools used by queue-audio normalization and vulnerability/TLS lookups.
for tool in ffmpeg curl openssl; do
  exe="$(command -v "$tool" || true)"
  [[ -n "$exe" ]] || { echo "Required portable helper missing: $tool" >&2; exit 1; }
  copy_pe_with_deps "$exe" "$DIST/tools"
done

# curl needs a CA bundle. Point the launchers at the staged copy when available.
CA_SOURCE=""
for candidate in "$PREFIX/etc/ssl/certs/ca-bundle.crt" "$PREFIX/ssl/certs/ca-bundle.crt" "/etc/ssl/certs/ca-bundle.crt"; do
  if [[ -f "$candidate" ]]; then CA_SOURCE="$candidate"; break; fi
done
[[ -z "$CA_SOURCE" ]] || cp -f "$CA_SOURCE" "$DIST/tools/cacert.pem"

# Win7 has no pktmon. If a Wireshark dumpcap installation is visible on the build host,
# bundle dumpcap and its DLL dependencies. The Win7 target machine still needs a compatible
# packet-capture driver installed; no user-space portable app can replace the kernel driver.
if [[ "$TARGET" == win7 ]]; then
  DUMPCAP=""
  for candidate in \
    "/c/Program Files/Wireshark/dumpcap.exe" \
    "/c/Program Files (x86)/Wireshark/dumpcap.exe"; do
    [[ -f "$candidate" ]] && { DUMPCAP="$candidate"; break; }
  done
  if [[ -n "$DUMPCAP" ]]; then
    cp -f "$DUMPCAP" "$DIST/tools/dumpcap.exe"
    # Wireshark's adjacent support DLLs are not predictable across releases; copy the DLLs
    # in that directory so a portable dumpcap has the same userspace runtime it was built with.
    find "$(dirname "$DUMPCAP")" -maxdepth 1 -type f -iname '*.dll' -exec cp -n {} "$DIST/tools/" \;
  fi
fi

cat > "$DIST/SIPHER-GUI.cmd" <<'CMD'
@echo off
setlocal
set "SIPHER_PORTABLE_ROOT=%~dp0"
set "PATH=%~dp0tools;%~dp0;%PATH%"
if exist "%~dp0tools\cacert.pem" set "CURL_CA_BUNDLE=%~dp0tools\cacert.pem"
start "S.I.P.H.E.R. By GITSC" "%~dp0sipher-gui.exe" %*
CMD
cat > "$DIST/SIPHER-CLI.cmd" <<'CMD'
@echo off
setlocal
set "SIPHER_PORTABLE_ROOT=%~dp0"
set "PATH=%~dp0tools;%~dp0;%PATH%"
if exist "%~dp0tools\cacert.pem" set "CURL_CA_BUNDLE=%~dp0tools\cacert.pem"
"%~dp0sipher.exe" %*
CMD

if [[ "$TARGET" == win7 ]]; then
cat > "$DIST/WINDOWS-7-NOTE.txt" <<'TXT'
S.I.P.H.E.R. WINDOWS 7 SP1 x64 PORTABLE EDITION

This build uses the Qt 5 compatibility frontend and targets the Windows 7 API level.
The GUI and CLI share the same S.I.P.H.E.R. SIP/PJSIP core and feature set as the modern build.

Packet capture note:
Windows 7 does not include pktmon. SIP/RTP PCAP capture therefore requires a compatible
Npcap/WinPcap-class capture driver on the machine. If dumpcap.exe was available on the
build host it is staged under tools; the kernel capture driver itself must be installed
on the Win7 host. Calling, RTP media, PBX audit/fingerprinting, CVE lookup, TLS audit,
queue testing and the rest of S.I.P.H.E.R. do not require that capture driver.

Native Win7 cmd.exe does not support modern ANSI VT sequences. S.I.P.H.E.R. automatically
uses a clean monochrome/native-console fallback there; commands and dashboard pages
remain available.
TXT
else
cat > "$DIST/WINDOWS-10-11-NOTE.txt" <<'TXT'
S.I.P.H.E.R. WINDOWS 10/11 x64 PORTABLE EDITION

This build uses Qt 6 and the same S.I.P.H.E.R. SIP/PJSIP core as Linux/FreeBSD and the Win7 edition.
The CLI enables Windows VT rendering when supported. Packet capture prefers dumpcap when
available and otherwise can use Windows pktmon as the built-in fallback.
TXT
fi

# Generate a manifest and checksums before zipping.
(
  cd "$DIST"
  find . -type f -print | sort > PACKAGE-FILES.txt
  if command -v sha256sum >/dev/null 2>&1; then
    find . -type f ! -name SHA256SUMS.txt -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS.txt
  fi
)

say "Portable runtime staged"
echo "  $DIST"
echo "  GUI: $DIST/SIPHER-GUI.cmd"
echo "  CLI: $DIST/SIPHER-CLI.cmd"
echo
echo "ZIP it with Windows Explorer/7-Zip, or from MSYS2:"
echo "  cd \"$ROOT/dist\" && 7z a -tzip \"$DIST_NAME.zip\" \"$DIST_NAME\""
