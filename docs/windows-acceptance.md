# Windows acceptance: first implementation batch

This is a test procedure, not a record of passing Windows tests. Its authority
for intended behavior is [the implementation plan](implementation-plan.md).
The authoring host is macOS; Windows compilation, private ABI compatibility,
GUI behavior, and latency remain **NOT RUN** until evidence is attached below.
The original README's Windows 11 build 26100.9278 claim applies to the original
mod only, not this candidate or later features.

## 1. Scope and result rules

The current candidate scope is P1 settings/core and a practical subset of
P2/P3: per-window queued selection, nested AddTab/AddTabAtIndex deduplication,
owned COM references/cleanup, and Ctrl intent limited to the synchronous
DispatchMessage call scope. These are acceptance targets, not an assertion
that each route is already implemented or proven safe on a target build.

| Area | Current acceptance boundary | Initial result |
| --- | --- | --- |
| P1 settings/core | Validate the settings actually exposed and consumed by this candidate | NOT RUN |
| P2/P3 queued selection | Window isolation, deduplication, stable focus, reference release and unload | NOT RUN |
| P3 Ctrl intent | Test the verified synchronous DispatchMessage route only | NOT RUN |
| P3 asynchronous input correlation | No broad timing heuristic; require separate native evidence | UNVERIFIED / BLOCKED |
| All 45 planned settings | Retained staged contract; not 45 working controls in this batch | STAGED |
| P4/P7 duplicate, P5 breadcrumb, P6 close policy | Not part of the first-batch completion claim | PENDING INTEGRATION |
| P7/P8 new window/batch, P9 header drop | Not part of the first-batch completion claim | PENDING INTEGRATION |

Use PASS, FAIL, NOT RUN, BLOCKED, or PENDING INTEGRATION per case. A skipped
feature, a missing optional symbol, automatic feature disablement, or a
successful pure-core test is not a GUI PASS. Do not present this batch as
completion of the seven-feature request or of all of P2/P3.

Record the candidate source hash, exposed setting keys, defaults, effective
values and unavailable capabilities before testing. If a setting listed in
the plan is absent, mark it STAGED; do not invent its UI control or claim its
behavior. Verify unsupported options are not advertised as working controls.

## 2. Environment and collector checks (P0)

Use a normal Windows PowerShell 5.1 session initially; administrator rights
are not required. Prefer the native-architecture shell. A 32-bit collector
uses Sysnative for the native System32 DLL candidate, but module enumeration
can still fail across bitness. Record any access failures, not a guessed
version. Do not change machine execution policy just to run these tests.
Each file's `path` records the discovered candidate; `readPath` records the
actual path used for metadata/hash reads, including any Sysnative translation.

The collector reads OS fields, allowlisted registry values, file metadata,
SHA256/PE headers and Explorer PID/session/module information. It does not
read credentials, process owners/command lines, open folder paths or mod
settings, change registry values, restart Explorer, or enable mods.
Only an explicit `-OutFile` creates a file; it never overwrites an existing
file or creates its parent directory. No network or binary execution is used.

Run from the repository root. These are manual operator commands, not actions
performed by the collector:

```powershell
# Parse without executing. Run this in WINDOWS POWERSHELL 5.1, not only pwsh 7.
$tokens = $null
$parseErrors = $null
$scriptPath = (Resolve-Path .\tools\collect-environment.ps1).Path
$null = [System.Management.Automation.Language.Parser]::ParseFile(
    $scriptPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -gt 0) { $parseErrors; throw 'Collector syntax errors' }

# No output file and no Windhawk installation discovery.
$plainJson = & .\tools\collect-environment.ps1
$plain = $plainJson | ConvertFrom-Json
$plain.collector
$plain.windhawk

# Optional discovery and a uniquely named output file outside the repository.
$outFile = Join-Path $env:TEMP ('sleek-environment-' + [guid]::NewGuid() + '.json')
$json = & .\tools\collect-environment.ps1 -ProbeWindhawk -OutFile $outFile
$collected = $json | ConvertFrom-Json
$saved = Get-Content -LiteralPath $outFile -Raw -Encoding UTF8 | ConvertFrom-Json
$saved.files.artifacts.'ExplorerFrame.dll'
```

