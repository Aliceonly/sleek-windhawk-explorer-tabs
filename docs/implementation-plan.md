---
title: Sleek WinTabs：代码实施计划
subtitle: 七项功能、45 项设置；按依赖分阶段实现，每阶段有验收条件
lang: zh
template: doc
theme: paper
---

采用一个 mod、单文件安装的方案。先完成原生适配与选择调度，再实现各项行为。所有用户偏好进入配置；线程、对象寿命和文件操作正确性不提供关闭选项。

## A 范围与默认行为

本文件是实施计划，不表示功能已完成。编写计划时，产品代码仍为 `sleekwintabs.wh.cpp` v1.1.1。当前环境为 macOS，没有本轮 Windows 运行结果。

| 需求 | 默认行为 | 阶段 |
|---|---|---|
| ① 地址栏中键 | 原生打开被点击目录，跟随全局前后台设置 | P5 |
| ② 文件拖到标签标题 | 悬停确认目标，在标题上松手移动 | P9 |
| ③ 关闭后的选择 | 关闭任意 tab 后选其左邻；无左邻时选最左 | P6 |
| ④ 中键延迟优化 | 删除固定 150 ms，去重并按就绪事件选择 | P3 |
| ⑤ Ctrl+中键 | 强制后台打开，不先切过去再切回来 | P3 |
| ⑥ Ctrl+Q 复制 | 当前 tab 右侧新建副本并激活，恢复有效选择 | P4、P7 |
| ⑦ 多目录 Enter | 按当前视图顺序，打开到一个新窗口的 tab 中 | P8 |

保留标题修复和现有两个配置键。默认不接管 `Ctrl+D`，不注册系统全局快捷键。七项行为可独立关闭；来源不明时不覆盖原生操作。

不扩展为独立文件管理器。不添加全局窗口自动合并、同路径标签自动复用或完整历史克隆。后台新窗口与特殊目录必须通过对应能力验证。

## B 接入验证与失败处理

现有修改锚点：设置在 `sleekwintabs.wh.cpp:45`，全局队列在 `:127`，150 ms 常量在 `:133`。创建 hook 在 `:169` 和 `:191`，符号安装在 `:208`，卸载在 `:260`。这些是实施前行号，后续以函数名定位。

已有静态证据来自 `explorerframe.dll` 10.0.26100.9278 x64。扩展样本为 `FileExplorerExtensions.dll` 2607.29001.0.0。这不等于目标机器的运行验证。

| 编号 | 必须取得的证据 | 不通过时 |
|---|---|---|
| G0 构建与 ABI | Windhawk 编译链可用；XAML 声明、调用约定和对象身份可核实 | 不猜 IID、vtable 槽位、偏移或 entryPoint 数值 |
| G1 窗口与标签 | 能关联 frame、UI 线程、活动 tab 和真实显示顺序 | 不按标题、创建顺序或最新 HWND 猜测 |
| G2 输入与就绪 | 点击来源、Ctrl 状态、稳定选择时点和非编辑焦点可确认 | 不用时间窗口猜来源，不误吞 Enter |
| G3 新窗口 | 创建返回对象能唯一关联新窗口及其 tab actions | 停止该请求，不向前台窗口追加 |
| G4 面包屑 | 现代 XAML 命中项可解析为真实 Shell 目录 | 保留原行为，不拼显示名称或模拟按键 |
| G5 拖放 | 标题 Drop 路由、IDataObject 桥和 effect 反馈均正确 | 不接管投递，不自行复制删除 |

P0 优先验证 G0–G3。G4、G5 在对应阶段开始前补完，不阻塞已经验证的独立功能。所有结果写入兼容性矩阵，区分源码证据、静态证据和实机证据。

`AddTab` 会转调 `AddTabAtIndex`，不能重复提交选择。它们使用的接口子对象 `this`，也不能直接传给所有 frame 方法。原生适配层集中处理这些差异。

