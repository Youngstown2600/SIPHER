#include "trunkmonkey/RuntimePaths.h"
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <system_error>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace trunkmonkey::runtime {
namespace {
std::filesystem::path envPath(const char* name)
{
    const char* value = std::getenv(name);
    return (value && *value) ? std::filesystem::path(value) : std::filesystem::path{};
}

std::filesystem::path absoluteEnvPath(const char* name)
{
    auto value = envPath(name);
    return (!value.empty() && value.is_absolute()) ? value : std::filesystem::path{};
}


#ifdef _WIN32
std::filesystem::path portableRoot()
{
    auto explicitRoot = absoluteEnvPath("SIPHER_PORTABLE_ROOT");
    if (explicitRoot.empty()) explicitRoot = absoluteEnvPath("SAK_PORTABLE_ROOT"); // legacy S.a.K. compatibility
    if (!explicitRoot.empty()) return explicitRoot;
#ifdef SAK_PORTABLE_BUILD
    std::wstring buffer(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (n > 0 && n < buffer.size()) {
        buffer.resize(n);
        return std::filesystem::path(buffer).parent_path();
    }
#endif
    return {};
}
#endif

std::filesystem::path homeDir()
{
    auto home = envPath("HOME");
    if (!home.empty()) {
        return home;
    }
#ifndef _WIN32
    if (const auto* pw = ::getpwuid(::getuid()); pw && pw->pw_dir && *pw->pw_dir) {
        return std::filesystem::path(pw->pw_dir);
    }
#endif
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path{"."} : cwd;
}

void makePrivateDir(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec && !std::filesystem::is_directory(path)) {
        throw std::runtime_error("Unable to create S.I.P.H.E.R. data directory: " + path.string() + ": " + ec.message());
    }
#ifndef _WIN32
    // Profiles and diagnostics may contain credentials, identities, SIP
    // messages, and endpoint metadata. Keep S.I.P.H.E.R.-owned directories
    // private regardless of a permissive process umask.
    (void)::chmod(path.c_str(), S_IRWXU);
#endif
}
}

std::filesystem::path configDir()
{
#ifdef _WIN32
    auto portable = portableRoot();
    if (!portable.empty()) return portable / "data" / "config";
#endif
    auto xdg = absoluteEnvPath("XDG_CONFIG_HOME");
    if (!xdg.empty()) {
        return xdg / "trunkmonkey";
    }
    return homeDir() / ".config" / "trunkmonkey";
}

std::filesystem::path stateDir()
{
#ifdef _WIN32
    auto portable = portableRoot();
    if (!portable.empty()) return portable / "data" / "state";
#endif
    auto xdg = absoluteEnvPath("XDG_STATE_HOME");
    if (!xdg.empty()) {
        return xdg / "trunkmonkey";
    }
    return homeDir() / ".local" / "state" / "trunkmonkey";
}

std::filesystem::path settingsPath()
{
    return configDir() / "trunkmonkey.ini";
}

std::filesystem::path logPath()
{
    return stateDir() / "logs" / "trunkmonkey.log";
}

std::filesystem::path tempDir()
{
#ifndef _WIN32
    return std::filesystem::path("/tmp") / ("trunkmonkey-" + std::to_string(static_cast<unsigned long>(::getuid())));
#else
    auto portable = portableRoot();
    if (!portable.empty()) return portable / "data" / "tmp";
    std::error_code ec;
    auto base = std::filesystem::temp_directory_path(ec);
    if (ec) base = std::filesystem::path{"."};
    return base / "trunkmonkey";
#endif
}

std::filesystem::path pjsipLogPath()
{
    return tempDir() / "pjsip-engine.log";
}

std::filesystem::path defaultProfilePath(const std::filesystem::path& executablePath)
{
    auto explicitProfile = envPath("SAK_PROFILE");
    if (explicitProfile.empty()) explicitProfile = envPath("SIPHER_PROFILE");
    if (explicitProfile.empty()) explicitProfile = envPath("TRUNKMONKEY_PROFILE");
    if (!explicitProfile.empty()) {
        return explicitProfile;
    }

    const auto userProfile = configDir() / "profile.conf";
    if (std::filesystem::exists(userProfile)) {
        return userProfile;
    }

    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec) {
        const auto projectProfile = cwd / "config" / "profile.conf";
        if (std::filesystem::exists(projectProfile)) {
            return projectProfile;
        }
    }

    if (!executablePath.empty()) {
        auto exe = executablePath;
        if (!exe.is_absolute()) {
            exe = std::filesystem::absolute(exe, ec);
        }
        if (!ec && exe.has_parent_path()) {
            const auto adjacentProfile = exe.parent_path() / "config" / "profile.conf";
            if (std::filesystem::exists(adjacentProfile)) {
                return adjacentProfile;
            }
        }
    }

    return userProfile;
}

void configurePortableEnvironment()
{
#ifdef _WIN32
    const auto root = portableRoot();
    if (root.empty()) return;

    // Make helper discovery work even when the user double-clicks sipher-gui.exe
    // directly instead of using SIPHER-GUI.cmd.
    if (envPath("SIPHER_PORTABLE_ROOT").empty()) {
        (void)SetEnvironmentVariableW(L"SIPHER_PORTABLE_ROOT", root.wstring().c_str());
    }

    const auto tools = root / "tools";
    std::wstring currentPath;
    const DWORD needed = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (needed > 0) {
        currentPath.resize(needed);
        const DWORD got = GetEnvironmentVariableW(L"PATH", currentPath.data(), needed);
        if (got > 0 && got < currentPath.size()) currentPath.resize(got);
        else currentPath.clear();
    }
    std::wstring combined = tools.wstring() + L";" + root.wstring();
    if (!currentPath.empty()) combined += L";" + currentPath;
    (void)SetEnvironmentVariableW(L"PATH", combined.c_str());

    if (GetEnvironmentVariableW(L"CURL_CA_BUNDLE", nullptr, 0) == 0) {
        const auto ca = tools / "cacert.pem";
        std::error_code ec;
        if (std::filesystem::is_regular_file(ca, ec) && !ec) {
            (void)SetEnvironmentVariableW(L"CURL_CA_BUNDLE", ca.wstring().c_str());
        }
    }
#endif
}

void ensureUserDirectories()
{
    makePrivateDir(configDir());
    makePrivateDir(stateDir());
    makePrivateDir(stateDir() / "logs");
    makePrivateDir(tempDir());
}
} // namespace trunkmonkey::runtime