| ID | Input / action | Expected result |
| --- | --- | --- |
| E01 | Run the parser command under PowerShell 5.1 | Zero parse errors; no collector execution |
| E02 | Run without arguments, parse the entire success output as JSON | One JSON document; schemaVersion=1; windhawk.status=not_requested; no collector-created file |
| E03 | Run with -ProbeWindhawk and the unique -OutFile | JSON on stdout and equivalent UTF-8 JSON in that one file; no process/UI actions |
| E04 | Run again with the same -OutFile, comparing its SHA256 before/after | Clear file-exists failure; original file bytes unchanged |
| E05 | Supply a file under a nonexistent parent directory | Clear output error; directory not created |
| E06 | Compare CIM build with winver; registry CurrentBuildNumber and UBR with fullBuildFromRegistry | Report contains observed build/revision and architecture; missing values remain null/not_found, not substituted from the README |
| E07 | Inspect explorer.exe, ExplorerFrame.dll and FileExplorerExtensions.dll entries | Found candidates have version, SHA256 and PE machine evidence; unreadable fields carry their own errors; missing files are explicit |
| E08 | Compare listed Explorer PIDs/session IDs with Task Manager Details | Actual matching processes at collection time, or explicit not_found/error; no owner or command-line fields |
| E09 | Inspect optional Windhawk discovery | Candidate directory is confirmed by windhawk.exe; registry-only stale entry is not a verified installation; absent portable installs are not guessed |
| E10 | Where available, repeat without admin rights and from a 32-bit shell | Access/bitness limits remain visible in moduleProbeStatus/error; native DLL candidate uses Sysnative where applicable |

Top-level and per-probe statuses have separate meanings: `found` means the
named observation succeeded, not that every nested field succeeded;
`not_found` is limited to the enumerated scope; `access_denied`/`error` mean
failure; `incomplete` means discovery failed to establish absence;
`unavailable` means the API returned no path; `unsupported_platform` means
the collector did not run Windows probes. Read nested version/hash errors too.

FileExplorerExtensions discovery is bounded to reported module paths, a
native system-directory candidate and one directory level under SystemApps.
Windhawk discovery uses the exact Uninstall\Windhawk key in HKLM/HKCU and
applicable 32/64-bit views plus conventional paths. Neither search claims to
find every installation. An OS update or process exit during collection can
produce a mixed snapshot: repeat after the environment settles. Disk paths
are not proof that matching bytes are currently loaded in a process.

Manually add Windhawk version, compiler version/output, Windows display
language, Explorer separate-process setting and relevant enabled mod list.
The collector does not infer those from install paths. Reports contain local
paths and IDs; review/redact before sharing and do not commit personal logs.

## 3. Isolated fixtures and native/original baseline

Use disposable empty directories, not personal files. Create a test parent
with folders A, B, C and D; optionally add Unicode/long-path and UNC fixtures
for later cases. Prepare two Explorer windows W1 and W2 with clearly different
parents. Use W1/W2 labels in notes, not titles as programmatic identity.

Keep native, original v1.1.1 and candidate runs separate. Save the exact source
for each in the operator's test workspace and record its SHA256, rather than
calling the current working file "original" after it has changed. Keep the
same OS, inputs and settings for comparisons. Do not run overlapping tab mods,
ExplorerTabUtility or the standalone breadcrumb mod at the same time.

| ID | Input / action | Expected result / observation |
| --- | --- | --- |
| B01 | Disable Sleek WinTabs and overlapping tab tools manually; open fixtures | Record native tab opening/focus/close behavior; no candidate expectations are applied |
| B02 | Compile the saved original v1.1.1 in Windhawk | Preserve full build log and source hash; original must compile before serving as a baseline |
| B03 | Enable only original with fixTabTitles=true and selectNewTabs=true; middle-click A | Record tab count, focus transitions, title and visible-selection latency; its 150 ms source delay is not an end-to-end measurement |
| B04 | Original: set selectNewTabs=false and repeat, then independently toggle fixTabTitles | Capture original setting semantics for migration/regression comparison |
| B05 | Disable original, compile and explicitly enable the candidate | Capture build/hook logs and exact settings; no two copies of the same mod run together |
| B06 | Update using a test profile with original values false/false, then true/true | Candidate preserves both old keys/values; new defaults do not overwrite intentional false/0 |

If the original crashes during a destructive/lifecycle baseline, preserve
evidence and stop that baseline case. It is not necessary to reproduce crashes
repeatedly. Do not restart or terminate Explorer automatically.

