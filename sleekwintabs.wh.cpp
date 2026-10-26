// ==WindhawkMod==
// @id              sleek-wintabs
// @name            Sleek WinTabs
// @description     Explorer tab titles and queued foreground selection (experimental input-scoped Ctrl+middle-click)
// @version         1.2.0-alpha.1
// @author          SleekGPX
// @github          https://github.com/Aliceonly/sleek-windhawk-explorer-tabs
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -lcomctl32 -luuid
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Sleek WinTabs - implementation preview

Keeps the existing title fix. Replaces the fixed 150 ms global timer with
owner-window dispatch, nested creation deduplication, retained COM references,
and explicit cancellation/cleanup. The default additional delay is zero.

Ctrl+middle-click preserves native background behavior when creation runs in
the same dispatched/accelerated mouse-input call. Asynchronous/XAML correlation and
stable post-navigation selection still require Windows verification. Unknown
sources retain the legacy selectNewTabs rule. This is NOT a complete seven-
feature release: breadcrumb, header drop, close policy, Ctrl+Q duplication and
ordered Enter batches are not connected yet. No keyboard shortcut is captured.

Window association uses QueryInterface(IOleWindow), never a private offset.
If the frame doesn't expose that interface, only its selection tweak is skipped.
Use debug logging to verify this capability on your Windows build.

Do not combine with another mod that overrides Explorer tab selection.
See the repository's implementation status and Windows acceptance checklist.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- fixTabTitles: true
  $name: Show folder name instead of full path
- selectNewTabs: true
  $name: Jump to newly opened tabs
  $description: Unknown input sources retain this legacy rule. Background rules do not undo native activation.
- openTabs:
  - folderMiddleClickFocus: inherit
    $name: Folder middle-click focus (synchronous input only)
    $options:
    - inherit: Follow global selection setting
    - foreground: Select the created tab
    - background: Leave native background behavior unchanged
    - native: Do not override Explorer
  - ctrlMiddleClick: background
    $name: Ctrl+middle-click (synchronous input only)
    $options:
    - background: Leave native background behavior unchanged
    - foreground: Select the created tab
    - inherit: Follow ordinary middle-click rule
- advanced:
  - extraSelectDelayMs: 0
    $name: Additional selection delay (0-1000 ms)
    $description: Zero uses a posted message, not a fixed wait. Readiness is not yet verified on Windows.
  - selectionDeadlineMs: 2000
    $name: Discard stale selection after (100-10000 ms)
- diagnostics:
  - level: errors
    $name: Diagnostic logging
    $options:
    - off: Off
    - errors: Errors only
    - debug: Debug events (no paths)
  - measureLatency: false
    $name: Log queue-to-selection-call latency
    $description: This is NOT input-to-visible-tab latency. Uses GetTickCount64 (millisecond resolution).
