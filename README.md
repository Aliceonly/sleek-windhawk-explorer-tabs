# Sleek WinTabs

A single-file [Windhawk](https://windhawk.net/) mod for Windows 11 File
Explorer tabs, maintained in
[Aliceonly/sleek-windhawk-explorer-tabs](https://github.com/Aliceonly/sleek-windhawk-explorer-tabs).
Based on the original [SleekGPX](https://github.com/SleekGPX) project; original
authorship and the MIT license are preserved.

```sh
git clone git@github.com:Aliceonly/sleek-windhawk-explorer-tabs.git
```

All design reports, implementation notes and HTML editions are in the
[documentation index](docs/README.md).

## Current Candidate: 1.2.0-alpha.1

**Implementation preview, not the completed seven-feature release.** This
candidate has not been run in Windows/Explorer. The original v1.1.1 build
26100.9278 compatibility claim does not certify this modified version.

- Keeps the existing folder-name title fix and both original setting keys.
- Replaces the fixed 150 ms global timer with owner-window queued work.
- Suppresses duplicate scheduling from nested AddTab -> AddTabAtIndex calls.
- Holds the tab-actions COM reference until selection/cancellation; isolates
  pending work by window instance and cancels it during close/unload.
- Adds configurable ordinary/Ctrl-middle-click focus for creation occurring
  synchronously inside the intercepted mouse-input call.

**Not connected yet:** breadcrumb middle-click, header file-drop, close-tab
selection policy, Ctrl+Q duplication, and ordered multi-folder Enter batches.
Ctrl+D remains untouched; this preview captures no keyboard shortcut.
The full 45-key design is retained as a staged contract, but only **8 connected
settings** appear in the current Windhawk panel.

See [implementation status](docs/implementation-status.md),
[settings and limitations](docs/settings-contract.md), and the
[Windows acceptance procedure](docs/windows-acceptance.md).

## Important Boundaries

Window association requires the hooked frame's public `IOleWindow` interface
on its owning UI thread. Unsupported association skips selection, without
guessing private object offsets. Symbol absence disables the affected
capability; an independent title hook can still work. A resolved symbol is
not proof that a future Explorer ABI remains compatible or crash-free.

Ctrl is captured from the button-down message, not sampled later. Creation
after the input call returns has no established causal association and falls
back to the original global `selectNewTabs` rule. **Universal Ctrl-background
support is not claimed.** Background/native policies omit the mod's selection;
they do not reverse a foreground activation performed by Explorer itself.

Zero extra delay means posted work after the creation call, not a proven
navigation-readiness event. Stable selection without an initialization bounce
must pass Windows testing before release. Logged enqueue-to-call latency is not
input-to-visible-selection latency.

## Install For Isolated Testing

1. Save a copy of your existing mod/settings; disable overlapping tab mods.
2. In Windhawk, create or edit a local custom mod.
3. Paste the contents of [`sleekwintabs.wh.cpp`](sleekwintabs.wh.cpp).
4. Compile and enable it in a disposable Explorer test window.
5. Follow the [acceptance checklist](docs/windows-acceptance.md). Do not use
   normal working windows for initial lifetime/unload tests.

There is no extra runtime file or service to install. No automatic deployment,
Explorer restart, or file-move operation is performed by the repository tools.

## Local Checks

```sh
python3 tools/check-settings.py
python3 tools/run-core-tests.py
python3 tools/run-core-tests.py --sanitize
```

Tests include the production pure core directly from the single mod source;
they do not reimplement its algorithms. The contract checker distinguishes
all 45 planned keys from the 8 exposed/consumed settings.

Optional cross-compilation uses Zig and the official Windhawk SDK headers:

```sh
python3 tools/check-windows-compile.py --sdk /path/to/windhawk-headers
```

This produces and discards an AMD64 COFF object. It does not link a Windhawk
DLL or test private ABI/GUI behavior. See the
[build evidence and reproduction commands](docs/implementation-status.md).

On Windows PowerShell 5.1, collect read-only environment evidence:

```powershell
.\tools\collect-environment.ps1 -ProbeWindhawk
```

It prints JSON; an optional `-OutFile` creates a new file only. It does not
change the registry, restart processes, activate mods, or access credentials.
Review local paths/IDs before sharing the report.

## License

MIT - see [LICENSE](LICENSE). No third-party mod implementation was copied
into this preview; the Windows SDK and Windhawk headers are external build
inputs, not vendored product code.