## 4. First-batch selection and settings checks (P1 / partial P2-P3)

Reset fixtures between cases; record the complete effective settings for
every run. Unless overridden, use the candidate's ordinary-middle-click
foreground default and zero extra delay if that control is actually exposed.

| ID | Input / action | Expected result |
| --- | --- | --- |
| S01 | W1 file list: middle-click A once | Exactly one new tab; it becomes visibly selected and stays selected after view initialization; no delayed bounce back |
| S02 | Disable selectNewTabs, repeat using the baseline-equivalent policy | Native/background behavior remains; title setting still works independently |
| S03 | Compare context-menu Open in new tab, the + button and Ctrl+T | Each creates one tab; no redundant focus bounce or accidental Ctrl-middle policy; use each route's actual implemented policy |
| S04 | Capture one nested AddTab -> AddTabAtIndex opening using diagnostics | One logical scheduling result and at most one successful selection commit for that creation, not two queue entries/commits |
| S05 | Rapidly open 10 valid folders in W1; maintain the file-list source, returning to it as necessary | 10 new tabs for 10 valid opens; no lost/duplicate creation; obsolete selection requests are coalesced or cancelled, not replayed through every tab |
| S06 | Middle-click A then immediately manually select an existing tab before queued work finishes | User's newer selection remains; an old callback must not steal focus later |
| S07 | Open A and immediately close that new tab | No attempt to select the removed tab, no crash and no later focus jump |
| S08 | With a request pending, disable auto-selection or the candidate | No uncommitted selection occurs after the disable takes effect; completed tab creation is not undone |
| S09 | Where implemented, test valid setting boundaries, invalid values, conflicting shortcuts and zero values | Valid values affect their consumers; invalid snapshots are rejected with diagnostic evidence and preserve the last valid snapshot; staged fields are not falsely claimed supported |
| S10 | Toggle title fix independently; compare plain names, filesystem paths and special locations with original | No new title semantics or regressions; disabling title fix does not disable selection |
| S11 | Open a slow/network folder where a fixture is available | Selection does not intentionally wait for full directory loading; content latency is measured separately; unresolved readiness is a failure/blocker, not waived by PostMessage |
| S12 | During an in-flight native creation, change settings or explicitly select another tab before creation returns | Old creation does not re-arm selection with its old settings/intent epoch |
| S13 | Trace nested returns with the same GUID, and a distinct re-entrant creation on the same frame | Same GUID coalesces; distinct GUID remains independent and the newer intent wins; attach returns/unique trace |
| S14 | In a debugger/test harness, let native SelectTab pump a second selection-work message | No recursive native SelectTab commit; outer call unwinds before latest pending work runs |

For S04, GUI tab counts alone do not prove deduplication. Attach a trace or
counter tied to the operation and tab identity. Instrumentation is operator-
enabled and should avoid complete folder paths by default. If diagnostics
cannot prove the assertion, mark the trace-specific check BLOCKED.

### Ctrl intent: synchronous proof is not asynchronous coverage

The first batch captures Ctrl within synchronous DispatchMessage processing.
Only a trace showing input and tab creation in that same scope supports a
synchronous-route PASS. Do not infer source from key state at a delayed
callback, from a broad timestamp window or simply from a visually correct run.

| ID | Input / action | Expected result |
| --- | --- | --- |
| C01 | Hold Ctrl, middle-click a folder, then quickly release Ctrl | For a proven synchronous route, one background tab with no foreground flash; original selected tab stays selected |
| C02 | Repeat C01 but hold Ctrl until after creation | Same result as C01; no dependency on when a deferred task samples the key |
| C03 | Plain middle-click, then press Ctrl only after the click | Ordinary-middle-click policy applies, not Ctrl-background policy |
| C04 | Ctrl+middle-click followed immediately by Ctrl+T | First action obeys its own intent; Ctrl+T opens normally and does not inherit stale background intent |
| C05 | Alternate modified/unmodified middle-clicks across W1 and W2 | No intent leaks between operations/windows; each verified synchronous action obeys its own capture |
| C06 | Exercise an observed XAML/asynchronous creation path that occurs after DispatchMessage returns | Record the missing correlation as UNVERIFIED/BLOCKED; do not assert Ctrl-background support or mislabel unrelated opens as middle-click |
| C07 | Set extra delay to 500 ms; create on middle DOWN and release before selection is due | Paired UP keeps the DOWN request and captured Ctrl intent; it does not cancel its own open |
| C08 | While selection is delayed, use an accelerator consumed before DispatchMessage (for example Ctrl+Tab or Ctrl+W) | Optional TranslateAcceleratorGlobal trace confirms cancellation; missing capability is BLOCKED, not a fabricated dispatched-key PASS |