*/
// ==/WindhawkModSettings==

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Core {
enum class SettingKind { Boolean, Integer, Choice, Shortcut };
struct SettingSpec {
    std::wstring_view key;
    SettingKind kind;
    std::wstring_view initial;
    std::wstring_view choices;
    int minimum = 0;
    int maximum = 0;
    bool exposed = false;
};

// The approved contract includes future stages; only implemented settings are
// exposed in Windhawk. Tests and the contract checker use this same table.
constexpr SettingSpec kSettings[] = {
    {L"fixTabTitles", SettingKind::Boolean, L"true", L"", 0, 0, true},
    {L"selectNewTabs", SettingKind::Boolean, L"true", L"", 0, 0, true},
    {L"openTabs.folderMiddleClickFocus", SettingKind::Choice, L"inherit", L"inherit|foreground|background|native", 0, 0, true},
    {L"openTabs.contextMenuFocus", SettingKind::Choice, L"inherit", L"inherit|foreground|background|native"},
    {L"openTabs.position", SettingKind::Choice, L"native", L"native|afterCurrent|end"},
    {L"openTabs.ctrlMiddleClick", SettingKind::Choice, L"background", L"background|foreground|inherit", 0, 0, true},
    {L"breadcrumb.enabled", SettingKind::Boolean, L"true", L""},
    {L"breadcrumb.includeDropdown", SettingKind::Boolean, L"true", L""},
    {L"breadcrumb.focus", SettingKind::Choice, L"inherit", L"inherit|foreground|background"},
    {L"breadcrumb.position", SettingKind::Choice, L"inherit", L"inherit|afterCurrent|end"},
    {L"tabDrop.enabled", SettingKind::Boolean, L"true", L""},
    {L"tabDrop.operation", SettingKind::Choice, L"move", L"move|copy|native"},
    {L"tabDrop.hoverDelayMs", SettingKind::Integer, L"0", L"", 0, 1000},
    {L"tabDrop.afterDrop", SettingKind::Choice, L"stay", L"stay|return"},
    {L"tabDrop.allowExternalSources", SettingKind::Boolean, L"true", L""},
    {L"closeTabs.selection", SettingKind::Choice, L"left", L"native|leftmost|left"},
    {L"closeTabs.scope", SettingKind::Choice, L"any", L"any|active"},
    {L"closeTabs.noLeftNeighbor", SettingKind::Choice, L"leftmost", L"leftmost|native"},
    {L"duplicate.enabled", SettingKind::Boolean, L"true", L""},
    {L"duplicate.shortcut", SettingKind::Shortcut, L"Ctrl+Q", L""},
    {L"duplicate.target", SettingKind::Choice, L"currentWindow", L"currentWindow|newWindow"},
    {L"duplicate.position", SettingKind::Choice, L"afterCurrent", L"afterCurrent|end"},
    {L"duplicate.focus", SettingKind::Choice, L"foreground", L"foreground|background|inherit"},
    {L"duplicate.restoreSelection", SettingKind::Boolean, L"true", L""},
    {L"duplicate.scope", SettingKind::Choice, L"nonEditingExplorer", L"nonEditingExplorer|fileList"},
    {L"batchOpen.enabled", SettingKind::Boolean, L"true", L""},
    {L"batchOpen.shortcut", SettingKind::Shortcut, L"Enter", L""},
    {L"batchOpen.minimumFolders", SettingKind::Integer, L"2", L"", 1, 1000},
    {L"batchOpen.target", SettingKind::Choice, L"newWindow", L"newWindow|currentWindow"},
    {L"batchOpen.order", SettingKind::Choice, L"view", L"view|nameAscending|nameDescending"},
    {L"batchOpen.position", SettingKind::Choice, L"end", L"afterCurrent|end"},
    {L"batchOpen.windowFocus", SettingKind::Choice, L"foreground", L"foreground|background"},
    {L"batchOpen.selectedTab", SettingKind::Choice, L"first", L"first|last|preserve"},
    {L"batchOpen.mixedSelection", SettingKind::Choice, L"native", L"native|foldersOnly"},
    {L"batchOpen.includeFolderShortcuts", SettingKind::Boolean, L"false", L""},
    {L"batchOpen.includeShellFolders", SettingKind::Boolean, L"false", L""},
    {L"batchOpen.onError", SettingKind::Choice, L"continue", L"continue|stop"},
    {L"batchOpen.maxFolders", SettingKind::Integer, L"0", L"", 0, 2147483647},
    {L"batchOpen.cancelShortcut", SettingKind::Shortcut, L"Escape", L""},
    {L"advanced.extraSelectDelayMs", SettingKind::Integer, L"0", L"", 0, 1000, true},
    {L"advanced.selectionDeadlineMs", SettingKind::Integer, L"2000", L"", 100, 10000, true},
    {L"advanced.batchCreateIntervalMs", SettingKind::Integer, L"0", L"", 0, 1000},
    {L"advanced.windowBindTimeoutMs", SettingKind::Integer, L"5000", L"", 500, 30000},
    {L"diagnostics.level", SettingKind::Choice, L"errors", L"off|errors|debug", 0, 0, true},
    {L"diagnostics.measureLatency", SettingKind::Boolean, L"false", L"", 0, 0, true},
};
constexpr size_t kSettingCount = sizeof(kSettings) / sizeof(*kSettings);
static_assert(kSettingCount == 45);

std::wstring Lower(std::wstring_view value) {
    std::wstring result(value);
    for (auto& c : result) c = static_cast<wchar_t>(std::towlower(c));
    return result;
}
bool HasChoice(std::wstring_view choices, std::wstring_view value) {
    while (!choices.empty()) {
        auto separator = choices.find(L'|');
        if (choices.substr(0, separator) == value) return true;
        if (separator == std::wstring_view::npos) break;
        choices.remove_prefix(separator + 1);
    }
    return false;
}
std::optional<int> ParseInteger(std::wstring_view text) {
    if (text.empty()) return std::nullopt;
    int value = 0;
    for (auto c : text) {
        if (c < L'0' || c > L'9' || value > (2147483647 - (c - L'0')) / 10)
            return std::nullopt;
        value = value * 10 + (c - L'0');
    }
    return value;
}
struct Shortcut {
    unsigned modifiers = 0;  // Ctrl, Shift, Alt, Win.
    unsigned key = 0;        // Windows virtual key; zero disables the binding.
    bool operator==(const Shortcut& other) const {
        return modifiers == other.modifiers && key == other.key;
    }
};
std::optional<Shortcut> ParseShortcut(std::wstring_view text) {
    Shortcut result;
    if (text.empty()) return result;
    auto normalized = Lower(text);
    text = normalized;
    while (true) {
        auto separator = text.find(L'+');
        auto part = text.substr(0, separator);
        if (part.empty()) return std::nullopt;
        if (separator != std::wstring_view::npos) {
            unsigned flag = part == L"ctrl" ? 1 : part == L"shift" ? 2 :
                            part == L"alt" ? 4 : part == L"win" ? 8 : 0;
            if (!flag || (result.modifiers & flag)) return std::nullopt;
            result.modifiers |= flag;
            text.remove_prefix(separator + 1);
            continue;
        }
        if (part.size() == 1 && ((part[0] >= L'a' && part[0] <= L'z') ||
                                 (part[0] >= L'0' && part[0] <= L'9'))) {
            result.key = static_cast<unsigned>(std::towupper(part[0]));
        } else if (part == L"enter") result.key = 0x0D;
        else if (part == L"escape") result.key = 0x1B;
        else if (part == L"tab") result.key = 0x09;
        else if (part == L"space") result.key = 0x20;
        else if (part == L"delete") result.key = 0x2E;
        else if (part == L"insert") result.key = 0x2D;
        else if (part == L"home") result.key = 0x24;
        else if (part == L"end") result.key = 0x23;
        else if (part == L"pageup") result.key = 0x21;
        else if (part == L"pagedown") result.key = 0x22;
        else if (part[0] == L'f') {
            auto number = ParseInteger(part.substr(1));
            if (!number || *number < 1 || *number > 24) return std::nullopt;
            result.key = 0x70 + *number - 1;
        } else return std::nullopt;
        return result;
    }
}

struct Config {
    std::array<std::wstring, kSettingCount> values;
    Config() {
        for (size_t i = 0; i < kSettingCount; ++i) values[i] = kSettings[i].initial;
    }
    static size_t Index(std::wstring_view key) {
        for (size_t i = 0; i < kSettingCount; ++i)
            if (kSettings[i].key == key) return i;
        return kSettingCount;
    }
    const std::wstring& Get(std::wstring_view key) const {
        return values.at(Index(key));
    }
    bool Boolean(std::wstring_view key) const { return Get(key) == L"true"; }
    int Integer(std::wstring_view key) const { return *ParseInteger(Get(key)); }
    bool Set(std::wstring_view key, std::wstring_view value, std::wstring& error) {
        auto i = Index(key);
        if (i == kSettingCount) { error = L"Unknown setting: " + std::wstring(key); return false; }
        const auto& spec = kSettings[i];
        bool valid = false;
        switch (spec.kind) {
            case SettingKind::Boolean: valid = value == L"true" || value == L"false"; break;
            case SettingKind::Integer: {
                auto number = ParseInteger(value);
                valid = number && *number >= spec.minimum && *number <= spec.maximum;
                break;
            }
            case SettingKind::Choice: valid = HasChoice(spec.choices, value); break;
            case SettingKind::Shortcut: valid = ParseShortcut(value).has_value(); break;
        }
        if (!valid) { error = L"Invalid setting: " + std::wstring(key); return false; }
        values[i] = value;
        error.clear();
        return true;
    }
    bool Validate(std::wstring& error) const {
        if (Integer(L"advanced.extraSelectDelayMs") >= Integer(L"advanced.selectionDeadlineMs")) {
            error = L"extraSelectDelayMs must be smaller than selectionDeadlineMs"; return false;
        }
        auto maximum = Integer(L"batchOpen.maxFolders");
        if (maximum && maximum < Integer(L"batchOpen.minimumFolders")) {
            error = L"maxFolders must be zero or at least minimumFolders"; return false;
        }
        std::vector<Shortcut> used;
        auto add = [&](std::wstring_view key) {
            auto shortcut = *ParseShortcut(Get(key));
            if (!shortcut.key) return true;
            if (std::find(used.begin(), used.end(), shortcut) != used.end()) return false;
            used.push_back(shortcut); return true;
        };
        if ((Boolean(L"duplicate.enabled") && !add(L"duplicate.shortcut")) ||
            (Boolean(L"batchOpen.enabled") &&
             (!add(L"batchOpen.shortcut") || !add(L"batchOpen.cancelShortcut")))) {
            error = L"Enabled shortcuts must not conflict"; return false;
        }
        error.clear();
        return true;
    }
};

enum class Focus { Inherit, Foreground, Background, Native };
Focus ParseFocus(std::wstring_view value) {
    return value == L"foreground" ? Focus::Foreground :
           value == L"background" ? Focus::Background :
           value == L"native" ? Focus::Native : Focus::Inherit;
}
struct InputIntent { bool middleClick = false; bool ctrl = false; };
bool ShouldSelect(const Config& config, InputIntent input) {
    auto focus = Focus::Inherit;
    if (input.middleClick) {
        focus = ParseFocus(config.Get(L"openTabs.folderMiddleClickFocus"));
        if (input.ctrl) {
            auto override = ParseFocus(config.Get(L"openTabs.ctrlMiddleClick"));
            if (override != Focus::Inherit) focus = override;
        }
    }
    if (focus == Focus::Foreground) return true;
    if (focus == Focus::Background || focus == Focus::Native) return false;
    return config.Boolean(L"selectNewTabs");
}

using TabId = std::array<unsigned char, 16>;
struct CreationRecord {
    uint64_t window;
    TabId tab;
    uint64_t entryOrder;
    size_t payload;
};
class CreationLedger {
    std::vector<CreationRecord> records_;
 public:
    void Record(CreationRecord value) {
        for (auto& old : records_) {
            if (old.window == value.window && old.tab == value.tab) {
                // AddTab -> AddTabAtIndex returns the same GUID. Prefer the
                // outer call's intent, but never merge different tab GUIDs.
                if (value.entryOrder < old.entryOrder) old = value;
                return;
            }
        }
        records_.push_back(value);
    }
    auto Ordered() const {
        auto result = records_;
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return a.entryOrder < b.entryOrder;
        });
        return result;
    }
};
class ExecutionGate {
    bool active_ = false;
 public:
    bool Enter() { if (active_) return false; active_ = true; return true; }
    void Leave() { active_ = false; }
};
class CallbackCounter {
    size_t active_ = 0;
 public:
    // Synchronization belongs to the runtime. Existing subclasses must remain
    // counted after stop, even though new business operations are forbidden.
    bool Enter(bool stopping, bool existingCallback) {
        if (stopping && !existingCallback) return false;
        ++active_;
        return true;
    }
    void Leave() { --active_; }
    bool Idle() const { return active_ == 0; }
};
enum class MousePhase { Down, Up, Other, Reset };
struct GestureResult { InputIntent intent; bool cancelOlder = false; };
class MiddleGesture {
    uintptr_t target_ = 0;
    bool ctrl_ = false;
 public:
    GestureResult Observe(MousePhase phase, uintptr_t target, bool ctrl) {
        if (phase == MousePhase::Down) {
            target_ = target; ctrl_ = ctrl;
            return {{true, ctrl}, true};
        }
        if (phase == MousePhase::Up) {
            bool paired = target_ && target_ == target;
            auto result = GestureResult{{true, paired ? ctrl_ : ctrl}, !paired};
            target_ = 0; ctrl_ = false;
            return result;
        }
        if (phase == MousePhase::Reset) { target_ = 0; ctrl_ = false; }
        return {};
    }
};
static_assert(std::is_trivially_destructible_v<MiddleGesture>);
struct SelectionRequest {
    TabId tab;
    uint64_t generation;
    uint64_t started;
    uint64_t due;
    uint64_t deadline;
};
class SelectionQueue {
    uint64_t generation_ = 0;
    std::optional<SelectionRequest> pending_;
 public:
    void Cancel() { ++generation_; pending_.reset(); }
    bool Enqueue(TabId tab, uint64_t now, unsigned delay, unsigned deadline) {
        if (pending_ && pending_->tab == tab) return false;
        ++generation_;
        pending_ = SelectionRequest{tab, generation_, now, now + delay, now + deadline};
        return true;
    }
    std::optional<SelectionRequest> Take(uint64_t now) {
        if (!pending_) return std::nullopt;
        if (now >= pending_->deadline || pending_->generation != generation_) { Cancel(); return std::nullopt; }
        if (now < pending_->due) return std::nullopt;
        auto result = pending_;
        pending_.reset();
        return result;
    }
    const std::optional<SelectionRequest>& Pending() const { return pending_; }
};

