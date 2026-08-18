#include "trunkmonkey/RuntimePaths.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace {
void check(bool condition, const char* expression)
{
    if (!condition) throw std::runtime_error(std::string("CHECK failed: ") + expression);
}
}

int main()
{
    try {
        namespace fs = std::filesystem;
        using namespace trunkmonkey;
        const auto base = fs::temp_directory_path() / "trunkmonkey-runtime-paths-test";
        std::error_code ec;
        fs::remove_all(base, ec);
        fs::create_directories(base / "config-root", ec);
        check(!ec, "create config test root");
        fs::create_directories(base / "state-root", ec);
        check(!ec, "create state test root");
#if defined(_WIN32)
        _putenv_s("XDG_CONFIG_HOME", (base / "config-root").string().c_str());
        _putenv_s("XDG_STATE_HOME", (base / "state-root").string().c_str());
#else
        check(setenv("XDG_CONFIG_HOME", (base / "config-root").string().c_str(), 1) == 0,
              "set XDG_CONFIG_HOME");
        check(setenv("XDG_STATE_HOME", (base / "state-root").string().c_str(), 1) == 0,
              "set XDG_STATE_HOME");
#endif
        check(runtime::configDir() == base / "config-root" / "trunkmonkey", "config directory path");
        check(runtime::stateDir() == base / "state-root" / "trunkmonkey", "state directory path");
        runtime::ensureUserDirectories();
        check(fs::is_directory(runtime::configDir()), "config directory is created");
        check(fs::is_directory(runtime::stateDir() / "logs"), "log directory is created");
#ifndef _WIN32
        struct stat configStat{};
        check(::stat(runtime::configDir().c_str(), &configStat) == 0, "config directory stat");
        check((configStat.st_mode & 0777) == 0700, "config directory permissions are 0700");
#endif
        check(runtime::defaultProfilePath() == runtime::configDir() / "profile.conf", "default profile path");
        fs::remove_all(base, ec);
        std::cout << "runtime path tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "runtime path test failed: " << e.what() << '\n';
        return 1;
    }
}
