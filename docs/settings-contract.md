# Settings contract - staged implementation

The approved design has **45 keys**. This preview exposes **8 implemented
controls**; the other 37 exist only in the contract model/tests, not in the
Windhawk UI and not as working runtime features. An internal default does not
enable an unconnected feature. See [implementation status](implementation-status.md).

`fixTabTitles` and `selectNewTabs` retain their existing names and values.
The first invalid/missing new-setting snapshot uses new defaults and preserves
both legacy booleans. Later invalid snapshots keep the entire last valid
snapshot. Zero/false are valid values, not missing-value markers. Windhawk's
integer API cannot distinguish an absent key from zero; new zero-default fields
therefore use zero semantics, while an absent positive-range field rejects the
snapshot rather than silently treating it as a valid user value. Default merge
and update behavior still need the Windows migration test.

## Connected controls

| Key | Consumer | Boundary |
| --- | --- | --- |
| `fixTabTitles` | `SetFrameTitleHook` | Same path-component behavior as v1.1.1 |
| `selectNewTabs` | `Core::ShouldSelect` | Global fallback, including unknown input sources |
| `openTabs.folderMiddleClickFocus` | `Core::ShouldSelect` | Synchronous dispatched middle-click only |
| `openTabs.ctrlMiddleClick` | `Core::ShouldSelect` | Ctrl captured at button-down, no delayed key sampling |
| `advanced.extraSelectDelayMs` | `Schedule` / `DispatchSelection` | 0 posts owner-window work; positive values use an HWND timer |
| `advanced.selectionDeadlineMs` | `SelectionQueue::Take` | Expired work is discarded; no retry loop |
| `diagnostics.level` | `LogErrors` / `LogDebug` | Settings/init capability messages remain visible |
| `diagnostics.measureLatency` | `DispatchSelection` | Enqueue-to-call, not visible UI latency |

Foreground requests are coalesced per owner window. Background/native policies
skip the mod's `SelectTab`; they do not reverse activation performed by Explorer.
A later background request cancels any older pending foreground request.
Unknown or asynchronous sources retain the legacy global selection rule and
are **not** certified Ctrl+middle-click routes.
The optional `TranslateAcceleratorGlobal` hook observes synchronous input that
Explorer consumes before `DispatchMessage`; it does not bridge asynchronous
XAML creation. Paired middle-button release retains button-down intent.
Settings changes invalidate in-flight creation snapshots. Known newer input,
window deactivation and destruction invalidate the owner window's intent epoch.

## Full schema

Choices use exact case-sensitive values. Booleans accept true/false; numeric
fields accept base-10 non-negative integers within the listed range. The table
is checked against the C++ schema and the 45-key implementation plan.