std::wstring ComputeDisplayTitle(const wchar_t* title) {
    std::wstring_view sv(title);
    while (sv.size() > 1 && (sv.back() == L'\\' || sv.back() == L'/')) sv.remove_suffix(1);
    size_t pos = sv.find_last_of(L"\\/");
    if (pos == std::wstring_view::npos || pos + 1 >= sv.size()) return std::wstring(sv);
    return std::wstring(sv.substr(pos + 1));
}
}  // namespace Core

#ifndef SLEEKWINTABS_CORE_TEST

#include <windhawk_utils.h>
#include <commctrl.h>
#include <oleidl.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <unordered_map>

namespace Runtime {
std::shared_ptr<const Core::Config> g_config = std::make_shared<Core::Config>();
std::atomic<bool> g_stopping{false};
HMODULE g_explorerFrame;
UINT g_dispatchMessage;
constexpr UINT_PTR kSubclassId = 0x53575442;
constexpr LPARAM kSelect = 1;
constexpr LPARAM kCancel = 2;
constexpr LPARAM kShutdown = 3;

using SelectTab = HRESULT(WINAPI*)(void*, GUID);
using AddTab = HRESULT(WINAPI*)(void*, const void*, int, GUID*);
using AddTabAtIndex = HRESULT(WINAPI*)(void*, const void*, int, int, GUID*);
using SetFrameTitle = void(WINAPI*)(void*, PCWSTR, GUID);
using TranslateAccelerator = HRESULT(WINAPI*)(void*, MSG*);
SelectTab g_selectTab;
AddTab g_addTab;
AddTabAtIndex g_addTabAtIndex;
SetFrameTitle g_setFrameTitle;
TranslateAccelerator g_translateGlobal;
decltype(&DispatchMessageW) g_dispatchW;
decltype(&DispatchMessageA) g_dispatchA;

auto Settings() { return std::atomic_load(&g_config); }
bool LogErrors() { return Settings()->Get(L"diagnostics.level") != L"off"; }
bool LogDebug() { return Settings()->Get(L"diagnostics.level") == L"debug"; }

// Scoped to DispatchMessage/TranslateAcceleratorGlobal input calls. No delayed
// GetAsyncKeyState sampling or time-window attribution is used.
struct DispatchedInput {
    HWND root = nullptr;
    Core::InputIntent intent;
};
thread_local DispatchedInput g_input;
static_assert(std::is_trivially_destructible_v<DispatchedInput>);
constinit thread_local Core::MiddleGesture g_middleGesture;
thread_local MSG g_lastInputMessage{};
thread_local Core::GestureResult g_lastGesture{};
thread_local uint64_t g_creationSequence = 0;

struct WindowState {
    HWND hwnd;
    ULONG_PTR incarnation;
    Core::SelectionQueue queue;
    uint64_t intentEpoch = 0;
    IUnknown* actions = nullptr;
    std::shared_ptr<const Core::Config> pendingConfig;
    UINT_PTR timer = 0;
    Core::ExecutionGate selectionGate;
    bool detached = false;
};
struct Registry {
    std::mutex mutex;
    std::unordered_map<HWND, std::shared_ptr<WindowState>> windows;
    ULONG_PTR nextIncarnation = 0;
};
// Created at init, deleted on normal unload. On process exit the raw pointer
// has no destructor that could touch Explorer's already-exited apartments.
Registry* g_registry;
std::mutex g_activityMutex;
std::condition_variable g_activityChanged;
Core::CallbackCounter g_activeCallbacks;
struct Activity {
    bool entered = false;
    explicit Activity(bool existingCallback = false) {
        std::lock_guard lock(g_activityMutex);
        entered = g_activeCallbacks.Enter(g_stopping.load(), existingCallback);
    }
    ~Activity() {
        if (!entered) return;
        std::lock_guard lock(g_activityMutex);
        g_activeCallbacks.Leave();
        if (g_activeCallbacks.Idle()) g_activityChanged.notify_all();
    }
};
Registry& Windows() { return *g_registry; }
auto FindWindowState(HWND hwnd) {
    auto& registry = Windows();
    std::lock_guard lock(registry.mutex);
    auto it = registry.windows.find(hwnd);
    return it == registry.windows.end() ? std::shared_ptr<WindowState>{} : it->second;
}

void ClearPending(WindowState& state) {
    state.queue.Cancel();
    state.pendingConfig.reset();
    if (state.timer) {
        KillTimer(state.hwnd, state.timer);
        state.timer = 0;
    }
    // Detach before Release, since Release can re-enter native window code.
    auto* actions = state.actions;
    state.actions = nullptr;
    if (actions) actions->Release();
}
void Cancel(WindowState& state) {
    ++state.intentEpoch;
    ClearPending(state);
}

HWND FrameWindow(void* tabActions) {
    // AddTab/SelectTab use a COM interface subobject. QueryInterface performs
    // any required this adjustment; private frame methods must not be called
    // with this pointer or a hardcoded complete-object offset.
    IOleWindow* window = nullptr;
    auto* unknown = static_cast<IUnknown*>(tabActions);
    if (FAILED(unknown->QueryInterface(IID_IOleWindow,
                                     reinterpret_cast<void**>(&window)))) return nullptr;
    HWND hwnd = nullptr;
    HRESULT hr = window->GetWindow(&hwnd);
    window->Release();
    if (FAILED(hr) || !hwnd) return nullptr;
    hwnd = GetAncestor(hwnd, GA_ROOT);
    DWORD process = 0;
    DWORD thread = GetWindowThreadProcessId(hwnd, &process);
    if (process != GetCurrentProcessId() || thread != GetCurrentThreadId()) return nullptr;
    wchar_t name[64]{};
    GetClassNameW(hwnd, name, ARRAYSIZE(name));
    if (wcscmp(name, L"CabinetWClass") && wcscmp(name, L"ExploreWClass")) return nullptr;
    return hwnd;
}

Core::TabId ToCoreId(const GUID& guid) {
    static_assert(sizeof(guid) == sizeof(Core::TabId));
    Core::TabId result;
    std::memcpy(result.data(), &guid, sizeof(guid));
    return result;
}
GUID FromCoreId(const Core::TabId& id) {
    GUID result;
    std::memcpy(&result, id.data(), sizeof(result));
    return result;
}

void DispatchSelection(WindowState& state) {
    if (state.timer) {
        KillTimer(state.hwnd, state.timer);
        state.timer = 0;
    }
    if (!state.queue.Pending()) return;
    if (g_stopping.load() || state.detached || state.pendingConfig != Settings()) {
        ClearPending(state);
        return;
    }
    // Native SelectTab can pump messages. A nested dispatcher must leave new
    // work queued, never recursively enter SelectTab on the same window.
    if (!state.selectionGate.Enter()) return;
    struct GateGuard {
        Core::ExecutionGate& gate;
        ~GateGuard() { gate.Leave(); }
    } guard{state.selectionGate};
    auto now = GetTickCount64();
    auto request = state.queue.Take(now);
    if (!request) {
        if (!state.queue.Pending()) { ClearPending(state); return; }
        auto delay = state.queue.Pending()->due - now;
        // HWND timer messages have no TIMERPROC that could survive unhooking.
        state.timer = reinterpret_cast<UINT_PTR>(&state);
        if (!SetTimer(state.hwnd, state.timer, static_cast<UINT>(delay), nullptr)) {
            state.timer = 0;
            ClearPending(state);
            if (LogErrors()) Wh_Log(L"Selection timer creation failed");
        }
        return;
    }
    auto* actions = state.actions;
    state.actions = nullptr;
    auto config = state.pendingConfig;
    state.pendingConfig.reset();
    if (!actions) return;
    auto start = GetTickCount64();
    HRESULT hr = g_selectTab(actions, FromCoreId(request->tab));
    actions->Release();
    if (FAILED(hr) && LogErrors()) Wh_Log(L"SelectTab failed: 0x%08X", hr);
    if (config->Boolean(L"diagnostics.measureLatency")) {
        Wh_Log(L"Selection dispatch: window=%llu generation=%llu queue=%llu ms call=%llu ms hr=0x%08X",
               static_cast<unsigned long long>(state.incarnation),
               static_cast<unsigned long long>(request->generation),
               start - request->started, GetTickCount64() - start, hr);
    }
    if (!g_stopping.load() && !state.detached && state.queue.Pending()) {
        if (!PostMessageW(state.hwnd, g_dispatchMessage, state.incarnation, kSelect)) ClearPending(state);
    }
}

void DetachWindow(const std::shared_ptr<WindowState>& state) {
    state->detached = true;
    Cancel(*state);
    auto& registry = Windows();
    std::lock_guard lock(registry.mutex);
    auto it = registry.windows.find(state->hwnd);
    if (it != registry.windows.end() && it->second == state) registry.windows.erase(it);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                            UINT_PTR, DWORD_PTR refData) {
    Activity activity(true);
    auto state = FindWindowState(hwnd);
    if (!state || state->incarnation != refData)
        return DefSubclassProc(hwnd, message, wParam, lParam);
    if (message == g_dispatchMessage && wParam == state->incarnation) {
        if (lParam == kShutdown) {
            RemoveWindowSubclass(hwnd, WindowProc, kSubclassId);
            DetachWindow(state);
        } else if (lParam == kCancel) {
            if (state->pendingConfig && state->pendingConfig != Settings()) ClearPending(*state);
        }
        else if (lParam == kSelect) DispatchSelection(*state);
        return 0;
    }
    if (message == WM_TIMER && state->timer && wParam == state->timer) {
        DispatchSelection(*state);
        return 0;
    }
    if (message == WM_CLOSE || (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE))
        Cancel(*state);
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, WindowProc, kSubclassId);
        DetachWindow(state);
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

auto EnsureWindow(HWND hwnd) {
    if (auto state = FindWindowState(hwnd))
        return state->detached ? std::shared_ptr<WindowState>{} : state;
    auto& registry = Windows();
    auto state = std::make_shared<WindowState>();
    state->hwnd = hwnd;
    {
        std::lock_guard lock(registry.mutex);
        if (g_stopping.load()) return std::shared_ptr<WindowState>{};
        state->incarnation = ++registry.nextIncarnation;
        registry.windows.emplace(hwnd, state);
        // Same-thread SetWindowSubclass doesn't dispatch messages. Keep the
        // registry lock through installation so unload cannot miss this state.
        if (!SetWindowSubclass(hwnd, WindowProc, kSubclassId, state->incarnation)) {
            registry.windows.erase(hwnd);
            return std::shared_ptr<WindowState>{};
        }
    }
    return state;
}

void Schedule(void* actions, GUID tab,
              const std::shared_ptr<const Core::Config>& config,
              const std::shared_ptr<WindowState>& state, uint64_t epoch,
              DispatchedInput input) {
    if (g_stopping.load() || !g_selectTab) return;
    if (!state || state->detached || state->intentEpoch != epoch || config != Settings()) return;
    HWND hwnd = state->hwnd;
    if (FindWindowState(hwnd) != state) return;
    if (input.root != hwnd) input.intent = {};
    // A later background request must also cancel an older foreground one.
    if (!Core::ShouldSelect(*config, input.intent)) {
        ClearPending(*state);
        return;
    }
    if (!state->queue.Enqueue(ToCoreId(tab), GetTickCount64(),
                             config->Integer(L"advanced.extraSelectDelayMs"),
                             config->Integer(L"advanced.selectionDeadlineMs"))) return;
    auto generation = state->queue.Pending()->generation;
    auto* retained = static_cast<IUnknown*>(actions);
    retained->AddRef();
    auto* old = state->actions;
    state->actions = retained;
    state->pendingConfig = config;
    if (old) old->Release();
    if (!state->queue.Pending() || state->queue.Pending()->generation != generation) return;
    if (!PostMessageW(hwnd, g_dispatchMessage, state->incarnation, kSelect)) {
        ClearPending(*state);
        if (LogErrors()) Wh_Log(L"Selection dispatch failed");
    } else if (LogDebug()) {
        Wh_Log(L"Selection queued: window=%llu generation=%llu middle=%d ctrl=%d",
               static_cast<unsigned long long>(state->incarnation),
               static_cast<unsigned long long>(generation),
               input.intent.middleClick, input.intent.ctrl);
    }
}

struct ReleaseCom { void operator()(IUnknown* value) const { if (value) value->Release(); } };
struct CreationCompletion {
    std::unique_ptr<IUnknown, ReleaseCom> actions;
    GUID tab;
    std::shared_ptr<const Core::Config> config;
    std::shared_ptr<WindowState> window;
    uint64_t epoch;
    DispatchedInput input;
};
struct CreationBatch {
    Core::CreationLedger ledger;
    std::vector<CreationCompletion> completed;
};
// The TLS pointer is trivial. Every batch/COM destructor runs on the owning
// hook stack, not as a deferred TLS destructor after DLL unload.
thread_local CreationBatch* g_creationBatch;
struct CreationScope {
    void* actions;
    std::shared_ptr<const Core::Config> config = Settings();
    DispatchedInput input = g_input;
    std::shared_ptr<WindowState> window;
    uint64_t epoch = 0;
    uint64_t order = ++g_creationSequence;
    std::unique_ptr<CreationBatch> owned;
    CreationBatch* batch;
    explicit CreationScope(void* value) : actions(value) {
        if (!g_creationBatch) {
            owned = std::make_unique<CreationBatch>();
            g_creationBatch = owned.get();
        }
        batch = g_creationBatch;
        if (HWND hwnd = FrameWindow(value)) {
            window = EnsureWindow(hwnd);
            if (window) epoch = window->intentEpoch;
        } else if (LogDebug()) Wh_Log(L"Selection skipped: no owner-thread IOleWindow capability");
    }
    bool Valid() const {
        return !g_stopping.load() && window && !window->detached &&
               window->intentEpoch == epoch && config == Settings();
    }
    void Complete(HRESULT hr, const GUID* tab) {
        if (FAILED(hr) || !tab || !Valid()) return;
        auto* unknown = static_cast<IUnknown*>(actions);
        unknown->AddRef();
        batch->completed.push_back({std::unique_ptr<IUnknown, ReleaseCom>(unknown),
                                    *tab, config, window, epoch, input});
        batch->ledger.Record({window->incarnation, ToCoreId(*tab), order,
                              batch->completed.size() - 1});
    }
    ~CreationScope() {
        if (!owned) return;
        g_creationBatch = nullptr;
        auto ordered = owned->ledger.Ordered();
        if (LogDebug()) Wh_Log(L"Creation batch: returns=%llu unique=%llu",
                              static_cast<unsigned long long>(owned->completed.size()),
                              static_cast<unsigned long long>(ordered.size()));
        for (const auto& record : ordered) {
            const auto& done = owned->completed[record.payload];
            Schedule(done.actions.get(), done.tab, done.config, done.window, done.epoch, done.input);
        }
    }
};
HRESULT WINAPI AddTabHook(void* actions, const void* pidl, int entryPoint, GUID* out) {
    Activity activity;
    if (!activity.entered || !g_selectTab) return g_addTab(actions, pidl, entryPoint, out);
    CreationScope scope(actions);
    HRESULT hr = g_addTab(actions, pidl, entryPoint, out);
    scope.Complete(hr, out);
    return hr;
}
HRESULT WINAPI AddTabAtIndexHook(void* actions, const void* pidl, int entryPoint, int index, GUID* out) {
    Activity activity;
    if (!activity.entered || !g_selectTab) return g_addTabAtIndex(actions, pidl, entryPoint, index, out);
    CreationScope scope(actions);
    HRESULT hr = g_addTabAtIndex(actions, pidl, entryPoint, index, out);
    scope.Complete(hr, out);
    return hr;
}
void WINAPI SetFrameTitleHook(void* frame, PCWSTR title, GUID tab) {
    if (Settings()->Boolean(L"fixTabTitles") && title && *title) {
        auto display = Core::ComputeDisplayTitle(title);
        if (display != title) { g_setFrameTitle(frame, display.c_str(), tab); return; }
    }
    g_setFrameTitle(frame, title, tab);
}

template <typename Callback>
auto WithInput(const MSG* message, Callback original) {
    Activity activity;
    if (!activity.entered || !message) return original();
    auto previous = g_input;
    struct InputGuard {
        DispatchedInput previous;
        ~InputGuard() { g_input = previous; }
    } guard{previous};
    // Unrelated re-entrant dispatch must not inherit the outer click's intent.
    g_input = {};
    HWND root = GetAncestor(message->hwnd, GA_ROOT);
    UINT kind = message->message;
    bool middle = kind == WM_MBUTTONDOWN || kind == WM_MBUTTONUP || kind == WM_MBUTTONDBLCLK;
    bool mouseDown = kind == WM_LBUTTONDOWN || kind == WM_RBUTTONDOWN ||
                     kind == WM_XBUTTONDOWN || kind == WM_NCLBUTTONDOWN ||
                     kind == WM_NCMBUTTONDOWN || kind == WM_NCRBUTTONDOWN;
    bool keyDown = kind == WM_KEYDOWN || kind == WM_SYSKEYDOWN;
    bool reset = kind == WM_CAPTURECHANGED || kind == WM_CANCELMODE;
    if (middle || mouseDown || keyDown || reset) {
        // TranslateAccelerator and DispatchMessage may see the exact same
        // queued MSG. This is identity deduplication, not a timing heuristic.
        const auto& last = g_lastInputMessage;
        bool duplicate = last.hwnd == message->hwnd && last.message == kind &&
                         last.wParam == message->wParam && last.lParam == message->lParam &&
                         last.time == message->time;
        auto gesture = g_lastGesture;
        if (!duplicate) {
            auto phase = kind == WM_MBUTTONUP ? Core::MousePhase::Up :
                         middle ? Core::MousePhase::Down :
                         (reset || mouseDown) ? Core::MousePhase::Reset : Core::MousePhase::Other;
            gesture = g_middleGesture.Observe(phase, reinterpret_cast<uintptr_t>(message->hwnd),
                                             (message->wParam & MK_CONTROL) != 0);
            g_lastInputMessage = *message;
            g_lastGesture = gesture;
            if (gesture.cancelOlder || mouseDown || keyDown) {
                if (auto state = FindWindowState(root)) Cancel(*state);
            }
        }
        if (middle) g_input = {root, gesture.intent};
    }
    return original();
}
LRESULT WINAPI DispatchWHook(const MSG* message) {
    return WithInput(message, [&] { return g_dispatchW(message); });
}
LRESULT WINAPI DispatchAHook(const MSG* message) {
    return WithInput(message, [&] { return g_dispatchA(message); });
}
HRESULT WINAPI TranslateGlobalHook(void* frame, MSG* message) {
    return WithInput(message, [&] { return g_translateGlobal(frame, message); });
}

void Broadcast(LPARAM operation) {
    std::vector<std::shared_ptr<WindowState>> states;
    {
        auto& registry = Windows();
        std::lock_guard lock(registry.mutex);
        for (const auto& entry : registry.windows) states.push_back(entry.second);
    }
    for (const auto& state : states) {
        if (operation == kShutdown) {
            // Never hold the registry lock while waiting for an owner thread.
            // No timeout/forced unload: it could leave a live subclass in DLL.
            SendMessageW(state->hwnd, g_dispatchMessage, state->incarnation, operation);
        } else {
            // Settings changes need no UI-thread wait. Snapshot identity also
            // rejects stale in-flight creations before this message arrives.
            PostMessageW(state->hwnd, g_dispatchMessage, state->incarnation, operation);
        }
    }
}

bool LoadSettings(bool initial = false) {
    Core::Config candidate;
    Core::Config fallback;
    // Legacy keys predate this schema. Even if a newly added setting is absent
    // or invalid during first-load migration, preserve explicit legacy false.
    fallback.values[Core::Config::Index(L"fixTabTitles")] =
        Wh_GetIntSetting(L"fixTabTitles") ? L"true" : L"false";
    fallback.values[Core::Config::Index(L"selectNewTabs")] =
        Wh_GetIntSetting(L"selectNewTabs") ? L"true" : L"false";
    std::wstring error;
    auto rejected = [&] {
        Wh_Log(L"Settings rejected: %s", error.c_str());
        if (initial) {
            std::shared_ptr<const Core::Config> snapshot = std::make_shared<Core::Config>(fallback);
            std::atomic_store(&g_config, std::move(snapshot));
            Wh_Log(L"Using new-setting defaults; legacy title and selection values retained");
        } else Wh_Log(L"Keeping previous settings snapshot");
        return false;
    };
    for (const auto& spec : Core::kSettings) {
        if (!spec.exposed) continue;
        std::wstring value;
        if (spec.kind == Core::SettingKind::Boolean) {
            int raw = Wh_GetIntSetting(spec.key.data());
            value = raw == 0 ? L"false" : raw == 1 ? L"true" : std::to_wstring(raw);
        } else if (spec.kind == Core::SettingKind::Integer) {
            value = std::to_wstring(Wh_GetIntSetting(spec.key.data()));
        } else {
            PCWSTR raw = Wh_GetStringSetting(spec.key.data());
            value = raw ? raw : L"";
            if (raw) Wh_FreeStringSetting(raw);
        }
        if (!candidate.Set(spec.key, value, error)) {
            return rejected();
        }
    }
    if (!candidate.Validate(error)) {
        return rejected();
    }
    std::shared_ptr<const Core::Config> snapshot = std::make_shared<Core::Config>(std::move(candidate));
    std::atomic_store(&g_config, std::move(snapshot));
    return true;
}

bool HookExplorer() {
    g_explorerFrame = LoadLibraryW(L"explorerframe.dll");
    if (!g_explorerFrame) return false;
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {{LR"(public: void __cdecl CExplorerFrame::SetFrameTitle(unsigned short const *,struct _GUID))"},
         &g_setFrameTitle, SetFrameTitleHook, true},
        {{LR"(public: virtual long __cdecl CExplorerFrame::SelectTab(struct _GUID))"},
         &g_selectTab, nullptr, true},
        {{LR"(public: virtual long __cdecl CExplorerFrame::AddTab(struct _ITEMIDLIST_ABSOLUTE const *,enum NewTabEntryPoint,struct _GUID *))"},
         &g_addTab, AddTabHook, true},
        {{LR"(public: virtual long __cdecl CExplorerFrame::AddTabAtIndex(struct _ITEMIDLIST_ABSOLUTE const *,enum NewTabEntryPoint,int,struct _GUID *))"},
         &g_addTabAtIndex, AddTabAtIndexHook, true},
        {{LR"(public: virtual long __cdecl CExplorerFrame::TranslateAcceleratorGlobal(struct tagMSG *))"},
         &g_translateGlobal, TranslateGlobalHook, true},
    };
    if (!WindhawkUtils::HookSymbols(g_explorerFrame, hooks, ARRAYSIZE(hooks))) return false;
    Wh_Log(L"Capabilities: title=%d, select=%d, add=%d, addAtIndex=%d, accelerator=%d",
           !!g_setFrameTitle, !!g_selectTab, !!g_addTab, !!g_addTabAtIndex, !!g_translateGlobal);
    return g_setFrameTitle || (g_selectTab && (g_addTab || g_addTabAtIndex));
}
}  // namespace Runtime

