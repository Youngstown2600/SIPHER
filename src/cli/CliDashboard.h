#pragma once

#include "trunkmonkey/CallSnapshot.h"
#include "trunkmonkey/Profile.h"
#include "trunkmonkey/SipTrace.h"
#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace trunkmonkey::cli {

enum class DashboardPage { Main=1, SipLog=2, Media=3, Calls=4, SecurityAudit=5, Profile=6, Help=7, EngineLog=8, QueueActivity=9 };

struct DashboardNotice {
    enum class Level { Info, Success, Warning, Error };
    std::string text;
    Level level{Level::Info};
};

struct DashboardState {
    SipProfile profile;
    std::string profilePath;
    std::string dialPrefix;
    std::string registrationText;
    bool registered{false};
    unsigned maxCalls{50};
    std::vector<CallSnapshot> calls;
    int focusCallId{-1};
    std::vector<SipTraceEntry> focusTrace;
    std::string captureStatus;
    std::vector<DashboardNotice> notices;
    std::vector<std::string> engineLogLines;
    std::string engineLogPath;
    std::size_t engineLogOffset{0};
    std::string auditSummary;
    DashboardPage page{DashboardPage::Main};
};

class CliDashboard {
public:
    CliDashboard();
    bool enabled() const noexcept { return enabled_; }
    void render(const DashboardState& state, std::ostream& out) const;
    void showOverlay(const std::string& title, const std::string& body, std::ostream& out) const;
    void pauseForEnter(std::istream& in, std::ostream& out) const;
    void prepareInteractivePrompt(std::ostream& out) const;
    void clear(std::ostream& out) const;
    bool setTheme(const std::string& name);
    const std::string& themeName() const noexcept { return themeName_; }
    static std::vector<std::string> themeNames();
    static const char* pageName(DashboardPage page);
private:
    struct TerminalSize { int columns{100}; int rows{40}; };
    TerminalSize terminalSize() const;
    std::string paint(const std::string& text, const char* ansi) const;
    std::string panel(const std::string& title, const std::vector<std::string>& lines, int width) const;
    std::vector<std::string> panelLines(const std::string& title,const std::vector<std::string>& lines,int width) const;
    std::vector<std::string> headerLines(const DashboardState& state,int width,bool compact) const;
    std::vector<std::string> accountLines(const DashboardState& state,int width) const;
    std::vector<std::string> registrationLines(const DashboardState& state,int width) const;
    std::vector<std::string> callLines(const DashboardState& state,int width,bool compact,int maxEntries=0) const;
    std::vector<std::string> diagnosticLines(const DashboardState& state,int width,int maxTraceRows) const;
    std::vector<std::string> sipPageLines(const DashboardState& state,int width,int maxRows) const;
    std::vector<std::string> engineLogPageLines(const DashboardState& state,int width,int maxRows) const;
    std::vector<std::string> profileLines(const DashboardState& state,int width) const;
    std::vector<std::string> securityLines(const DashboardState& state,int width) const;
    std::vector<std::string> activityLines(const DashboardState& state,int width,int maxRows) const;
    std::vector<std::string> quickCommandLines(int width,bool compact) const;
    std::vector<std::string> pageBarLines(DashboardPage page,int width) const;
    static std::string fit(const std::string& value,std::size_t width);
    static std::string duration(std::uint64_t startMs,std::uint64_t endMs=0);
    static std::size_t visibleLength(const std::string& value);
    static std::string padVisible(const std::string& value,std::size_t width);
    static std::vector<std::string> splitLines(const std::string& text);
    static std::vector<std::string> mergeColumns(const std::vector<std::string>& left,const std::vector<std::string>& right,int leftWidth,int gap);
    struct Palette {
        std::string brand;
        std::string accent;
        std::string success;
        std::string warning;
        std::string error;
        std::string text;
        std::string dim;
    };
    bool enabled_{false};
    bool color_{false};
    bool vtEnabled_{false};
    bool consoleTty_{false};
    std::string themeName_{"classic"};
    Palette palette_;
};

} // namespace trunkmonkey::cli
