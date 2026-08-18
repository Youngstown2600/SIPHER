#include "trunkmonkey/RuntimePaths.h"
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <system_error>
#ifndef _WIN32
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
    auto xdg = absoluteEnvPath("XDG_CONFIG_HOME");
    if (!xdg.empty()) {
        return xdg / "trunkmonkey";
    }
    return homeDir() / ".config" / "trunkmonkey";
}

std::filesystem::path stateDir()
{
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
    auto explicitProfile = envPath("SIPHER_PROFILE");
    if (explicitProfile.empty()) explicitProfile = envPath("SIPCLIENT_PROFILE");
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

void ensureUserDirectories()
{
    makePrivateDir(configDir());
    makePrivateDir(stateDir());
    makePrivateDir(stateDir() / "logs");
    makePrivateDir(tempDir());
}
} // namespace trunkmonkey::runtime