BOOL Wh_ModInit() {
    using namespace Runtime;
    auto registry = std::make_unique<Registry>();
    g_registry = registry.get();
    auto fail = [&] {
        if (g_explorerFrame) { FreeLibrary(g_explorerFrame); g_explorerFrame = nullptr; }
        g_registry = nullptr;
        return FALSE;
    };
    LoadSettings(true);
    GUID session;
    wchar_t sessionText[40]{};
    if (FAILED(CoCreateGuid(&session)) || !StringFromGUID2(session, sessionText, ARRAYSIZE(sessionText))) return fail();
    std::wstring name = L"SleekWinTabs.OwnerDispatch." + std::wstring(sessionText);
    g_dispatchMessage = RegisterWindowMessageW(name.c_str());
    if (!g_dispatchMessage || !HookExplorer()) return fail();
    if (!WindhawkUtils::SetFunctionHook(DispatchMessageW, DispatchWHook, &g_dispatchW) ||
        !WindhawkUtils::SetFunctionHook(DispatchMessageA, DispatchAHook, &g_dispatchA)) {
        Wh_Log(L"Could not hook input dispatch");
        return fail();
    }
    registry.release();
    return TRUE;
}
void Wh_ModBeforeUninit() {
    {
        std::lock_guard lock(Runtime::g_activityMutex);
        Runtime::g_stopping.store(true);
    }
    Runtime::Broadcast(Runtime::kShutdown);
    // Owner cleanup may be re-entered while an outer native call pumps sent
    // messages. Wait for that call to unwind before registry/trampoline teardown.
    std::unique_lock lock(Runtime::g_activityMutex);
    Runtime::g_activityChanged.wait(lock, [] { return Runtime::g_activeCallbacks.Idle(); });
}
void Wh_ModUninit() {
    if (Runtime::g_explorerFrame) FreeLibrary(Runtime::g_explorerFrame);
    delete Runtime::g_registry;
    Runtime::g_registry = nullptr;
}
BOOL Wh_ModSettingsChanged(BOOL* reload) {
    *reload = FALSE;
    if (Runtime::LoadSettings()) Runtime::Broadcast(Runtime::kCancel);
    return TRUE;
}

#endif