| Key | Default | Values / range | Runtime status |
| --- | --- | --- | --- |
| `fixTabTitles` | `true` | `true`, `false` | Connected; Windows unverified |
| `selectNewTabs` | `true` | `true`, `false` | Connected; Windows unverified |
| `openTabs.folderMiddleClickFocus` | `inherit` | `inherit`, `foreground`, `background`, `native` | Connected; Windows unverified |
| `openTabs.contextMenuFocus` | `inherit` | `inherit`, `foreground`, `background`, `native` | Staged, not exposed |
| `openTabs.position` | `native` | `native`, `afterCurrent`, `end` | Staged, not exposed |
| `openTabs.ctrlMiddleClick` | `background` | `background`, `foreground`, `inherit` | Connected; Windows unverified |
| `breadcrumb.enabled` | `true` | `true`, `false` | Staged, not exposed |
| `breadcrumb.includeDropdown` | `true` | `true`, `false` | Staged, not exposed |
| `breadcrumb.focus` | `inherit` | `inherit`, `foreground`, `background` | Staged, not exposed |
| `breadcrumb.position` | `inherit` | `inherit`, `afterCurrent`, `end` | Staged, not exposed |
| `tabDrop.enabled` | `true` | `true`, `false` | Staged, not exposed |
| `tabDrop.operation` | `move` | `move`, `copy`, `native` | Staged, not exposed |
| `tabDrop.hoverDelayMs` | `0` | 0-1000 ms | Staged, not exposed |
| `tabDrop.afterDrop` | `stay` | `stay`, `return` | Staged, not exposed |
| `tabDrop.allowExternalSources` | `true` | `true`, `false` | Staged, not exposed |
| `closeTabs.selection` | `left` | `native`, `leftmost`, `left` | Staged, not exposed |
| `closeTabs.scope` | `any` | `any`, `active` | Staged, not exposed |
| `closeTabs.noLeftNeighbor` | `leftmost` | `leftmost`, `native` | Staged, not exposed |
| `duplicate.enabled` | `true` | `true`, `false` | Staged, not exposed |
| `duplicate.shortcut` | `Ctrl+Q` | Shortcut string; empty disables the binding | Staged, not exposed |
| `duplicate.target` | `currentWindow` | `currentWindow`, `newWindow` | Staged, not exposed |
| `duplicate.position` | `afterCurrent` | `afterCurrent`, `end` | Staged, not exposed |
| `duplicate.focus` | `foreground` | `foreground`, `background`, `inherit` | Staged, not exposed |
| `duplicate.restoreSelection` | `true` | `true`, `false` | Staged, not exposed |
| `duplicate.scope` | `nonEditingExplorer` | `nonEditingExplorer`, `fileList` | Staged, not exposed |
| `batchOpen.enabled` | `true` | `true`, `false` | Staged, not exposed |
| `batchOpen.shortcut` | `Enter` | Shortcut string; empty disables the binding | Staged, not exposed |
| `batchOpen.minimumFolders` | `2` | 1-1000 | Staged, not exposed |
| `batchOpen.target` | `newWindow` | `newWindow`, `currentWindow` | Staged, not exposed |
| `batchOpen.order` | `view` | `view`, `nameAscending`, `nameDescending` | Staged, not exposed |
| `batchOpen.position` | `end` | `afterCurrent`, `end` | Staged, not exposed |
| `batchOpen.windowFocus` | `foreground` | `foreground`, `background` | Staged, not exposed |
| `batchOpen.selectedTab` | `first` | `first`, `last`, `preserve` | Staged, not exposed |
| `batchOpen.mixedSelection` | `native` | `native`, `foldersOnly` | Staged, not exposed |
| `batchOpen.includeFolderShortcuts` | `false` | `true`, `false` | Staged, not exposed |
| `batchOpen.includeShellFolders` | `false` | `true`, `false` | Staged, not exposed |
| `batchOpen.onError` | `continue` | `continue`, `stop` | Staged, not exposed |
| `batchOpen.maxFolders` | `0` | 0-2147483647 | Staged, not exposed |
| `batchOpen.cancelShortcut` | `Escape` | Shortcut string; empty disables the binding | Staged, not exposed |
| `advanced.extraSelectDelayMs` | `0` | 0-1000 ms | Connected; Windows unverified |
| `advanced.selectionDeadlineMs` | `2000` | 100-10000 ms | Connected; Windows unverified |
| `advanced.batchCreateIntervalMs` | `0` | 0-1000 ms | Staged, not exposed |
| `advanced.windowBindTimeoutMs` | `5000` | 500-30000 ms | Staged, not exposed |
| `diagnostics.level` | `errors` | `off`, `errors`, `debug` | Connected; Windows unverified |
| `diagnostics.measureLatency` | `false` | `true`, `false` | Connected; Windows unverified |

## Cross-field validation

Additional delay must be smaller than the selection deadline. A nonzero batch
maximum must be at least the minimum. Enabled shortcuts must not collide after
normalization. Invalid fields never partially overwrite a valid snapshot.
Batch/shortcut validation here is preparation, not an input hook or batch job.

Shortcut grammar: optional distinct Ctrl/Shift/Alt/Win modifiers, then one A-Z,
0-9, F1-F24, Enter, Escape, Tab, Space, Delete, Insert, Home, End, PageUp or
PageDown key. Modifier ordering/case are normalized. Empty disables the future
binding. No global hotkeys or shortcuts are registered by this preview.
In particular, default duplicate remains **Ctrl+Q**, never Ctrl+D.

## Verification

Run `python3 tools/check-settings.py` and `python3 tools/run-core-tests.py`.
The core tests validate all 45 schema entries and choices, boundaries, conflicts,
focus precedence and queue behavior using the actual production code.
These checks do not establish Windows input coverage or private ABI support.