A C06 limitation blocks the broad promise that Ctrl+middle-click always works
on that build. It does not invalidate independently passing pure-core tests,
but must remain in the candidate's supported-scope notes. Source-unknown paths
must not acquire a fabricated Ctrl intent.

## 5. Window lifetime, unload and isolation

These close tests verify cancellation and safety, **not** the future P6
"leftmost/left neighbor" selection feature. Compare close selection to the
native baseline until P6 is explicitly integrated.

| ID | Input / action | Expected result |
| --- | --- | --- |
| L01 | Enable candidate with W1/W2 already open, then also open W3 | Correct capability behavior on both existing and newly created windows; unsupported attachment is explicit, not silent success |
| L02 | Queue an open in W1, immediately switch to/open in W2 | W1 work never selects W2's tabs or redirects navigation; newer user foreground-window choice is not stolen |
| L03 | Queue an open, immediately close its source window; repeat with a fresh window | No stale frame/COM access and no task applied to a new window reusing an HWND |
| L04 | Rapid opens in W1/W2, then close a background tab, the selected tab and the final tab | No stale selection/crash; final tab retains native window-close behavior; no P6 policy claim |
| L05 | During queued work, disable/unload the candidate via Windhawk UI | Intake stops; pending uncommitted work and owner-thread callbacks are drained/cancelled; no post-unload callback or hang |
| L06 | Repeat enable -> open -> disable cycles 20 times, including pre-existing windows | Explorer stays responsive; repeated cycles do not retain the mod's COM references, callbacks or timers |
| L07 | Disable/unload while both windows have pending work and one is closing | No UI/worker wait cycle, stale callback, or crash; other Explorer windows remain usable |
| L08 | Inspect unavailable-symbol/unsupported-build behavior when such an environment is available | Affected capability is explicitly unavailable; independent title behavior is not falsely reported broken/passed; no guessed ABI call |

For L03/L05-L07 record PID/session IDs before and after, Windhawk unload logs,
errors and a short interaction recording. A stable PID alone does not prove
safe cleanup. Add callback/reference counters or a debugger trace if available;
without them, label internal ownership checks BLOCKED rather than claiming
"no leaks" from Task Manager alone. Do not force-kill the process to manufacture
a successful unload. A spontaneous Explorer restart counts as a failure.

## 6. Performance procedure

The implementation plan's numbers are **targets, not measurements**:
input-hook p95 < 1 ms; warm local middle-click to visible selection p95 <=
100 ms with extra delay zero. Removing the old 150 ms timer alone is not a
PASS: selection must remain stable after asynchronous Explorer initialization.

1. Warm the same local fixtures, then measure at least 100 valid file-list
   middle-click opens each for original and candidate on the same machine.
2. Use an input marker/monotonic event trace for click time and correlated
   visible-selection time. Record creation return, selection commit and
   navigation-complete times separately when available. A hook duration is
   not visible UI latency; video frame timing has limited precision.
3. Report sample count, p50, p95 (sorted value at ceil(0.95*N)), maximum,
   failures, focus bounces and duplicate commits. Do not discard failed or
   slow samples without listing the exclusion and reason.
4. Separate cold, UNC/offline and warm-local results. Keep tracing overhead
   consistent; do not compare verbose candidate logging to quiet original
   logging as if equivalent. Missing instrumentation means NOT MEASURED.
5. Check source/trace for fixed waits, input-thread UIA and simulated-key
   navigation. Static absence of Sleep does not prove the <1 ms runtime target.

The 100-sample breadcrumb benchmark and 10/50-folder batch timings are deferred
until their modules are connected. No test here changes that status.

## 7. Later feature integration checklist (not implemented by this batch)

Run these only after the feature and its actual settings are connected. Until
then every row is **PENDING INTEGRATION**, not FAIL or PASS of a stub.