新窗口优先验证 `CLSID_ShellBrowserWindow` 返回 `IWebBrowser2` 的路线。保留创建返回对象，再导航首目录并关联 frame。该路线不适用时，才研究私有创建链。[Microsoft：Explorer 创建示例](https://learn.microsoft.com/en-us/windows/win32/shell/developing-with-windows-explorer)

## C 代码组织与状态模型

安装产物继续是 `sleekwintabs.wh.cpp`。内部按命名空间分段，不引入额外运行时。测试辅助文件可以独立存在，不成为安装依赖。

| 逻辑模块 | 对外职责 | 内部处理 |
|---|---|---|
| `Settings` | 发布有效配置快照 | 读取、默认值、迁移、校验与冲突 |
| `ExplorerAdapter` | 查询视图与顺序；创建、选择标签 | 私有 ABI、XAML 身份、COM 与版本能力 |
| `WindowRegistry` | 按窗口实例管理状态 | UI 线程、销毁事件、HWND 复用 |
| `InputRouter` | 输出带来源的操作意图 | 修饰键、键盘上下文、重复与编辑排除 |
| `SelectionController` | 决定是否选择及何时选择 | 嵌套去重、就绪、取消和额外延迟 |
| `ViewSnapshot` | 返回有序目录与选择快照 | PIDL 所有权、Shell 身份、视图顺序 |
| `DuplicateController` | 创建副本并恢复选择 | 与普通中键及批量规则隔离 |
| `BreadcrumbController` | 命中目录并请求原生打开 | UIA MTA worker、缓存与过期结果 |
| `CloseController` | 关闭成功后选择目标 | 后台关闭、重排、迁移和整窗退出 |
| `WindowFactory` / `BatchController` | 绑定一个窗口并依次创建 | 跨进程关联、失败、取消与节流 |
| `TabDropController` | 管理 Shell 拖放会话 | 目标切换、引用、修饰键和 effect |

以下只定义内部模型职责，不是 Windows 接口声明。

```cpp title="内部数据模型草案"
struct WindowToken;       // Process, owner thread, window, incarnation.
struct TabRef;            // GUID or validated XAML item identity.
struct OwnedPidl;         // Owned absolute Shell identifier.
struct ConfigSnapshot;   // Immutable values and configuration version.
struct OperationIntent;  // Source, sequence, modifiers, focus policy.
struct WindowState;      // Tabs, epochs, pending work and capabilities.
struct BatchJob;         // Ordered targets, results and next index.
struct DropSession;      // Data, target, allowed effects and settings.
```

区分操作序号、焦点意图代次和导航代次。主动切换、关闭或新的冲突输入会使旧选择失效。原生内部就绪事件不能冒充用户动作；连续创建保留全部 tab，只合并过期选择。

跨消息传递 token 与自有数据，不传裸 frame 指针。注册表在所属 UI 线程解析有效对象。`IsWindow` 不能单独防止 HWND 复用；跨进程不能传递指针。

后台请求不安排前台选择。如果原生创建本身强制激活，必须找到真正后台创建路径。“先切过去再切回来”不算通过后台验收。

## D 45 项配置的实现归属

采用已有配置矩阵，不减少约定设置。下表记录稳定 key、内部默认值和负责阶段。各选项的中文名称、范围和用途写入设置说明。

| key | 可选值 | 默认值 | 阶段 |
|---|---|---|---|
| `fixTabTitles` | 开 / 关 | `true` | P1 |
| `selectNewTabs` | 开 / 关 | `true` | P1、P3 |
| `openTabs.folderMiddleClickFocus` | 跟随全局 / 前台 / 后台 / 系统默认 | `inherit` | P3 |
| `openTabs.contextMenuFocus` | 跟随全局 / 前台 / 后台 / 系统默认 | `inherit` | P3 |
| `openTabs.position` | 系统默认 / 当前右侧 / 最右端 | `native` | P3 |
| `openTabs.ctrlMiddleClick` | 后台 / 前台 / 跟随普通中键 | `background` | P3 |
| `breadcrumb.enabled` | 开 / 关 | `true` | P5 |
| `breadcrumb.includeDropdown` | 开 / 关 | `true` | P5 |
| `breadcrumb.focus` | 跟随全局 / 前台 / 后台 | `inherit` | P5 |
| `breadcrumb.position` | 跟随普通开标签 / 当前右侧 / 最右端 | `inherit` | P5 |
| `tabDrop.enabled` | 开 / 关 | `true` | P9 |
| `tabDrop.operation` | 移动 / 复制 / 系统规则 | `move` | P9 |
| `tabDrop.hoverDelayMs` | 0–1000 ms | `0` | P9 |
| `tabDrop.afterDrop` | 留在目标 tab / 返回拖入前的活动 tab | `stay` | P9 |
| `tabDrop.allowExternalSources` | 开 / 关 | `true` | P9 |
| `closeTabs.selection` | 系统默认 / 最左 / 左邻 | `left` | P6 |
| `closeTabs.scope` | 任意关闭 / 仅关闭活动 tab | `any` | P6 |
| `closeTabs.noLeftNeighbor` | 最左存活 tab / 系统默认 | `leftmost` | P6 |
| `duplicate.enabled` | 开 / 关 | `true` | P4 |
| `duplicate.shortcut` | 合法快捷键字符串；空值禁用快捷键 | `Ctrl+Q` | P4 |
| `duplicate.target` | 当前窗口 / 新窗口 | `currentWindow` | P4、P7 |
| `duplicate.position` | 当前右侧 / 最右端 | `afterCurrent` | P4 |
| `duplicate.focus` | 前台 / 后台 / 跟随全局 | `foreground` | P4、P7 |
| `duplicate.restoreSelection` | 开 / 关 | `true` | P4 |
| `duplicate.scope` | Explorer 非编辑区域 / 仅文件列表 | `nonEditingExplorer` | P4 |
| `batchOpen.enabled` | 开 / 关 | `true` | P8 |
| `batchOpen.shortcut` | 合法快捷键字符串；空值禁用 | `Enter` | P8 |
| `batchOpen.minimumFolders` | 1–1000 | `2` | P8 |
| `batchOpen.target` | 一个新窗口 / 当前窗口 | `newWindow` | P8 |
| `batchOpen.order` | 当前视图 / 名称升序 / 名称降序 | `view` | P8 |
| `batchOpen.position` | 当前 tab 右侧 / 最右端 | `end` | P8 |
| `batchOpen.windowFocus` | 前台 / 后台 | `foreground` | P8 |
| `batchOpen.selectedTab` | 本批第一项 / 本批最后一项 / 保留原活动项 | `first` | P8 |
| `batchOpen.mixedSelection` | 保留系统行为 / 只处理文件夹 | `native` | P8 |
| `batchOpen.includeFolderShortcuts` | 开 / 关 | `false` | P8 |
| `batchOpen.includeShellFolders` | 开 / 关 | `false` | P8 |
| `batchOpen.onError` | 继续剩余项 / 停止剩余项 | `continue` | P8 |
| `batchOpen.maxFolders` | 0 或正整数 | `0` | P8 |
| `batchOpen.cancelShortcut` | 合法快捷键字符串；空值禁用 | `Escape` | P8 |
| `advanced.extraSelectDelayMs` | 0–1000 ms | `0` | P3 |
| `advanced.selectionDeadlineMs` | 100–10000 ms | `2000` | P3 |
| `advanced.batchCreateIntervalMs` | 0–1000 ms | `0` | P8 |
| `advanced.windowBindTimeoutMs` | 500–30000 ms | `5000` | P7 |
| `diagnostics.level` | 关闭 / 错误 / 调试 | `errors` | P1、P2 |
| `diagnostics.measureLatency` | 开 / 关 | `false` | P2、P10 |

每个 key 必须关联声明、读取器、校验器、消费者和测试。不能只增加设置页面，却让实现忽略设置。P1 先建立完整内部模型；阶段性版本明确未完成项，不把空实现伪装成可用功能。

旧版迁移保留两个现有键。新增字段的默认值与用户填写的 `false`、`0` 必须区分。先实测 Windhawk 更新时的默认合并行为；必要的版本标记只作内部存储。

规范化快捷键后检查冲突。拒绝重复绑定、未知枚举、越界值及矛盾参数。非法新配置保留上次有效快照；首次加载则使用安全默认值并报告错误。

额外选择延迟必须小于选择期限。批次上限为 0 时不限制；正数上限小于最小数量时拒绝配置。取消快捷键只在对应批次有效上下文内生效。

批次和拖放会话使用开始时的配置快照。禁用功能会取消未提交任务，但不撤回已成功的投递或创建。用户切换与关闭仍可使旧任务失效。

`Wh_ModBeforeUninit` 在移除 hook 前调用，`Wh_ModUninit` 在移除后调用。设置回调可请求 reload，不能误返回失败导致整个 mod 被卸载。[Windhawk：创建 mod](https://github.com/ramensoftware/windhawk/wiki/Creating-a-new-mod)、[生命周期](https://github.com/ramensoftware/windhawk/wiki/Mod-lifetime)


## E 分阶段实施清单

顺序为 **P0 → P1 → P2 → P3 → P4 → P5 → P6 → P7 → P8 → P9 → P10**。编号不是工期承诺。原生验证结果决定后续工作量。

### P0：编译基线与最小探针

- 记录目标 OS、两份 DLL、Windhawk、编译器版本及架构。
- 在 Windows 编译原版，记录中键延迟与焦点行为。
- 制作事件探针，验证 G0–G3 的路径与对象关联。
- 核实 XAML 声明能否由当前编译链使用，不为缺失头文件猜 ABI。

通过条件：原版可复现，能安全定位 frame、tab、输入和销毁事件。创建验证使用测试窗口，不导航已有窗口或移动文件。未通过的能力保留为未完成。

### P1：配置、策略类型与测试入口

- 替换匿名 `g_settings`，建立强类型设置与不可变快照。
- 定义 45 项声明、枚举、数值范围、快捷键解析和迁移规则。
- 分离不依赖 Windows 的策略逻辑，加入测试入口。
- 保持标题修复行为，不顺手修改其显示语义。

通过条件：覆盖默认值、所有枚举、边界、非法输入和迁移。现有用户配置不被重置，配置数量和消费者能一一核对。

### P2：原生适配、注册表与清理

- 拆分标题、创建、XAML、输入和拖放的能力依赖。
- 建立按窗口实例隔离的状态与 UI 线程消息通道。
- 对动态加载 DLL 和启用前已有窗口补建状态。
- 实现停止接收、撤销事件、清理队列和释放 COM 的退出流程。

通过条件：多窗口不串状态，销毁与卸载无迟到回调。可选能力缺失不拖累标题修复。关闭一个功能不影响其他功能。

卸载先停止新操作，再取消任务并撤销订阅，最后释放资源。清理遵守 UI 线程归属。不阻塞等待依赖当前线程的 worker，也不强杀执行 COM 的线程后卸载代码。

### P3：中键延迟、来源识别与 Ctrl 后台

- 用嵌套计数识别 AddTab 转调，最外层只提交一次结果。
- 替换全局 timer/vector，按窗口管理前台请求。
- 将输入时的 Ctrl 与来源绑定到实际创建，未知来源不猜。
- 用消息与就绪事件调度，支持额外延迟和选择期限。

通过条件：Ctrl 快速松开仍后台，紧接 Ctrl+T 不被误判。连续打开不漏 tab、不重复选择。后续主动切换后，不被旧回调抢回。

仅删除固定等待不算完成。必须证明选择不再被 Explorer 后续初始化覆盖。就绪时点以实测为准，不能等网络目录加载完才激活。

### P4：Ctrl+Q 复制到当前窗口

- 快照源 tab 的实际位置、目录 PIDL 和所选项目身份。
- 按配置插入副本，即使已有相同目录也不复用。
- 执行复制组的前后台规则，不套用 Ctrl+中键规则。
- 新视图就绪后恢复有效选择，关闭或导航后取消旧恢复。

通过条件：每次独立按压创建一个副本，长按不自动连开。重命名、文本编辑和输入法状态不误触发。默认完全不拦截 Ctrl+D。

此阶段完成当前窗口目标；新窗口目标在 P7 验收。

### P5：地址栏与下拉菜单中键

- 接入已验证的原生命中路径，必要时使用 UIA MTA worker。
- 复用 UIA，限定查询范围；输入回调只捕获与投递。
- 解析真实 PIDL，结果携带窗口和导航代次。
- 支持下拉目录、折叠祖先和特殊位置；不可靠时不处理。

通过条件：无 Alt+D、Ctrl+T、剪贴板或 Sleep 导航链。中文、同名祖先、长路径与 UNC 不误开目录。普通中键与 Ctrl+中键分别遵守配置。

### P6：关闭后选择最左或左邻

- 关闭前读取真实 TabItems，保存候选对象身份。
- 关闭成功且集合更新后复查候选，再提交选择。
- 区分后台关闭、重排、标签迁移、整窗退出和最后一项。
- 将关闭纳入统一选择控制，清除冲突的旧创建请求。

通过条件：按钮、标签中键、Ctrl+W 和菜单关闭均覆盖。重排后选对位置，RTL 按实际左右关系处理。后台关闭仅在配置要求时切换。

### P7：创建并绑定一个新窗口

- 保留创建返回的 IWebBrowser2，初始化首目录。
- 确认独立目标窗口，并关联 owner 线程与 tab actions。
- 处理绑定期限、销毁和跨进程模式；有歧义即停止。
- 为复制功能接入新窗口目标，分别验证前台和后台行为。

通过条件：并发创建相同目录窗口也不会误绑定。失败不导航已有窗口。后台模式不先闪到前台再切回；做不到时明确该选项未完成。

### P8：多文件夹 Enter 与有序批次

- 正向确认文件列表非编辑焦点，物化有序选择快照。
- 在创建前检查数量、混选、目录类型和上限。
- 选择配置目标，首目录只占一个槽位，不遗留 Home 空 tab。
- 按序追加并逐次让出消息循环，不等待目录内容全部加载。
- 单独处理批次焦点、Escape、错误策略和逐项结果。

通过条件：默认 N 个有效目录只新增一个窗口和 N 个 tab。顺序不受导航速度影响。消费 Enter 后若发生运行失败，不重放原按键造成二次打开。

创建失败与导航失败分别记录。tab 已插入但目录加载报错时，不重复建 tab。报告已开、失败和未执行数量，不静默丢弃项目。

顺序枚举使用 `IFolderView::Items` 与 `SVGIO_SELECTION | SVGIO_FLAG_VIEWORDER`。匹配用 Shell 身份，属性判定检查实际标志位。[Microsoft：Items](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifolderview-items)、[SVGIO](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ne-shobjidl_core-_svgio)

### P9：标题区域接收文件投递

- 验证 G5，只接管文件数据，保留标签重排和其他拖放区域。
- 悬停激活并确认目标目录，绑定目录 IDropTarget。
- 转发完整 DragEnter、DragOver、DragLeave 和 Drop 会话。
- 实现移动、复制、系统规则，以及来源、悬停和投递后选择设置。

通过条件：同盘、跨盘、冲突、权限失败、取消和只支持复制的来源正确处理。源数据不由 mod 自行删除，未知目标不能接收投递。

返回原 tab 只在操作意图仍有效时执行。文件传输进度与标签选择分离，不等传输结束后突然抢焦点。[Microsoft：IDropTarget](https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nn-oleidl-idroptarget)

### P10：整体验收与交付整理

- 完成 45 项设置追踪、交互组合与默认行为回归。
- 测量新旧延迟，检查动态设置、卸载和多窗口操作。
- 更新 README、内嵌说明、版本和实际支持矩阵。
- 移除临时探针与高频日志，只保留可复用诊断工具。

通过条件：默认七项功能在目标 Windows build 上验收通过。任何未通过项标为未完成，不能以自动禁用代替完成整个需求。

## F 测试与性能验收

不枚举 45 项设置的全部笛卡尔积。逐项覆盖合法值和非法值，再测试实际存在的交互组合。

| 测试层 | 内容 | 环境 |
|---|---|---|
| 策略 | 配置优先级、位置、左邻回退、排序、限额与热更新 | macOS 可执行 |
| 状态 | 去重、失效 token、超时、取消与任务竞争 | 假时钟和适配层测试替身 |
| 构建 | 单文件在 Windhawk 编译链中编译，无额外安装依赖 | Windows x64 |
| 交互 | 焦点、XAML、输入关联、Drop 数据桥与错误反馈 | 目标 Windows build |
| 性能 | 输入、创建到选择、首窗和批次插入耗时 | 同机、同版本、同数据对照 |

策略测试必须编译生产使用的同一份逻辑。可用测试宏屏蔽 Windows hook 部分，不能在测试中重写一份算法。策略测试通过不代表原生生命周期或 GUI 已验证。

关键组合：Ctrl+中键与快速释放、复制后台与全局前台、批次后台与自动选择。还需测试创建后立即关闭、重排后关闭、拖放中改配置，以及两个窗口同时执行相同动作。

下表是默认性能目标，不是已有成绩。

| 指标 | 目标或口径 |
|---|---|
| 输入钩子自身耗时 | p95 小于 1 ms，不做 UIA 查询或等待导航 |
| 暖本地中键到可见选中 | p95 不高于 100 ms，额外延迟为 0 |
| 固定等待 | 无旧版 150 ms，无模拟按键 Sleep 链 |
| 有效前台提交 | 同次创建最多一次成功的选择提交 |
| 采样 | 文件列表与地址栏各至少 100 次暖场景 |
| 批次 | 10 项和 50 项，记录首窗、全部插入与失败数 |

冷启动、网络和离线目录单独报告。内容加载与 tab 可见选中时间分开。日志记录操作序号和阶段，不默认泄露文件名或完整路径。

## G 文件改动与交付批次

以下是后续拟议文件，不是已经生成的实现。

| 文件 | 用途 |
|---|---|
| `sleekwintabs.wh.cpp` | 45 项声明、策略模块、原生适配、七项功能和清理 |
| `tests/core-tests.cpp` | 测试同一份纯策略、状态与设置解析代码 |
| `tools/run-core-tests.py` | 标准库测试启动器，不安装额外依赖 |
| `tools/collect-environment.ps1` | 只读采集 OS、DLL、Windhawk 和编译环境 |
| `tools/check-settings.py` | 检查声明、读取、消费者和测试的覆盖映射 |
| `docs/windows-acceptance.md` | 可复现的 GUI、拖放和性能测试步骤 |
| `docs/settings-contract.md` | 45 项选值、默认值、依赖和错误处理说明 |
| `README.md` | 安装方式、冲突 mod、设置与已实测支持范围 |

临时符号、DLL 和反汇编资料留在项目外缓存。测试脚本生成的日志、二进制和个人路径不提交。代码复用保留许可与署名；不将 GPLv3 参考实现复制进当前 MIT mod。

每个 P 阶段作为独立可检查的改动批次。可先交付已验证的基础与中键功能，再逐项扩展；但最终完成仍要求覆盖全部约定功能。版本更新不等于发布，本计划不授权自动 commit、推送或发布。

## H 开始条件与完成定义

当前即可开展的本地工作是 P1 的配置模型、纯策略和测试组织。Windows 编译及交互验收需要可访问的目标机器；macOS 结果不能替代它们。

实施时首先执行 P0：采集真实版本、确认编译链、运行原版基线和最小探针。若尚无 Windows 环境，可以推进独立的本地代码，但不能把未验证的私有调用标为可用。

最终完成要求：七项默认行为通过实机测试；45 项设置均有实现和验证。启用前已有窗口、多窗口、配置变更、功能关闭和卸载均无未清理任务。文件投递不误投、不误删，Ctrl+D 默认行为保持不变。

当前仍有明确缺口：真实机器的 ABI、输入到创建关联、DirectUI 编辑状态、新窗口身份和 XAML Drop 会话。`ExplorerTabPlus` 的准确来源也未确认；它不阻塞独立实现，但不能声称已对照其源码。
