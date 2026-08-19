#include "trunkmonkey/Profile.h"
#include "trunkmonkey/TextPool.h"
#include <filesystem>
#include <fstream>
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

struct TempDir {
    std::filesystem::path path;
    TempDir()
    {
        path = std::filesystem::temp_directory_path() / "trunkmonkey-profile-test";
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        if (!std::filesystem::create_directories(path, ec) && ec)
            throw std::runtime_error("Unable to create test directory: " + path.string());
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
}

int main()
{
    try {
        using namespace trunkmonkey;
        namespace fs = std::filesystem;
        TempDir temp;
        const auto profilePath = temp.path / "profile-test.conf";
        const auto firstRunPath = temp.path / "profile-first-run.conf";
        const auto normalizePath = temp.path / "profile-normalize.conf";
        const auto invalidPath = temp.path / "profile-invalid.conf";
        const auto poolPath = temp.path / "pool-test.txt";

        SipProfile profile;
        profile.name = "Test";
        profile.sipDomain = "pbx.example.net";
        profile.registrar = "sip:pbx.example.net";
        profile.username = "1001";
        profile.authUsername = "auth1001";
        profile.password = "secret";
        profile.dialPrefix = "4071";
        profile.transport = Transport::Tcp;
        profile.identityMode = IdentityMode::FromAndPai;
        ProfileStore::save(profile, profilePath.string());
#ifndef _WIN32
        struct stat profileStat{};
        check(::stat(profilePath.c_str(), &profileStat) == 0, "profile file exists");
        check((profileStat.st_mode & 0777) == 0600, "profile permissions are 0600");
#endif

        const auto blank = ProfileStore::defaults();
        check(!ProfileStore::isConfigured(blank), "blank profile is not configured");

        const bool created = ProfileStore::createDefaultIfMissing(firstRunPath.string());
        check(created, "first-run profile is created");
        check(fs::exists(firstRunPath), "first-run profile exists after creation");
        const bool createdAgain = ProfileStore::createDefaultIfMissing(firstRunPath.string());
        check(!createdAgain, "existing first-run profile is preserved");

        const auto firstRun = ProfileStore::loadDraft(firstRunPath.string());
        check(firstRun.name == "Default", "first-run profile has default name");
        check(firstRun.sipDomain.empty(), "first-run SIP domain is empty");
        check(firstRun.username.empty(), "first-run username is empty");
        check(!ProfileStore::isConfigured(firstRun), "first-run profile is not configured");

        SipProfile placeholder;
        placeholder.sipDomain = "pbx.example.net";
        placeholder.username = "1001";
        placeholder.password = "CHANGE_ME";
        check(!ProfileStore::isConfigured(placeholder), "CHANGE_ME profile is not configured");

        const auto loaded = ProfileStore::load(profilePath.string());
        check(loaded.name == profile.name, "profile name round-trips");
        check(loaded.sipDomain == profile.sipDomain, "SIP domain round-trips");
        check(loaded.authUsername == profile.authUsername, "auth username round-trips");
        check(loaded.dialPrefix == "4071", "dial prefix round-trips");
        check(loaded.transport == Transport::Tcp, "transport round-trips");
        check(loaded.identityMode == IdentityMode::FromAndPai, "identity mode round-trips");

        {
            std::ofstream out(normalizePath);
            out << "sip_domain=pbx.example.net\nusername=1001\nregistrar=pbx.example.net\noutbound_proxy=proxy.example.net\n";
        }
        const auto normalized = ProfileStore::load(normalizePath.string());
        check(normalized.registrar == "sip:pbx.example.net", "registrar is normalized to SIP URI");
        check(normalized.outboundProxy == "sip:proxy.example.net", "outbound proxy is normalized to SIP URI");

        {
            std::ofstream out(invalidPath);
            out << "sip_domain=pbx.example.net\nusername=1001\nlocal_sip_port=70000\n";
        }
        bool rejected = false;
        try { (void)ProfileStore::load(invalidPath.string()); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "out-of-range SIP port is rejected");

        SipProfile invalidPrefix = profile;
        invalidPrefix.dialPrefix = "4071@bad";
        rejected = false;
        try { ProfileStore::validate(invalidPrefix); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "invalid dial prefix is rejected");

        {
            std::ofstream out(poolPath);
            out << "# x\n111\n222\n333\n";
        }
        TextPool pool;
        pool.load(poolPath.string());
        check(pool.size() == 3, "text pool loads three entries");
        check(pool.next() == "111", "text pool first entry");
        check(pool.next() == "222", "text pool second entry");
        check(pool.next() == "333", "text pool third entry");
        check(pool.next() == "111", "text pool wraps");

        std::cout << "profile/text-pool tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "profile/text-pool test failed: " << e.what() << '\n';
        return 1;
    }
}