| Area | Future input / action | Expected result after integration |
| --- | --- | --- |
| Ctrl+Q / P4 | One press on a folder tab, existing same-path tab present; then hold the key | Distinct copy immediately right of source, default foreground; one copy per independent press; valid selection restored |
| Keyboard guards / P4-P8 | Ctrl+Q / Enter in address/search editing, rename or IME composition; press Ctrl+D | No accidental duplicate/batch; Ctrl+D remains native unless explicitly reassigned later |
| Duplicate settings / P4-P7 | Test target, position, focus and restore-selection controls once consumed | Settings affect the intended operation independently of ordinary middle-click; unsupported background-new-window mode is not advertised as complete |
| Breadcrumb / P5 | Middle-click and Ctrl+middle-click ancestor, dropdown and collapsed items; include duplicate names, Unicode, long paths and UNC | Correct Shell target/position/focus; no input-thread UIA wait, Alt+D/Ctrl+T/Sleep/clipboard navigation or name-based path guess |
| Close policy / P6 | Close active and inactive tabs after reorder using button, middle-click, Ctrl+W and menu; test first/last and RTL | Actual current visual left/leftmost policy, configured scope/fallback and native final-tab close |
| New window / P7 | Concurrently create identical-folder windows while duplicate/batch starts | Bind only to the returned target identity, never newest/foreground HWND; timeout does not navigate unrelated windows |
| Batch / P8 | Select 2, 10 and 50 folders in a deliberately non-name view order; press Enter | Exactly one new window, N tabs in frozen view order, no spare Home tab; first batch tab active by default; source unchanged |
| Batch controls / P8 | Mixed files, shortcuts, unsupported Shell folders, limits, reorder/sort options, partial failure and Escape | Respect only consumed controls; do not silently truncate, replay Enter after consumption or abandon counts of successful/failed/unexecuted items |
| Header drop / P9 | Drag disposable files onto a tab title and release, including same/cross-volume and external sources | Move to the confirmed tab directory by full Shell drop session; not merely hover switching; source/target must permit MOVE |
| Drop failure / P9 | Conflict, access denial, copy-only source, cancel, closed target, navigation during drag and mid-drag settings change | No mistaken directory, silent copy-as-move or manual source deletion; frozen session policy; native progress/conflict handling |

For drop testing, hash disposable contents before/after and verify source and
destination explicitly. Never use real documents to validate a new move
handler. Retain native tab reordering and non-header drop regression tests.

## 8. Evidence and release gate

Fill one row per environment/source/settings combination. Empty evidence is
NOT RUN, not a successful test. Keep raw personal logs outside the repository.

| Field | Recorded value |
| --- | --- |
| Date / tester / OS full build / architecture | NOT RUN |
| Environment JSON / actual loaded DLL paths / DLL hashes | NOT RUN |
| Windhawk / compiler versions and build log | NOT RUN |
| Native baseline / original source hash / candidate source hash | NOT RUN |
| Exposed settings / effective values / unavailable capabilities | NOT RUN |
| Case IDs / inputs / expected vs actual / result | NOT RUN |
| Trace or recording / latency samples / cleanup evidence | NOT RUN |
| Synchronous Ctrl route evidence / asynchronous limitations | NOT RUN |
| Failures and reproduction steps / deferred features | NOT RUN |

First-batch acceptance requires a successful target-Windows build, relevant
E/B/S/C/L evidence, preserved old settings, no false exposure of staged
settings, and accurate limitation notes. Failed safety/lifecycle tests block
shipping the affected path. Unverified asynchronous Ctrl correlation must not
be hidden behind a passing synchronous case. Full seven-feature acceptance
still requires later P4-P10 integration and all 45 planned settings to have
real consumers and validation; this document does not certify that outcome.

## Reference contracts

- [Microsoft: Win32_OperatingSystem](https://learn.microsoft.com/en-us/windows/win32/cimwin32prov/win32-operatingsystem) defines the observed build/version/architecture fields.
- [Microsoft: RegistryKey.OpenSubKey](https://learn.microsoft.com/en-us/dotnet/api/microsoft.win32.registrykey.opensubkey?view=netframework-4.8.1) documents read-only access when writable is false.
- [Microsoft: Get-FileHash, PowerShell 5.1](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.utility/get-filehash?view=powershell-5.1) documents the SHA256 and LiteralPath interface used here.
