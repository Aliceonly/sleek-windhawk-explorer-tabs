#define SLEEKWINTABS_CORE_TEST
#include "../sleekwintabs.wh.cpp"

#include <cstdlib>
#include <iostream>
#include <set>

namespace {
int checks = 0;
#define CHECK(expression) do { ++checks; if (!(expression)) { \
    std::cerr << __FILE__ << ':' << __LINE__ << ": " #expression "\n"; std::exit(1); } } while (0)

void Set(Core::Config& config, std::wstring_view key, std::wstring_view value) {
    std::wstring error;
    CHECK(config.Set(key, value, error));
}
void SettingsContract() {
    Core::Config defaults;
    std::wstring error;
    CHECK(defaults.Validate(error));
    CHECK(error.empty());
    CHECK(Core::kSettingCount == 45);
    std::set<std::wstring_view> keys;
    unsigned exposed = 0;
    for (const auto& spec : Core::kSettings) {
        CHECK(keys.insert(spec.key).second);
        exposed += spec.exposed;
        Core::Config config;
        CHECK(config.Set(spec.key, spec.initial, error));
        CHECK(config.Get(spec.key) == spec.initial);
        switch (spec.kind) {
            case Core::SettingKind::Boolean:
                CHECK(config.Set(spec.key, L"false", error));
                CHECK(!config.Boolean(spec.key));
                CHECK(config.Set(spec.key, L"true", error));
                CHECK(config.Boolean(spec.key));
                CHECK(!config.Set(spec.key, L"1", error));
                CHECK(!config.Set(spec.key, L"", error));
                break;
            case Core::SettingKind::Integer:
                CHECK(config.Set(spec.key, std::to_wstring(spec.minimum), error));
                CHECK(config.Integer(spec.key) == spec.minimum);
                CHECK(config.Set(spec.key, std::to_wstring(spec.maximum), error));
                CHECK(config.Integer(spec.key) == spec.maximum);
                CHECK(!config.Set(spec.key, std::to_wstring(static_cast<long long>(spec.maximum) + 1), error));
                CHECK(!config.Set(spec.key, std::to_wstring(spec.minimum - 1), error));
                CHECK(!config.Set(spec.key, L"20ms", error));
                break;
            case Core::SettingKind::Choice: {
                auto choices = spec.choices;
                while (!choices.empty()) {
                    auto delimiter = choices.find(L'|');
                    auto choice = choices.substr(0, delimiter);
                    CHECK(config.Set(spec.key, choice, error));
                    CHECK(config.Get(spec.key) == choice);
                    if (delimiter == std::wstring_view::npos) break;
                    choices.remove_prefix(delimiter + 1);
                }
                CHECK(!config.Set(spec.key, L"", error));
                CHECK(!config.Set(spec.key, L"__unknown__", error));
                break;
            }
            case Core::SettingKind::Shortcut:
                CHECK(config.Set(spec.key, L"", error));
                CHECK(config.Get(spec.key).empty());
                CHECK(config.Set(spec.key, L"Alt+Shift+F24", error));
                CHECK(!config.Set(spec.key, L"Ctrl+Ctrl+Q", error));
                break;
        }
    }
    CHECK(exposed == 8);
    auto original = defaults.values;
    CHECK(!defaults.Set(L"not.a.key", L"true", error));
    CHECK(defaults.values == original);
    CHECK(defaults.Get(L"duplicate.shortcut") == L"Ctrl+Q");
    CHECK(defaults.Get(L"duplicate.shortcut") != L"Ctrl+D");
    Set(defaults, L"fixTabTitles", L"false");
    Set(defaults, L"selectNewTabs", L"false");
    Set(defaults, L"advanced.extraSelectDelayMs", L"0");
    CHECK(!defaults.Boolean(L"fixTabTitles"));
    CHECK(!defaults.Boolean(L"selectNewTabs"));
    CHECK(defaults.Integer(L"advanced.extraSelectDelayMs") == 0);
    CHECK(!Core::ParseInteger(L"2147483648"));
    CHECK(!Core::ParseInteger(L"+1"));
    CHECK(!Core::ParseInteger(L" 1"));
    CHECK(!Core::ParseInteger(L""));
}
void ValidationAndShortcuts() {
    std::wstring error;
    Core::Config config;
    Set(config, L"advanced.extraSelectDelayMs", L"1000");
    Set(config, L"advanced.selectionDeadlineMs", L"1000");
    CHECK(!config.Validate(error));
    Set(config, L"advanced.selectionDeadlineMs", L"1001");
    CHECK(config.Validate(error));
    Set(config, L"batchOpen.maxFolders", L"1");
    CHECK(!config.Validate(error));
    Set(config, L"batchOpen.maxFolders", L"0");
    CHECK(config.Validate(error));
    Set(config, L"batchOpen.maxFolders", L"2");
    CHECK(config.Validate(error));
    Set(config, L"batchOpen.shortcut", L"q");
    Set(config, L"duplicate.shortcut", L"Q");
    CHECK(!config.Validate(error));
    Set(config, L"duplicate.enabled", L"false");
    CHECK(config.Validate(error));
    Set(config, L"batchOpen.cancelShortcut", L"Q");
    CHECK(!config.Validate(error));
    Set(config, L"batchOpen.cancelShortcut", L"");
    CHECK(config.Validate(error));
    CHECK(*Core::ParseShortcut(L"ctrl+SHIFT+q") == *Core::ParseShortcut(L"Shift+Ctrl+Q"));
    CHECK(Core::ParseShortcut(L"Ctrl+Q")->key == 'Q');
    CHECK(Core::ParseShortcut(L"Ctrl+Q")->modifiers == 1);
    CHECK(Core::ParseShortcut(L"Enter")->key == 0x0D);
    CHECK(Core::ParseShortcut(L"Escape")->key == 0x1B);
    CHECK(Core::ParseShortcut(L"")->key == 0);
    for (auto value : {L"Ctrl+", L"Q+Ctrl", L"Ctrl++Q", L"F25", L"F0", L"Meta+Q", L"Ctrl+Mouse"})
        CHECK(!Core::ParseShortcut(value));
}
void FocusPolicies() {
    for (bool global : {false, true}) {
        Core::Config config;
        Set(config, L"selectNewTabs", global ? L"true" : L"false");
        CHECK(Core::ShouldSelect(config, {}) == global);
        CHECK(Core::ShouldSelect(config, {true, false}) == global);
        CHECK(!Core::ShouldSelect(config, {true, true}));
        for (auto ordinary : {L"inherit", L"foreground", L"background", L"native"}) {
            Set(config, L"openTabs.folderMiddleClickFocus", ordinary);
            bool normal = std::wstring_view(ordinary) == L"foreground" ||
                          (std::wstring_view(ordinary) == L"inherit" && global);
            CHECK(Core::ShouldSelect(config, {true, false}) == normal);
            Set(config, L"openTabs.ctrlMiddleClick", L"background");
            CHECK(!Core::ShouldSelect(config, {true, true}));
            Set(config, L"openTabs.ctrlMiddleClick", L"foreground");
            CHECK(Core::ShouldSelect(config, {true, true}));
            Set(config, L"openTabs.ctrlMiddleClick", L"inherit");
            CHECK(Core::ShouldSelect(config, {true, true}) == normal);
        }
    }
}
void SelectionState() {
    Core::SelectionQueue window1, window2;
    Core::TabId first{}, second{};
    first[0] = 1; second[0] = 2;
    CHECK(window1.Enqueue(first, 10, 0, 2000));
    CHECK(!window1.Enqueue(first, 11, 0, 2000));
    CHECK(window1.Pending()->started == 10);
    CHECK(window1.Enqueue(second, 12, 0, 2000));
    CHECK(window2.Enqueue(first, 10, 0, 2000));
    CHECK(window1.Take(12)->tab == second);
    CHECK(!window1.Take(12));
    CHECK(window2.Take(13)->tab == first);
    CHECK(window1.Enqueue(first, 20, 50, 2000));
    CHECK(!window1.Take(69));
    CHECK(window1.Take(70)->tab == first);
    CHECK(window1.Enqueue(first, 100, 0, 100));
    CHECK(!window1.Take(200));
    CHECK(!window1.Pending());
    CHECK(window1.Enqueue(first, 300, 0, 100));
    window1.Cancel();
    CHECK(!window1.Take(301));
    CHECK(window1.Enqueue(second, 302, 0, 100));
    CHECK(window1.Take(302)->tab == second);
    for (unsigned i = 1; i <= 10000; ++i) {
        Core::TabId id{};
        id[0] = i & 255; id[1] = (i >> 8) & 255;
        CHECK(window1.Enqueue(id, i, 0, 2000));
    }
    auto last = window1.Take(10000);
    CHECK(last && last->tab[0] == (10000 & 255));
    CHECK(!window1.Pending());
}
void NestedCreation() {
    Core::CreationLedger ledger;
    Core::TabId first{}, second{};
    first[0] = 1; second[0] = 2;
    // Completion order differs from entry order. A different GUID on the
    // same frame is an independent creation, never a nested duplicate.
    ledger.Record({1, first, 2, 0});   // Inner AddTabAtIndex returns first.
    ledger.Record({1, second, 3, 1});  // Separate re-entrant create.
    ledger.Record({1, first, 1, 2});   // Outer AddTab returns same GUID.
    ledger.Record({2, first, 4, 3});   // Another window, same GUID bytes.
    auto records = ledger.Ordered();
    CHECK(records.size() == 3);
    CHECK(records[0].entryOrder == 1 && records[0].payload == 2);
    CHECK(records[1].tab == second && records[1].entryOrder == 3);
    CHECK(records[2].window == 2);
    ledger.Record({1, first, 5, 4});
    CHECK(ledger.Ordered()[0].payload == 2);
    Core::ExecutionGate gate;
    CHECK(gate.Enter());
    CHECK(!gate.Enter());  // Native SelectTab pumping another work message.
    CHECK(!gate.Enter());
    gate.Leave();
    CHECK(gate.Enter());
    gate.Leave();
}
void InputGestures() {
    Core::MiddleGesture mouse;
    auto down = mouse.Observe(Core::MousePhase::Down, 100, true);
    CHECK(down.intent.middleClick && down.intent.ctrl && down.cancelOlder);
    auto up = mouse.Observe(Core::MousePhase::Up, 100, false);
    CHECK(up.intent.middleClick && up.intent.ctrl);
    CHECK(!up.cancelOlder);  // Releasing Ctrl/middle must not cancel this open.
    up = mouse.Observe(Core::MousePhase::Up, 100, false);
    CHECK(!up.intent.ctrl && up.cancelOlder); // Unpaired up is separate input.
    down = mouse.Observe(Core::MousePhase::Down, 100, false);
    up = mouse.Observe(Core::MousePhase::Up, 100, true);
    CHECK(!up.intent.ctrl); // Ctrl pressed after DOWN doesn't change intent.
    down = mouse.Observe(Core::MousePhase::Down, 100, true);
    up = mouse.Observe(Core::MousePhase::Up, 200, false);
    CHECK(!up.intent.ctrl && up.cancelOlder); // No cross-window inheritance.
    mouse.Observe(Core::MousePhase::Down, 100, true);
    mouse.Observe(Core::MousePhase::Reset, 100, false);
    up = mouse.Observe(Core::MousePhase::Up, 100, false);
    CHECK(!up.intent.ctrl && up.cancelOlder);
}
void CallbackLifetimes() {
    Core::CallbackCounter callbacks;
    CHECK(callbacks.Idle());
    CHECK(callbacks.Enter(false, false));
    CHECK(!callbacks.Idle());
    callbacks.Leave();
    CHECK(callbacks.Idle());
    CHECK(!callbacks.Enter(true, false)); // Stop forbids new business.
    CHECK(callbacks.Idle());
    CHECK(callbacks.Enter(true, true)); // Post-stop NCDESTROY still runs code.
    CHECK(!callbacks.Idle());
    CHECK(callbacks.Enter(true, true)); // Release pumps nested shutdown.
    callbacks.Leave();
    CHECK(!callbacks.Idle()); // Shutdown returned, NCDESTROY not unwound yet.
    callbacks.Leave();
    CHECK(callbacks.Idle()); // Only now may owner state be destroyed.
}
void LegacyTitles() {
    CHECK(Core::ComputeDisplayTitle(L"C:\\Users\\Name\\Projects") == L"Projects");
    CHECK(Core::ComputeDisplayTitle(L"C:\\Foo\\") == L"Foo");
    CHECK(Core::ComputeDisplayTitle(L"/a/b/") == L"b");
    CHECK(Core::ComputeDisplayTitle(L"This PC") == L"This PC");
    CHECK(Core::ComputeDisplayTitle(L"C:\\") == L"C:");
    CHECK(Core::ComputeDisplayTitle(L"\\\\server\\share\\") == L"share");
    CHECK(Core::ComputeDisplayTitle(L"/") == L"/");
    CHECK(Core::ComputeDisplayTitle(L"").empty());
}
}  // namespace
int main() {
    SettingsContract(); ValidationAndShortcuts(); FocusPolicies();
    SelectionState(); NestedCreation(); InputGestures(); CallbackLifetimes(); LegacyTitles();
    std::cout << "PASS " << checks << " checks (production core, 45 contract settings, 8 exposed)\n";
}
